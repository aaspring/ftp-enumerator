#include "ctrlSecurity.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>

#include "ftpReturnCodes.h"
#include "ctrlEnd.h"
#include "parseResp.h"
#include "ctrlSend.h"
#include "ftpCommands.h"
#include "impOracles.h"
#include "ctrlRead.h"

#include "../include/terminationCodes.h"
#include "../include/logger.h"
#include "../include/ftpEnumerator.h"
#include "../include/interestCodes.h"
#include "../include/dbKeys.h"
#include "../include/outputDB.h"
#include "../include/dataChannel.h"
#include "../include/ctrlChannel.h"
#include "../include/recorder.h"
#include "../include/recordKeys.h"

extern void ctrl_read_cb(struct bufferevent *bev, void* args);
extern void ctrl_event_cb(struct bufferevent *bev, short triggerEvent, void* args);

void post_ending_optional_sec_check(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("post-ending-optional-sec-check", "%s", state->ip_address_str);
	state->terminationCode = SUCCESS_EXIT;
	snprintf(
					state->terminationDesc,
					MAX_STATIC_BUFFER_SIZE,
					"%s",
					"SUCCESS OK");
	disconnect_pleasant(bev, state);
}

void post_middle_optional_sec_check(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("post-middle-optional-sec-check", "%s", state->ip_address_str);

	state->terminationCode = state->secProbeStorageInt;
	snprintf(
					state->terminationDesc,
					MAX_STATIC_BUFFER_SIZE,
					"%s",
					state->secProbeStorageStr
	);
	disconnect_pleasant(bev, args);
}


void send_pbsz(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-pbsz-init", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_PBSZ_INIT_SENT, "%s\r\n", CMD_PBSZ);
	log_trace("send-pbsz-init", "%s: Sent initial PBSZ", state->ip_address_str);
}


void send_prot(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-prot", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_PROT_SENT, "%s\r\n", CMD_PROT);
	log_trace("send-prot", "%s: Sent PROTO", state->ip_address_str);
}


void init_tls_connection(struct bufferevent* bev, hoststate_t* state) {
	log_trace("init-tls-connection", "%s", state->ip_address_str);

	if (state->secType == SEC_UNK) {
		state->ctrlSec = SSL_new(gconfig.primarySecCtx);
		state->secType = SEC_PRIMARY;
		log_debug(
							"init-tls-connection",
							"%s: Using Primary Security",
							state->ip_address_str
		);
	}
	else if (state->secType == SEC_PRIMARY){
		state->ctrlSec = SSL_new(gconfig.secondarySecCtx);
		state->secType = SEC_SECONDARY;
		log_debug(
							"init-tls-connection",
							"%s: Using Secondary Security",
							state->ip_address_str
		);
	}

	if (!SSL_set_fd(state->ctrlSec, state->ctrlSocket)) {
		log_error(
						"init-tls-connection",
						"%s: ERROR in SSL_set_fd()\n",
						state->ip_address_str
		);
		state->terminationCode = FAIL_HARD_INTERNAL;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"ERROR setting ctrl ssl filedesc"
		);
		disconnect_abrupt(bev, state);
		return;
	}
	bufferevent_disable(bev, EV_READ | EV_WRITE);
	state->ctrlBev = bufferevent_openssl_socket_new(
																									gconfig.ev_base,
																									-1,
																									state->ctrlSec,
																									BUFFEREVENT_SSL_CONNECTING,
																									BEV_OPT_CLOSE_ON_FREE
	);

	if (state->ctrlBev == NULL) {
		log_error(
							"init-tls-connection",
							"%s: ERROR creating secure ctrl socket",
							state->ip_address_str
		);
		state->terminationCode = FAIL_HARD_INTERNAL;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"ERROR creating secure ctrl socket"
		);
		disconnect_abrupt(bev, state);
		return;
	}

	bufferevent_setcb(state->ctrlBev, ctrl_read_cb, NULL, ctrl_event_cb, state);
	bufferevent_set_timeouts(state->ctrlBev, &gconfig.readTimer, NULL);
	state->ctrlUsingSec = true;
  log_trace("init-tls-connection", "%s: Connecting", state->ip_address_str);
  change_state(state, S_SECURITY_CONNECTING);
}

void log_security_certs(struct bufferevent* bev, hoststate_t* state) {
	log_trace("log-security-certs", "%s", state->ip_address_str);
	BUF_MEM* bioPtr;
	char* stackPemCert = NULL;

	// Handle the peer cert
	X509* destCert = SSL_get_peer_certificate(state->ctrlSec);
	BIO* bio = BIO_new(BIO_s_mem());
	PEM_write_bio_X509(bio, destCert);
	BIO_get_mem_ptr(bio, &bioPtr);
	int certLen = bioPtr->length;
	char* pemCert = malloc(certLen + 1);
	BIO_read(bio, pemCert, certLen);
	pemCert[certLen] = '\0';

	record_string(state, RECKEY_PEER_CERT, pemCert);

	BIO_free(bio);
	free(pemCert);

	STACK_OF(X509)* certStack = SSL_get_peer_cert_chain(state->ctrlSec);
	if (certStack == NULL) {
		certStack = sk_X509_new_null();
		sk_X509_push(certStack, destCert);
	}
	// The reference counter is incremented by SSL_get_peer_certificate() so we
	// have to free it explicitly.
	X509_free(destCert);

	str_queue_t chainQueue;
	str_queue_init_custom_buffer_size(&chainQueue, MAX_PEM_CERT_SIZE);

	unsigned stackSize = sk_X509_num(certStack);
	unsigned i;
	X509* stackCert = NULL;
	for (i = 0; i < stackSize; i++) {
		stackCert = (X509*) sk_X509_value(certStack, i);

		BIO* stackBio = BIO_new(BIO_s_mem());
		PEM_write_bio_X509(stackBio, stackCert);

		BUF_MEM* stackBioPtr;
		BIO_get_mem_ptr(stackBio, &stackBioPtr);
		int stackCertLen = stackBioPtr->length;

		stackPemCert = malloc(stackCertLen + 1);
		BIO_read(stackBio, stackPemCert, stackCertLen);
		stackPemCert[stackCertLen] = '\0';

		str_enqueue(&chainQueue, stackPemCert);

		BIO_free(stackBio);
		free(stackPemCert);
	}

	record_string_queue(state, RECKEY_CERT_CHAIN, &chainQueue);

	str_queue_destroy(&chainQueue);
	// The reference counters aren't incremented by SSL_get_peer_cert_chain(), so
	// we don't free anything else.

	return;
}

void log_ssl_data(struct bufferevent* bev, hoststate_t* state) {
	log_trace("log-ssl-data", "%s", state->ip_address_str);

	log_security_certs(bev, state);

	record_string(state, RECKEY_TLS_CIPHER, SSL_get_cipher(state->ctrlSec));
	record_string(
								state,
								RECKEY_TLS_CIPHER_VER,
								SSL_get_cipher_version(state->ctrlSec)
	);
	record_int(
						state,
						RECKEY_TLS_CIPHER_BITS,
						SSL_get_cipher_bits(state->ctrlSec, NULL)
	);

	if (state->secType == SEC_PRIMARY) {
		record_string(state, RECKEY_SECURITY_TYPE, "SSLv23");
	}
	else {
		assert(state->secType == SEC_SECONDARY);
		record_string(state, RECKEY_SECURITY_TYPE, "SSLv3");
	}
}

void read_auth_ssl_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-auth-ssl-resp", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-auth-ssl-resp",
								"%s: Partial response found : %s",
								state->ip_address_str,
								DBuffer_start_ptr(&state->ctrlDBuffer)
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_AUTH_SSL_RESP, NULL, 0, &state->ctrlDBuffer);
			if (is_reply_code(replyCode, FTP_TLS_OK)) {
				log_trace(
									"read-auth-ssl-resp",
									"%s: Accepted AUTH SSL",
									state->ip_address_str
				);

				if (state->secProbe) {
					state->interestMask |= INTEREST_OPTIONAL_SECURITY;
				}

				DBuffer_clear(&state->ctrlDBuffer);
				init_tls_connection(bev, state);
			}
			else {
				if (state->postSecConnectCB == disconnect_pleasant) {
					state->terminationCode = SUCCESS_EXIT;
					snprintf(
									state->terminationDesc,
									MAX_STATIC_BUFFER_SIZE,
									"%s",
									"SUCCESS OK"
					);
					disconnect_pleasant(bev, state);
				}
				else if (
								(state->postSecConnectCB == post_ending_optional_sec_check)
								|| (state->postSecConnectCB == post_middle_optional_sec_check)
				) {
					state->postSecConnectCB(bev, state);
				}
				else {
					unexpected_reply(bev, state, FAIL_SSL_REFUSED, "read-auth-ssl-resp");
				}
			}
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-auth-ssl-resp");
			break;

	case (RESP_ERROR) :
			break;
	}
}

void read_auth_tls_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-auth-tls-resp", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-auth-tls-resp",
								"%s: Partial response found : %s",
								state->ip_address_str,
								DBuffer_start_ptr(&state->ctrlDBuffer)
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_AUTH_TLS_RESP, NULL, 0, &state->ctrlDBuffer);
			if (is_reply_code(replyCode, FTP_TLS_OK)) {
				log_trace(
									"read-auth-tls-resp",
									"%s: Accepted AUTH TLS",
									state->ip_address_str
				);
				DBuffer_clear(&state->ctrlDBuffer);

				if (state->secProbe) {
					state->interestMask |= INTEREST_OPTIONAL_SECURITY;
				}

				init_tls_connection(bev, state);
			}
			else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_NO_TLS)) {
				log_debug(
									"read-auth-tls-resp",
									"%s: No TLS allowed",
									state->ip_address_str
				);
				if (state->postSecConnectCB == disconnect_pleasant) {
					state->terminationCode = SUCCESS_EXIT;
					snprintf(
									state->terminationDesc,
									MAX_STATIC_BUFFER_SIZE,
									"%s",
									"SUCCESS OK"
					);
					disconnect_pleasant(bev, state);
				}
				else if (
								(state->postSecConnectCB == post_middle_optional_sec_check)
								|| (state->postSecConnectCB == post_ending_optional_sec_check)
				) {
					state->postSecConnectCB(bev, state);
				}
				else {
					unexpected_reply(bev, state, FAIL_TLS_REFUSED, "read-auth-tls-resp");
				}
			}
			else if (is_reply_code_strict(replyCode, FTP_SECURITY_REQ)) {
				state->interestMask |= INTEREST_TRIED_AUTH_SSL;
				log_trace(
									"read-auth-tls-resp",
									"%s: Requesting AUTH SSL",
									state->ip_address_str
				);
				send_auth_ssl(bev, state);
			}
			else if (
							strcmp(
											DBuffer_start_ptr(&state->ctrlDBuffer),
											ORACLE_OPTIONAL_PRE_SEC
							) == STR_MATCH
			) {
				log_info(
								"read-auth-tls-resp",
								"%s: Won't upgrade to TLS post login",
								state->ip_address_str
				);
				assert(state->secProbe);
				handle_early_sec_req(bev, state);
			}
			else {
				log_info(
								"read-auth-tls-resp",
								"%s: Assuming a rejection response",
								state->ip_address_str
				);

				if (state->postSecConnectCB == disconnect_pleasant) {
					state->terminationCode = SUCCESS_EXIT;
					snprintf(
									state->terminationDesc,
									MAX_STATIC_BUFFER_SIZE,
									"%s",
									"SUCCESS OK"
					);
					log_info(
									"read-auth-tls-resp",
									"%s: postSecConnectCB evaluation of disconnect_pleasant()",
									state->ip_address_str
					);
					disconnect_pleasant(bev, state);
				}
				else if (
								(state->postSecConnectCB == post_middle_optional_sec_check)
								|| (state->postSecConnectCB == post_ending_optional_sec_check)
				) {
					state->postSecConnectCB(bev, state);
					log_info(
									"read-auth-tls-resp",
									"%s: postSecConnectCB evaluation of and post_???_optional_sec_check()",
									state->ip_address_str
					);
				}
				else {
					unexpected_reply(bev, state, FAIL_TLS_REFUSED, "read-auth-tls-resp");
				}
			}
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-auth-tls-resp");
			break;

	case (RESP_ERROR) :
			break;
	}
}

void read_pbsz_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-pbsz-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-pbsz-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_PBSZ_RESP, NULL, 0, &state->ctrlDBuffer);
			if (is_reply_code(replyCode, FTP_PBSZ_OK)) {
				send_prot(bev, state);
			}
			else {
				if (
						(state->postSecConnectCB == post_ending_optional_sec_check)
						|| (state->postSecConnectCB == post_ending_optional_sec_check)
				) {
					state->postSecConnectCB(bev, state);
				}
				else {
					unexpected_reply(bev, state, FAIL_PBSZ_OK, "read-pbsz-resp");
				}
			}
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-pbsz-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void read_prot_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-prot-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-prot-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_FOUND) :

			record_raw_dbuffer(state, RECKEY_PROT_RESP, NULL, 0, &state->ctrlDBuffer);
			if (is_reply_code(replyCode, FTP_PROT_OK)) {

				if (state->postSecConnectCB == disconnect_pleasant) {
					state->terminationCode = SUCCESS_EXIT;
					snprintf(
									state->terminationDesc,
									MAX_STATIC_BUFFER_SIZE,
									"%s",
									"SUCCESS OK"
					);
				}
				state->postSecConnectCB(bev, state);
			}
			else {
				if (
						(state->postSecConnectCB == post_ending_optional_sec_check)
						|| (state->postSecConnectCB == post_middle_optional_sec_check)
				) {
					state->postSecConnectCB(bev, state);
				}
				else {
					unexpected_reply(bev, state, FAIL_PROT_OK, "read-prot-resp");
				}
			}

			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-prot-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void setup_ending_optional_sec_check(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("setup-ending-optional-sec-check", "%s", state->ip_address_str);
	state->postSecConnectCB = post_ending_optional_sec_check;
	state->secProbe = true;
	send_auth_tls(bev, state);
}

void setup_middle_optional_sec_check(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("setup-middle-optional-sec-check", "%s", state->ip_address_str);

	if (state->ctrlUsingSec) {
		return disconnect_pleasant(bev, state);
	}

	state->secProbeStorageInt = state->terminationCode;
	snprintf(
					state->secProbeStorageStr,
					MAX_STATIC_BUFFER_SIZE,
					"%s",
					state->terminationDesc
	);
	state->postSecConnectCB = post_middle_optional_sec_check;
	state->secProbe = true;
	send_auth_tls(bev, state);
}

void send_auth_tls(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-auth-tls", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_AUTH_TLS_SENT, "%s\r\n", CMD_AUTH_TLS);
	log_trace("send-auth-tls", "%s: Sent AUTH TLS", state->ip_address_str);
}

void send_auth_ssl(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-auth-ssl", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_AUTH_SSL_SENT, "%s\r\n", CMD_AUTH_SSL);
	log_trace("send-auth-ssl", "%s: Sent AUTH SSL", state->ip_address_str);
}
