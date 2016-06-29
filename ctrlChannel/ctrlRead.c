#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>

#include "ctrlRead.h"
#include "ctrlSend.h"
#include "ftpReturnCodes.h"
#include "ftpCommands.h"
#include "impOracles.h"
#include "ctrlEnd.h"
#include "parseResp.h"
#include "badPort.h"
#include "ctrlSecurity.h"
#include "ctrlDeciders.h"

#include "../include/terminationCodes.h"
#include "../include/ctrlChannel.h"
#include "../include/interestCodes.h"
#include "../include/magicNumbers.h"
#include "../include/threadStates.h"
#include "../include/ftpEnumerator.h"
#include "../include/dataChannel.h"
#include "../include/DBuffer.h"
#include "../include/outputDB.h"
#include "../include/logger.h"
#include "../include/cfaaBannerCheck.h"
#include "../include/recorder.h"
#include "../include/recordKeys.h"

extern void handle_robots_txt(struct bufferevent* bev, void* args);
extern void end_channel_now(struct bufferevent *bev, hoststate_t* state);
extern void fetch_robots_txt(struct bufferevent* bev, hoststate_t* state);

void reset_wait_event(evutil_socket_t fd, short triggerEvent, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("reset-wait-event", "%s", state->ip_address_str);
	evtimer_del(state->resetWaitTimer);
	event_free(state->resetWaitTimer);
	state->resetWaitTimer = NULL;
	state->resetWaitPostFunc(state->ctrlBev, state);
}

void reset_wait_setup(
		struct bufferevent* bev,
		hoststate_t* state,
		ResetWaitPost_f postFunc
) {
	log_trace("reset-wait-setup", "%s", state->ip_address_str);
	state->resetWaitTimer = evtimer_new(gconfig.ev_base, reset_wait_event, state);
	evtimer_add(state->resetWaitTimer, &one_second);
	state->resetWaitPostFunc = postFunc;
	change_state(state, S_RESET_WAIT);
}

void read_reset_wait(struct bufferevent* bev, hoststate_t* state) {
	log_trace("reset-wait-read", "%s", state->ip_address_str);
	DBuffer_t tempDBuffer;

	DBuffer_init(&tempDBuffer, DBUFFER_INIT_TINY);

	fill_DBuffer_all(bev, state, &tempDBuffer);
	state->interestMask |= INTEREST_CHECK_MISC;
	record_misc_partial_raw_1(
														state,
														"Recieved resp while waiting for possible RESET -- %s",
														DBuffer_start_ptr(&tempDBuffer),
														tempDBuffer.len
	);

	DBuffer_destroy(&tempDBuffer);
}

void read_security_connecting(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_debug("read-security-connecting", "%s");
	state->terminationCode = FAIL_REALLY_BAD_ERROR;
	snprintf(
					state->terminationDesc,
					MAX_STATIC_BUFFER_SIZE,
					"%s",
					"SOME HOW GOT A REPLY WHILE TRYING TO CONNECT SECURITY"
	);
	disconnect_abrupt(bev, state);
	return;
}

void multi_resp_timeout(evutil_socket_t fd, short triggerEvent, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("multi-resp-timeout", "%s", state->ip_address_str);

	if (evbuffer_get_length(bufferevent_get_input(state->ctrlBev)) == 0) {
		state->multiRespEvalFunc(state->ctrlBev, state, RESP_FOUND);
	}
	else {
		state->multiRespEvalFunc(state->ctrlBev, state, RESP_EXTRA);
	}
}

void multi_resp_setup(
		struct bufferevent* bev,
		hoststate_t* state,
		MultiRespEvalFunc_f evalFunc
) {
	log_trace("multi-resp-setup", "%s", state->ip_address_str);
	state->multiRespState = MULTI_RESP_READ_ONE;
	state->multiRespTimer = evtimer_new(gconfig.ev_base, multi_resp_timeout, state);
	state->multiRespEvalFunc = evalFunc;
	evtimer_add(state->multiRespTimer, &one_second);

}

void multi_resp_end(hoststate_t* state) {
	log_trace("multi-resp-end", "%s", state->ip_address_str);
	if (state->multiRespTimer) {
			evtimer_del(state->multiRespTimer);
			event_free(state->multiRespTimer);
			state->multiRespTimer = NULL;
	}
	state->multiRespState = MULTI_RESP_UNK;
	state->multiRespEvalFunc = NULL;
}

void handle_extra_resp_error(
		struct bufferevent* bev,
		hoststate_t* state,
		char* functionStr
) {
	log_error(
						functionStr,
						"%s: Extra data in buffer %s <+> %.*s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->ctrlDBuffer),
						evbuffer_get_length(bufferevent_get_input(bev)),
						evbuffer_pullup(bufferevent_get_input(bev), -1)
	);
	state->terminationCode = FAIL_EXTRA_RESP;
	snprintf(
					state->terminationDesc,
					MAX_STATIC_BUFFER_SIZE,
					"Extra response found in %s",
					functionStr
	);
	DBuffer_append(&state->ctrlDBuffer, " <+> ");
	size_t evBufferLen = evbuffer_get_length(bufferevent_get_input(bev));
	DBuffer_append_bytes(
											&state->ctrlDBuffer,
											(char*) evbuffer_pullup(bufferevent_get_input(bev), -1),
											evBufferLen
	);
	record_raw_dbuffer(state, RECKEY_EXTRA_RESP, NULL, 0, &state->ctrlDBuffer);
	evbuffer_drain(bufferevent_get_input(bev), evBufferLen);
	DBuffer_clear(&state->ctrlDBuffer);
	disconnect_pleasant(bev, state);
}

void post_retr_wait(evutil_socket_t fd, short triggerEvent, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	struct bufferevent* bev = state->ctrlBev;

	log_trace("post-retr-wait", "%s -- 0x%x", state->ip_address_str, triggerEvent);

	if (state->retrWaitTimer != NULL) {
		evtimer_del(state->retrWaitTimer);
		event_free(state->retrWaitTimer);
		state->retrWaitTimer = NULL;
	}

	if (strcmp(DBuffer_start_ptr(&state->itemDBuffer), ROBOTS_TXT) == STR_MATCH) {
		state->robotPresence = ROBOTS_NO;
	}

	robot_parser_init(&state->robotObj, "", USER_AGENT);

	if (state->dataBev) {
		data_close_channel(state->dataBev, state);
	}

	state->pasvPost(bev, state);

}


void help_list_xfer(struct bufferevent* bev, hoststate_t* state) {
	log_trace("help-list-xfer", "%s", state->ip_address_str);

	change_state(state, S_LIST_XFER);

	if (state->ctrlUsingSec) {
		data_tls_init(state->ctrlBev, state);
	}

	if (evbuffer_get_length(bufferevent_get_input(bev)) != 0) {
		// This is to solve the issue of replies building up in the buffer when you
		// LIST an empty directory.
		log_debug(
						"help-list-xfer",
						"%s: LIST-done already in buffer",
						state->ip_address_str
		);
		read_list_done(bev, state);
	}
}

void read_retr_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-retr-resp", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-retr-resp",
								"%s: Found partial response : %s",
								state->ip_address_str,
								DBuffer_start_ptr(&state->ctrlDBuffer)
			);
			return;
	case (RESP_EXTRA) :
			log_debug(
								"read-retr-resp",
								"%s: Extra data in buffer",
								state->ip_address_str
			);
			// no break
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_RETR_RESP, NULL, 0, &state->ctrlDBuffer);

			if (is_reply_code(replyCode, FTP_RETR_OK)) {
				if (strcmp(state->itemDBuffer.startPtr, ROBOTS_TXT) == STR_MATCH) {
					state->interestMask |= INTEREST_HAS_ROBOTS;
					state->robotPresence = ROBOTS_YES;
				}
				log_debug(
									"read-retr-resp",
									"%s: RETR resp read",
									state->ip_address_str
				);
				change_state(state, S_RETR_XFER);

				if (state->ctrlUsingSec) {
					data_tls_init(state->ctrlBev, state);
				}

				if (evbuffer_get_length(bufferevent_get_input(bev)) != 0) {
					// This is to solve the issue of replies building up in the buffer when you
					// RETR an empty/non-existant robots.txt
					log_debug(
									"read-retr-resp",
									"%s: RETR-done already in buffer",
									state->ip_address_str
					);
					read_retr_done(bev, state);
				}
			}
			else if (
							(is_reply_code(replyCode, FTP_NO_FILE))
							|| (is_reply_code(replyCode, FTP_NO_FILE_2))
			) {
				log_debug(
									"read-retr-resp",
									"%s: No file %s",
									state->ip_address_str,
									DBuffer_start_ptr(&state->itemDBuffer)
				);

				change_state(state, S_RETR_XFER);

				if (state->dataBev) {
					data_close_channel(state->dataBev, state);
				}

				state->retrWaitTimer = evtimer_new(gconfig.ev_base, post_retr_wait, state);
				evtimer_add(state->retrWaitTimer, &one_second);

				log_trace(
							"read-retr-resp",
							"%s: Setup retr-WAIT timer",
							state->ip_address_str
				);

			}
			else {
				unexpected_reply(bev, state, FAIL_RETR_OK, "read-retr-resp");
			}
			break;
	case (RESP_ERROR) :
			break;
	}
	return;
}

void read_type_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-type-resp", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch(res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-type-resp",
								"%s: Partial response found : %s",
								state->ip_address_str,
								DBuffer_start_ptr(&state->ctrlDBuffer)
			);
			break;
	case (RESP_FOUND) :
			if (is_reply_code(replyCode, FTP_TYPE_OK)) {
				log_debug("read-type-resp", "%s: Accepted TYPE change");
				DBuffer_clear(&state->ctrlDBuffer);
			}
			else {
				unexpected_reply(bev, state, FAIL_TYPE_OK, "read-type-resp");
			}
			break;
	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-type-resp");
			break;
	case (RESP_ERROR) :
			break;
	}

	return;
}


void read_additional_banner_resp(struct bufferevent* bev, hoststate_t* state) {
	log_trace("read-additional-banner-resp", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];

	DBuffer_clear(&state->ctrlDBuffer);

	RespResult_e resp = parse_response(bev, state, replyCode);
	switch (resp) {
	case (RESP_FOUND) :
			state->interestMask |= INTEREST_CHECK_MISC;
			record_misc_partial_raw_1(
												state,
												"Extra banner data -- %s",
												DBuffer_start_ptr(&state->ctrlDBuffer),
												state->ctrlDBuffer.len
			);


			if (is_reply_code(replyCode, FTP_EARLY_SEC_REQ)) {
				log_debug(
									"read-additional-banner-resp",
									"%s: Banner indicates security required",
									state->ip_address_str
				);
				state->interestMask |= INTEREST_SECURITY_REQUIRED;
				state->postSecConnectCB = send_user;
				send_auth_tls(bev, state);
			}
			else {
				log_debug(
									"read-additional-banner-resp",
									"%s: Not acting on extra banner",
									state->ip_address_str
				);
				if (state->earlySecReq) {
					if (state->secProbe) {
						state->postSecConnectCB = disconnect_pleasant;
					}
					else {
						state->postSecConnectCB = send_user;
					}
					send_auth_tls(bev, state);
				}
				else {
					send_user(bev, state);
				}
			}
			break;
	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-additional-banner-resp");
			break;
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-additional-banner-resp",
								"%s: Found additional banner line |%s|",
								state->ip_address_str,
								DBuffer_start_ptr(&state->ctrlDBuffer)
			);
			break;
	case (RESP_ERROR) :
			// everything already handled
			break;
	}
}

void read_banner(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-banner", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];
	char cfaaMsg[MAX_STATIC_BUFFER_SIZE];

	RespResult_e resp = parse_response(bev, state, replyCode);
	switch(resp) {
	case (RESP_EXTRA) :
			log_debug(
								"read-banner",
								"%s: Multiple responses found",
								state->ip_address_str
			);
			state->interestMask |= INTEREST_EXTRA_BANNER_RESP;
			// no break
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_BANNER, NULL, 0, &state->ctrlDBuffer);

			if (is_reply_code(replyCode, FTP_CONNECTED)) {
				log_debug(
									"read-banner",
									"%s: Found banner : %s",
									state->ip_address_str,
									DBuffer_start_ptr(&state->ctrlDBuffer)
				);

				if (strcasestr(DBuffer_start_ptr(&state->ctrlDBuffer), ORACLE_VX_WORKS)) {
					state->interestMask |= INTEREST_VX_WORKS;
					state->isVxWorks = true;
					log_info(
									"read-banner",
									"%s: Is a VxWorks system",
									state->ip_address_str
					);
				}

				if (
						cfaa_login_not_allowed(
																	DBuffer_start_ptr(&state->ctrlDBuffer),
																	cfaaMsg,
																	MAX_STATIC_BUFFER_SIZE
						)
				) {
					state->terminationCode = FAIL_CFAA_BAN;
					snprintf(
									state->terminationDesc,
									MAX_STATIC_BUFFER_SIZE,
									"CFAA prevented : %s",
									cfaaMsg
					);
					return setup_middle_optional_sec_check(bev, state);
				}

				if (resp == RESP_EXTRA) {
					read_additional_banner_resp(bev, state);
				}
				else {
					if (state->earlySecReq) {
						if (state->secProbe) {
							state->postSecConnectCB = disconnect_pleasant;
						}
						else {
							state->postSecConnectCB = send_user;
						}
						return send_auth_tls(bev, state);
					}
					else if (state->secProbe) {
						state->postSecConnectCB = disconnect_pleasant;
						return send_auth_tls(bev, state);
					}
					else {
						send_user(bev, state);
					}
				}
			}
			else if (
								(STR_MATCH == strncmp(
												DBuffer_start_ptr(&state->ctrlDBuffer),
												ORACLE_VPS_NO_HOST_1,
												sizeof(ORACLE_VPS_NO_HOST_1) - 1
												)
								)
								|| (STR_MATCH == strncmp(
												DBuffer_start_ptr(&state->ctrlDBuffer),
												ORACLE_VPS_NO_HOST_2,
												sizeof(ORACLE_VPS_NO_HOST_2) - 1
												)
								)
			) {
				state->terminationCode = FAIL_VPS_NO_HOST;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"VPS allows connection w/o host"
				);
				return disconnect_abrupt(bev, state);
			}
			else if (STR_MATCH == strncmp(
																			DBuffer_start_ptr(&state->ctrlDBuffer),
																			ORACLE_VSFTPD_BANNER_ERROR,
																			sizeof(ORACLE_VSFTPD_BANNER_ERROR) - 1
																			)
			) {
				state->terminationCode = FAIL_VSFTPD_BANNER_ERROR;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"vsftpd error in banner"
				);
				return disconnect_abrupt(bev, state);
			}
			else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_SESSION_LIMIT)) {
				state->terminationCode = FAIL_SESSION_LIMIT;
				snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Server's session limit reached"
				);
				return disconnect_abrupt(bev, state);
			}
			else {
				unexpected_reply(bev, state, FAIL_BANNER_RET, "read-banner");
			}
			break;
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-banner",
								"%s: Found non-reply banner line |%s|",
								state->ip_address_str,
								DBuffer_start_ptr(&state->ctrlDBuffer)
			);
			break;
	case (RESP_ERROR) :
			// everything already handled
			return;
	}
}

void read_syst_resp(struct bufferevent *bev, void *args) {
	char replyCode[REPLY_CODE_SIZE];
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-syst-resp", "%s", state->ip_address_str);

	RespResult_e res = parse_response(bev, state, replyCode);
	switch(res) {
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_SYST_RESP, NULL, 0, &state->ctrlDBuffer);

			if (is_reply_code(replyCode, FTP_SYST_OK)) {
				if (state->isVxWorks) {
					log_info(
									"read-syst-resp",
									"%s: Is a VxWorks type system",
									state->ip_address_str
					);
					state->destType = VXWORKS;

				}
				else if (strcasestr(DBuffer_start_ptr(&state->ctrlDBuffer), "nix") != 0) {
					log_info(
									"read-syst-resp",
									"%s: Is a UNIX type system",
									state->ip_address_str
					);
					state->destType = UNIX;
				}
				else if (
						(strcasestr(DBuffer_start_ptr(&state->ctrlDBuffer), "window") != 0)
				){
					log_info(
									"read-syst-resp",
									"%s: Is a Windows type system",
									state->ip_address_str
					);
					state->destType = WINDOWS;
				}
				else {
					log_info(
									"read-syst-resp",
									"%s: Is a Unknown type system",
									state->ip_address_str
					);
					state->destType = UNK;
				}
			}
			else {
				state->interestMask |= INTEREST_SYST_FAILED;
				log_info(
								"read-syst-resp",
								"%s: SYST failed to return expected code",
								state->ip_address_str
				);
				if (state->isVxWorks) {
					state->destType = VXWORKS;
				}
				else {
					state->destType = UNK;
				}
			}

			if (gconfig.crawlType == SHORT_CRAWL) {
				DBuffer_clear(&state->itemDBuffer);
				DBuffer_append(&state->itemDBuffer, "/");
				state->pasvAction = send_list;
				state->pasvPost = disconnect_pleasant;
				send_pasv(bev, state);
			}
			else {
				send_pwd(bev, state);
			}
			break;
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-syst-resp",
								"%s: Read partial response",
								state->ip_address_str
			);
			break;
	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-syst-resp");
			break;
	case (RESP_ERROR) :
			return;
	}
}
void handle_early_sec_req(struct bufferevent* bev, hoststate_t* state) {
	log_trace("handle-early-sec-req", "%s", state->ip_address_str);
	if (state->earlySecReq == true) {
		log_info(
						"handle-early-sec-req",
						"%s: Escaping early security loop.",
						state->ip_address_str
		);
		state->terminationCode = FAIL_EARLY_SEC_LOOP;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Early security loop detected"
		);
		disconnect_abrupt(bev, state);
		return;
	}

	log_info(
					"handle-early-sec-req",
					"%s: Requires early security",
					state->ip_address_str
	);

	delete_all_timers(state);

	state->interestMask |= INTEREST_EARLY_SEC_REQ;

	bufferevent_free(state->ctrlBev);

	state->state = S_INITIAL;
	state->ctrlUsingSec = false;
	DBuffer_clear(&state->itemDBuffer);
	DBuffer_clear(&state->ctrlDBuffer);
	DBuffer_clear(&state->dataDBuffer);

	state->earlySecReq = true;

	return ctrl_connect(state);
}

static inline void inline_user_resp_reject(
		struct bufferevent* bev,
		hoststate_t* state
) {
	state->terminationCode = FAIL_USER_REJECTED;
	log_debug(
						"read-user-resp",
						"%s: %s",
						state->ip_address_str,
						state->terminationDesc
	);
	DBuffer_clear(&state->ctrlDBuffer);
	setup_middle_optional_sec_check(bev, state);
}


void read_user_resp_eval(struct bufferevent* bev, void* args, RespResult_e res) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-user-resp-eval", "%s: res == 0x%x", state->ip_address_str, res);

	multi_resp_end(state);

	UserRespID_e userRespID = id_user_resp(
																				state,
																				state->multiRespFirstReplyCode,
																				res
	);
	log_debug(
						"read-user-resp",
						"%s: userRespID : %d",
						state->ip_address_str,
						userRespID
	);

	switch (userRespID) {
	case (USER_Virtual_Sites) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			state->terminationCode = FAIL_VIRTUAL_SITES;
			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"USER resp indicates MS Virtual Sites"
			);
			log_debug(
								"read-user-resp",
								"%s: %s",
								state->ip_address_str,
								state->terminationDesc
			);
			DBuffer_clear(&state->ctrlDBuffer);
			disconnect_pleasant(bev, state);
			break;

	case (USER_TLS_Optimistic) :
			state->interestMask |= INTEREST_OPTIMISTIC_SECURITY;
			// no break
	case (USER_TLS_Other) :
			// no break
	case (USER_TLS_530) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			state->interestMask |= INTEREST_SECURITY_REQUIRED;
			state->postSecConnectCB = send_user;
			send_auth_tls(bev, state);
			break;

	case (USER_Reject_530) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"USER was rejected by 530"
			);
			inline_user_resp_reject(bev, state);
			break;

	case (USER_Reject_Explicit) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"USER was explicitly rejected"
			);
			inline_user_resp_reject(bev, state);
			break;

	case (USER_No_Pass) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			state->interestMask |= INTEREST_NO_PASS;
			state->interestMask |= INTEREST_ALLOWED_LOGIN;
			if (gconfig.crawlType == SUPER_SHORT_CRAWL) {
				state->terminationCode = SUCCESS_EXIT;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"SUCCESS OK"
				);
				disconnect_pleasant(bev, state);
			}
			else { // SHORT or NORMAL
				fetch_robots_txt(bev, state);
			}
			break;

	case (USER_Accept_User) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			log_trace(
								"read-user-resp",
								"%s: Successfully read username response",
								state->ip_address_str
			);
			send_pass(bev, state);
			break;

	case (USER_Early_Sec_Req) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			handle_early_sec_req(bev, state);
			break;

	case (USER_Bad_Config) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			state->terminationCode = FAIL_BAD_USER_CONFIG;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Oracle'd bad server config"
			);
			DBuffer_clear(&state->ctrlDBuffer);
			setup_middle_optional_sec_check(bev, state);
			break;

	case (USER_Reject_Double_1) :
			log_debug(
								"read-user-resp",
								"%s: Known Double line user-resp found #1. Treating as USER_REJECT",
								state->ip_address_str
			);
			fill_DBuffer_all(bev, state, &state->ctrlDBuffer);
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);

			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Rejected USER with double response #3"
			);
			inline_user_resp_reject(bev, state);
			break;

	case (USER_Extra) :
			state->terminationCode = FAIL_EXTRA_RESP;
			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Extra response found in read-user-resp"
			);
			DBuffer_append(&state->ctrlDBuffer, " <+> ");
			size_t evBufferLen = evbuffer_get_length(bufferevent_get_input(bev));
			DBuffer_append_bytes(
													&state->ctrlDBuffer,
													(char*) evbuffer_pullup(bufferevent_get_input(bev), -1),
													evBufferLen
			);
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			evbuffer_drain(bufferevent_get_input(bev), evBufferLen);
			DBuffer_clear(&state->ctrlDBuffer);
			disconnect_pleasant(bev, state);
			break;

	default :
	case (USER_Unexpected) :
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			unexpected_reply(bev, state, FAIL_USER_RET, "read-user-resp");
			break;
	}
}

void read_user_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-user-resp", "%s", state->ip_address_str);

	if (state->multiRespState == MULTI_RESP_READ_ONE) {
		return read_user_resp_eval(bev, state, RESP_EXTRA);
	}
	assert(state->multiRespState == MULTI_RESP_READ_ZERO);

	RespResult_e res = parse_response(bev, state, state->multiRespFirstReplyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-user-resp",
								"%s: Read partial response",
								state->ip_address_str
			);
			break;

	case (RESP_EXTRA) :
			return read_user_resp_eval(bev, state, RESP_EXTRA);

	case (RESP_FOUND) :
				log_debug(
									"read-user-resp",
									"%s: Found response |%.*s|",
									state->ip_address_str,
									state->ctrlDBuffer.len - 2,
									DBuffer_start_ptr(&state->ctrlDBuffer)
				);

			return multi_resp_setup(bev, state, read_user_resp_eval);

	case (RESP_ERROR) :
			return;
	}
}

void read_quit_resp(struct bufferevent* bev, void* args) {
	char replyCode[REPLY_CODE_SIZE];
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-quit-resp", "%s", state->ip_address_str);

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-quit-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_QUIT_RESP, NULL, 0, &state->ctrlDBuffer);

			if (is_reply_code(replyCode, FTP_QUIT_OK)) {
				log_trace(
									"read-quit-resp",
									"%s: Successfully QUIT",
									state->ip_address_str
				);
				DBuffer_clear(&state->ctrlDBuffer);
				change_state(state, S_QUIT_ACK);
				record_string(state, RECKEY_DISCONNECT_RESULT, "PLEASANT");
				end_channel_now(bev, state);
			}
			else {
				log_error(
									"read-quit_resp",
									"%s: resp failure : %s",
									state->ip_address_str,
									DBuffer_start_ptr(&state->ctrlDBuffer)
				);
				DBuffer_clear(&state->ctrlDBuffer);
				disconnect_abrupt(bev, state);
			}
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-quit-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

static inline void inline_pass_resp_reject(
		struct bufferevent* bev,
		hoststate_t* state
) {
	state->terminationCode = FAIL_PASS_REJECTED;
	log_info(
					"read-pass-resp",
					"%s: %s",
					state->ip_address_str,
					state->terminationDesc
	);
	DBuffer_clear(&state->ctrlDBuffer);
	reset_wait_setup(bev, state, setup_middle_optional_sec_check);
}

void read_pass_resp_eval(struct bufferevent* bev, void* args, RespResult_e res) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-pass-resp-eval", "%s: res == 0x%x", state->ip_address_str, res);

	multi_resp_end(state);

	PassRespID_e respID = id_pass_resp(state, state->multiRespFirstReplyCode, res);
	log_debug(
						"read-pass-resp",
						"%s: respID : %d",
						state->ip_address_str,
						respID
	);

	switch (respID) {
	case (PASS_Firewall) :
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			state->terminationCode = FAIL_FIREWALL_AUTH;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"220 happy firewall detected"
			);
			DBuffer_clear(&state->ctrlDBuffer);
			return disconnect_pleasant(bev, state);

	case (PASS_OK) :
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			log_trace(
								"read-pass-resp",
								"%s: Successfully read password resp",
								state->ip_address_str
			);
			state->interestMask |= INTEREST_ALLOWED_LOGIN;
			if (state->secProbe) {
				state->terminationCode = SUCCESS_EXIT;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"SUCCESS OK"
				);
				disconnect_pleasant(bev, state);
			}

			if (gconfig.crawlType == SUPER_SHORT_CRAWL) {
				state->terminationCode = SUCCESS_EXIT;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"SUCCESS OK"
				);
				disconnect_pleasant(bev, state);
			}
			else { // SHORT or NORMAL
				fetch_robots_txt(bev, state);
			}
			break;

	case (PASS_Reject_530) :
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Remote rejected PASS with 530"
			);
			inline_pass_resp_reject(bev, state);
			break;

	case (PASS_Reject_550) :
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Remote rejected PASS with 550"
			);
			inline_pass_resp_reject(bev, state);
			break;

	case (PASS_Reject_421) :
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Remote rejected PASS with 421"
			);
			inline_pass_resp_reject(bev, state);
			break;

	case (PASS_Reject_Double_1_2) :
			log_debug(
								"read-pass-resp",
								"%s: Double '421/530 Login incorrect.' Treating as FAIL_LOGIN",
								state->ip_address_str
			);
			fill_DBuffer_all(bev, state, &state->ctrlDBuffer);
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);

			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Rejected PASS with double '421/530 Login incorrect.'"
			);
			inline_pass_resp_reject(bev, state);
			break;

	case (PASS_Reject_Double_3) :
			log_debug(
								"read-pass-resp",
								"%s: Known Double line pass-resp #3 found. Treating as FAIL_LOGIN",
								state->ip_address_str
			);
			fill_DBuffer_all(bev, state, &state->ctrlDBuffer);
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);

			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Rejected PASS with double response #3"
			);
			inline_pass_resp_reject(bev, state);
			break;

	case (PASS_Reject_Double_4) :
			log_debug(
								"read-pass-resp",
								"%s: Known Double line pass-resp #4 found. Treating as FAIL_LOGIN",
								state->ip_address_str
			);
			fill_DBuffer_all(bev, state, &state->ctrlDBuffer);
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);

			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Rejected PASS with double response #4"
			);
			inline_pass_resp_reject(bev, state);
			break;

	case (PASS_Extra) :
			state->terminationCode = FAIL_EXTRA_RESP;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Extra response found in read-pass-resp"
			);
			DBuffer_append(&state->ctrlDBuffer, " <+> ");
			size_t evBufferLen = evbuffer_get_length(bufferevent_get_input(bev));
			DBuffer_append_bytes(
													&state->ctrlDBuffer,
													(char*) evbuffer_pullup(bufferevent_get_input(bev), -1),
													evBufferLen
			);
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			evbuffer_drain(bufferevent_get_input(bev), evBufferLen);
			DBuffer_clear(&state->ctrlDBuffer);
			disconnect_pleasant(bev, state);
			break;

	case (PASS_Bftpd_Crash) :
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Remote bftpd instance crashed"
			);
			state->terminationCode = FAIL_BFTPD_CRASH;
			log_error(
								"read-pass-resp",
								"%s: PASS-resp shows bftpd crashed",
								state->ip_address_str
			);
			disconnect_abrupt(bev, state);
			break;

	case (PASS_Child_Died) :
			log_debug(
								"read-pass-resp",
								"%s: Child died after PASS",
								state->ip_address_str
			);
			fill_DBuffer_all(bev, state, &state->ctrlDBuffer);
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);

			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"PASS caused child to die"
			);
			state->terminationCode = FAIL_PASS_CHILD_DIED;
			disconnect_abrupt(bev, state);
			break;

	case (PASS_No_Entry) :
			log_debug(
								"read-pass-resp",
								"%s: No entry after PASS",
								state->ip_address_str
			);
			fill_DBuffer_all(bev, state, &state->ctrlDBuffer);
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);

			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"'No entry' after PASS"
			);
			state->terminationCode = FAIL_PASS_NO_ENTRY;
			disconnect_abrupt(bev, state);
			break;

	case (PASS_Unexpected) :
	default :
			record_raw_dbuffer(
												state,
												RECKEY_PASS_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			unexpected_reply(bev, state, FAIL_PASS_OK, "read-pass-resp");

	}
}


void read_pass_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-pass-resp", "%s", state->ip_address_str);

	if (state->multiRespState == MULTI_RESP_READ_ONE) {
		return read_pass_resp_eval(bev, state, RESP_EXTRA);
	}
	assert(state->multiRespState == MULTI_RESP_READ_ZERO);

	RespResult_e res = parse_response(bev, state, state->multiRespFirstReplyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-pass-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;

	case (RESP_EXTRA) :
			log_debug(
								"read-pass-resp",
								"%s: Found multiple resp at once",
								state->ip_address_str
			);
			return read_pass_resp_eval(bev, state, RESP_EXTRA);

	case (RESP_FOUND) :
			log_debug(
								"read-pass-resp",
								"%s: Setting up multi-resp",
								state->ip_address_str
			);
			return multi_resp_setup(bev, state, read_pass_resp_eval);

	case (RESP_ERROR) :
			return;
	}
}

void read_pwd_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-pwd-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-pwd-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_PWD_RESP, NULL, 0, &state->ctrlDBuffer);
			char pwdSBuffer[MAX_STATIC_BUFFER_SIZE];
			bool foundPath = false;

			if (is_reply_code(replyCode, FTP_PWD_OK)) {
				char* respPtr = DBuffer_start_ptr(&state->ctrlDBuffer);
				char* firstQuotePtr = strchr(respPtr, '"');
				if (firstQuotePtr) {
					char* secondQuotePtr = strchr(firstQuotePtr + 1, '"');
					if (
							(firstQuotePtr)
							&& (secondQuotePtr)
							&& (*(firstQuotePtr + 1) == '/')
					) {
						size_t numChar = secondQuotePtr - firstQuotePtr - 1;
						if (numChar < MAX_STATIC_BUFFER_SIZE - 2) {
							memcpy(pwdSBuffer,firstQuotePtr + 1, numChar);

							if (*(secondQuotePtr - 1) != '/') {
								pwdSBuffer[numChar] = '/';
								pwdSBuffer[numChar + 1] = '\0';
							}
							else {
								pwdSBuffer[numChar] = '\0';
							}

							str_enqueue(&state->dirQueue, pwdSBuffer);
							foundPath = true;
						}
					}
				}
			}

			if (!foundPath) {
				state->interestMask |= INTEREST_CHECK_MISC;
				record_misc_string(state, "Couldn't parse PWD response. Assuming '/'");
				str_enqueue(&state->dirQueue, "/");
			}

			walk_dir_init(bev, state);
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-pwd-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void read_pasv_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-pasv-resp", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-pasv-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(
												state,
												RECKEY_PASV_RESP,
												DBuffer_start_ptr(&state->itemDBuffer),
												state->itemDBuffer.len,
												&state->ctrlDBuffer
			);

			if (!is_reply_code(replyCode, FTP_PASV_OK)) {
				unexpected_reply(bev, state, FAIL_PASV_RESP, "read-pasv-resp");
				break;
			}

			char* beginGrp = strchr(DBuffer_start_ptr(&state->ctrlDBuffer), '(');
			if (beginGrp == NULL) {
				unexpected_reply(bev, state, FAIL_PASV_RESP, "read-pasv-resp");
				break;
			}

			int ip1, ip2, ip3, ip4, port1, port2;
			int numScanned = sscanf(
															beginGrp,
															"(%d,%d,%d,%d,%d,%d)",
															&ip1,
															&ip2,
															&ip3,
															&ip4,
															&port1,
															&port2
			);

			if (numScanned != 6) {
				numScanned = sscanf(
														beginGrp,
														"(%d.%d.%d.%d,%d,%d)",
														&ip1,
														&ip2,
														&ip3,
														&ip4,
														&port1,
														&port2
				);
			}

			if (numScanned != 6) {
				unexpected_reply(bev, state, FAIL_PASV_SCAN, "read-pasv-resp");
				break;
			}

			uint16_t port = (port1 << 8) + port2;
			char ipStr[INET_ADDRSTRLEN + 1];
			snprintf(ipStr, INET_ADDRSTRLEN + 1, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
			log_debug(
								"read-pasv-resp",
								"%s: Found %s and %d",
								state->ip_address_str,
								ipStr,
								port
			);

			state->dataDest.sin_family = AF_INET;
			state->dataDest.sin_port = htons(port);

			if (strcmp(state->ip_address_str, ipStr) != STR_MATCH) {
				state->interestMask |= INTEREST_DIFF_CHANNEL_IPS;
				record_string(state, RECKEY_DIFF_DATA_IP, ipStr);
				log_info(
								"read-pasv-resp",
								"PASV resp directing to ip %s not the control channel's %s. " \
																				"Explicitly using control channel's IP",
								ipStr,
								state->ip_address_str
				);
				inet_pton(
									AF_INET,
									state->ip_address_str,
									&state->dataDest.sin_addr.s_addr
				);
			}
			else {
				inet_pton(AF_INET, ipStr, &state->dataDest.sin_addr.s_addr);
			}

			change_state(state, S_DATA_CONN);
			data_setup_channel(state);
			log_trace(
								"read-pasv-resp",
								"%s: Successfuly read PASV resp and setup data conn",
								state->ip_address_str
			);
			if (state->pasvAction == disconnect_pleasant) {
				state->terminationCode = SUCCESS_EXIT;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"SUCCESS OK"
				);
			}

			state->pasvAction(bev, state);
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-pasv-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void read_retr_done(struct bufferevent *bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-retr-done", "%s", state->ip_address_str);

	// This is a horrible way to do it but it's the cheapest
	if (state->dataBev != NULL) {
		log_trace("read-retr-done", "%s: DELAYED", state->ip_address_str);
		state->delayedCtrlCB = read_retr_done;
		return;
	}

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-retr-done",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_RETR_DONE, NULL, 0, &state->ctrlDBuffer);

			if (state->retrWaitTimer) {
					evtimer_del(state->retrWaitTimer);
					event_free(state->retrWaitTimer);
					state->retrWaitTimer = NULL;
			}


			if (is_reply_code(replyCode, FTP_XFER_OK)) {
				handle_robots_txt(bev, state);
				if (state->retrWaitTimer != NULL) {
					evtimer_del(state->retrWaitTimer);
					event_free(state->retrWaitTimer);
					state->retrWaitTimer = NULL;
				}
				state->pasvPost(bev, state);
			}
			else if (
							(is_reply_code(replyCode, FTP_NO_FILE))
							|| (is_reply_code(replyCode, FTP_NO_FILE_2))
			) {
				log_debug(
									"read-retr-done",
									"%s: No file %s",
									state->ip_address_str,
									DBuffer_start_ptr(&state->itemDBuffer)
				);

				post_retr_wait(0, 0, state);

			}
			else {
				unexpected_reply(bev, state, FAIL_RETR_DONE, "read-retr-done");
			}

			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-retr-done");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void read_list_done(struct bufferevent *bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-list-done", "%s", state->ip_address_str);

	// This is a horrible way to do it but it's the cheapest
	if (state->dataBev != NULL) {
		log_trace("read-list-done", "%s: DELAYED", state->ip_address_str);
		state->delayedCtrlCB = read_list_done;
		return;
	}

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-list-done",
								"%s: Partial response",
								state->ip_address_str
			);
			break;

	case (RESP_FOUND) :
			record_raw_dbuffer(
										state,
										RECKEY_LIST_DONE,
										state->itemDBuffer.startPtr,
										state->itemDBuffer.len,
										&state->ctrlDBuffer
			);

			if (is_reply_code(replyCode, FTP_XFER_OK)) {
				if (state->pasvPost == disconnect_pleasant) {
					state->terminationCode = SUCCESS_EXIT;
					snprintf(
									state->terminationDesc,
									MAX_STATIC_BUFFER_SIZE,
									"%s",
									"SUCCESS OK"
					);
				}

				if (state->savedPortReq[0] == '\0') {
					snprintf(
									state->savedPortReq,
									MAX_STATIC_BUFFER_SIZE,
									"%s",
									DBuffer_start_ptr(&state->itemDBuffer)
					);
				}

				state->pasvPost(bev, state);
			}
			else {
				state->listErrCount++;

				if (state->dataBev) {
					data_close_channel(state->dataBev, state);
				}

				if (
						(gconfig.crawlType == SHORT_CRAWL)
						|| (state->listErrCount == LIST_ERROR_CUTOFF)
				) {
					// Want to absorb some errors on normal for things like permis-denied
					return unexpected_reply(bev, state, FAIL_LIST_DONE, "read-list-done");
				}

				return state->pasvPost(bev, state);
			}
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-list-done");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void read_list_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-list-resp", "%s", state->ip_address_str);
	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-list-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_EXTRA) :
			log_debug(
								"read-list-resp",
								"%s: Extra data in buffer",
								state->ip_address_str
			);
			// no break
	case (RESP_FOUND) :
			record_raw_dbuffer(
										state,
										RECKEY_LIST_RESP,
										state->itemDBuffer.startPtr,
										state->itemDBuffer.len,
										&state->ctrlDBuffer
			);

			if (
					(is_reply_code(replyCode, FTP_LIST_IN))
					|| (is_reply_code(replyCode, FTP_LIST_IN_ALT))
			) {
				help_list_xfer(bev, state);
			}
			else {
				state->listErrCount++;

				if (state->dataBev) {
					data_close_channel(state->dataBev, state);
				}

				if (
						(gconfig.crawlType == SHORT_CRAWL)
						|| (state->listErrCount == LIST_ERROR_CUTOFF)
				) {
					// Want to absorb some errors on normal for things like permis-denied
					return unexpected_reply(bev, state, FAIL_LIST_IN, "read-list-resp");
				}

				return state->pasvPost(bev, state);
			}
			break;
	case (RESP_ERROR) :
			return;
	}
}

void read_feat_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-feat-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-feat-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;

	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_FEAT_RESP, NULL, 0, &state->ctrlDBuffer);
			send_stat(bev, state);
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-feat-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void read_stat_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-stat-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-stat-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;

	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_STAT_RESP, NULL, 0, &state->ctrlDBuffer);
			send_site(bev, state);
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-stat-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void read_site_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-site-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-site-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;

	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_SITE_RESP, NULL, 0, &state->ctrlDBuffer);
			send_port(bev, state);
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-site-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}


void read_help_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-help-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-help-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;

	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_HELP_RESP, NULL, 0, &state->ctrlDBuffer);
			send_feat(bev, state);
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-help-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}
