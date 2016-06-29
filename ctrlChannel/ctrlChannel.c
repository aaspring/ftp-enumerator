#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <db.h>

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ctrlSend.h"
#include "ctrlRead.h"
#include "ftpReturnCodes.h"
#include "ftpCommands.h"
#include "ctrlEnd.h"
#include "parseResp.h"
#include "badPort.h"
#include "ctrlSecurity.h"
#include "ctrlDeciders.h"
#include "ctrlSecurity.h"

#include "../include/logger.h"
#include "../include/ctrlChannel.h"
#include "../include/terminationCodes.h"
#include "../include/interestCodes.h"
#include "../include/magicNumbers.h"
#include "../include/threadStates.h"
#include "../include/ftpEnumerator.h"
#include "../include/dataChannel.h"
#include "../include/DBuffer.h"
#include "../include/outputDB.h"
#include "../include/recorder.h"

void walk_dir(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("walk-dir", "%s", state->ip_address_str);
	bool crawlableDir = false;

	char dirStr[MAX_STATIC_BUFFER_SIZE];
	str_dequeue(&state->dirQueue, dirStr, MAX_STATIC_BUFFER_SIZE);

	DBuffer_clear(&state->itemDBuffer);

	if (state->lieCount > NUMBER_ALLOWED_LIES) {
		state->interestMask |= INTEREST_LIST_IS_LYING;
		log_debug("walk-dir", "%s: Caught LIST in a lie", state->ip_address_str);
		str_queue_destroy(&state->dirQueue);
		str_queue_init(&state->dirQueue);
		return send_help(bev, state);
	}

	while (dirStr[0] != '\0') {
		if (access_is_allowed(&state->robotObj, dirStr)) {
			crawlableDir = true;
			DBuffer_append(&state->itemDBuffer, dirStr);
			break;
		}
		else {
			log_debug(
								"walk-dir",
								"%s: Skipping %s b/c of robots.txt",
								state->ip_address_str,
								dirStr
			);
			record_raw_buffer(
												state,
												RECKEY_ROBOT_NOT_ALLOW,
												NULL,
												0,
												dirStr,
												strlen(dirStr)
			);
			str_dequeue(&state->dirQueue, dirStr, MAX_STATIC_BUFFER_SIZE);
		}
	}

	if (crawlableDir) {
		send_pasv(bev, state);
		return;
	}
	else {
		log_info(
						"walk-dir",
						"%s: Enumeration complete, gathering more data",
						state->ip_address_str
		);
		if (gconfig.crawlType == NORMAL_CRAWL) {
			send_help(bev, state);
		}
		else {
			state->terminationCode = SUCCESS_EXIT;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"SUCCESS OK"
			);
			disconnect_pleasant(bev, state);
		}
		return;
	}
}

void handle_robots_txt(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("handle-robots-txt", "%s", state->ip_address_str);
	record_raw_dbuffer(state, RECKEY_ROBOT_DATA, NULL, 0, &state->dataDBuffer);
	if (gconfig.crawlType == NORMAL_CRAWL) {
		robot_parser_init(&state->robotObj, state->dataDBuffer.startPtr, USER_AGENT);
		record_raw_buffer(
											state,
											RECKEY_ROBOT_GROUP,
											NULL,
											0,
											DBuffer_start_ptr(&state->robotObj.groupDBuffer),
											state->robotObj.groupDBuffer.len
		);
	}

	DBuffer_clear(&state->dataDBuffer);
}

void short_scan_ending(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("short-scan-ending", "%s", state->ip_address_str);
	if (state->robotPresence == ROBOTS_NO) {
		send_syst(bev, state);
	}
	else {
		state->terminationCode = SUCCESS_EXIT;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"SUCCESS OK"
		);
		disconnect_pleasant(bev, state);
	}
}

void fetch_robots_txt(struct bufferevent* bev, hoststate_t* state) {
	log_trace("fetch-robots-txt", "%s", state->ip_address_str);
	DBuffer_append(&state->itemDBuffer, ROBOTS_TXT);

	state->xferType = XFER_FILE;
	state->pasvAction = send_retr;
	if (gconfig.crawlType == NORMAL_CRAWL) {
		state->pasvPost = send_syst;
	}
	else if (gconfig.crawlType == SHORT_CRAWL) {
		state->pasvPost = short_scan_ending;
	}
	else {
		log_fatal(
							"fetch-robots-txt",
							"%s: Fetching when shouldn't be",
							state->ip_address_str
		);
	}

	send_pasv(bev, state);
}

void walk_dir_init(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("walk-dir-init", "%s", state->ip_address_str);
	state->pasvAction = send_list;
	state->pasvPost = walk_dir;
	walk_dir(bev, state);
}


// Create and return a non-blocking eventlib ready socket
int create_nb_socket() {
	log_trace("create_nb_socket", "");
	int sock = -1;
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		log_fatal(
							"create-nb-socket",
							"unable to create socket%s",
							"\n\t\tCheck 'ulimit -n' and 'sysctl fs.file-max'"
		);
	}
	evutil_make_socket_nonblocking(sock);
	struct sockaddr_in localAddr;
	memset(&localAddr, 0, sizeof(struct sockaddr_in));
	localAddr.sin_family = AF_INET;
	localAddr.sin_port = 0;
	inet_pton(AF_INET, gconfig.sourceIp, &localAddr.sin_addr);
	int rc = bind(sock, (struct sockaddr*)&localAddr, sizeof(struct sockaddr_in));
	if (rc < 0) {
		log_fatal("create-nb-socket", "unable to bind to local address");
	}
	return sock;
}

void ctrl_event_init_state(
													struct bufferevent *bev,
													short triggerEvent,
													hoststate_t* state
) {
	log_trace("ctrl-event-init-state", "%s", state->ip_address_str);

	if (triggerEvent & BEV_EVENT_CONNECTED) {
		log_info("ctrl-event-init-state", "%s is alive", state->ip_address_str);
		record_bool(state, RECKEY_ALIVE,true);
		change_state(state, S_CONNECTED);
		if (state->delayedCtrlCB != NULL) {
			bufferevent_data_cb delayed = state->delayedCtrlCB;
			state->delayedCtrlCB = NULL;
			log_debug(
								"ctrl-event-init-state",
								"%s: Executing Delayed callback",
								state->ip_address_str
			);
			delayed(bev, state);
		}
	}
	else if (triggerEvent & BEV_EVENT_ERROR) {
		record_bool(state, RECKEY_ALIVE, false);
		gconfig.countRefused++;
		state->terminationCode = FAIL_DEST_REFUSED;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Remote replied to SYN with RST (refused)"
		);
		log_info(
						"ctrl-event-init-state",
						"%s: %s",
						state->ip_address_str,
						state->terminationDesc
		);
		disconnect_abrupt(bev, state);
	}
	else if (
					(triggerEvent & BEV_EVENT_TIMEOUT)
					&& (triggerEvent & BEV_EVENT_READING)
	){
		gconfig.countDead++;
		state->terminationCode = FAIL_DEST_NO_RESPONSE;
		record_bool(state, RECKEY_ALIVE, false);
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Destination did not respond to SYN (dead)"
		);
		log_info(
						"ctrl-event-init-state",
						"%s: %s",
						state->ip_address_str,
						state->terminationDesc
		);
		disconnect_abrupt(bev, state);
	}
	else {
		state->terminationCode = FAIL_UNK_INIT_EVENT;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"Unknown event (0x%x) while in initial state",
						triggerEvent
		);
		log_error(
						"ctrl-event-init-state",
						"%s: %s",
						state->ip_address_str,
						state->terminationDesc
		);

		disconnect_abrupt(bev, state);
	}

	if (state->hostIsFinished) {
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
	}
}

void ctrl_event_cb(
													struct bufferevent *bev,
													short triggerEvent,
													void* args
) {
	char sBuffer[MAX_STATIC_BUFFER_SIZE];
	hoststate_t* state = (hoststate_t*) args;
	log_trace("ctrl-event-cb", state->ip_address_str);

	if (state->state == S_INITIAL) {
		return ctrl_event_init_state(bev, triggerEvent, state);
	}

	CtrlEventID_e ctrlEventID = id_ctrl_event(state, triggerEvent);
	log_debug(
						"ctrl-event-cb",
						"%s: ctrlEventID : 0x%x",
						state->ip_address_str,
						ctrlEventID
	);

	switch (ctrlEventID) {

	case (Event_Timeout_Banner) :
			log_error("ctrl-event-cb", "%s: BANNER timeout", state->ip_address_str);
			gconfig.countTimeouts++;
			state->terminationCode = FAIL_TIMEOUT_BANNER;
			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"BANNER timeout (0x%x) -- didn't send a complete banner (not FTP)",
						triggerEvent
			);
			log_error(
								"ctrl-event-cb",
								"%s: %s",
								state->ip_address_str,
								state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Timeout_Read) :
			log_error("ctrl-event-cb", "%s: READ timeout", state->ip_address_str);
			gconfig.countTimeouts++;
			state->terminationCode = FAIL_TIMEOUT;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"READ timeout (0x%x) -- while in state '%s' (%d)",
							triggerEvent,
							thread_state_strings[state->state],
							state->state
			);
			log_error(
								"ctrl-event-cb",
								"%s: %s",
								state->ip_address_str,
								state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Timeout_TLS) :
			log_error("ctrl-event-cb", "%s: TLS timeout", state->ip_address_str);
			state->terminationCode = FAIL_TIMEOUT_TLS;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"TLS timeout (0x%x) -- while conducting TLS handshake",
							triggerEvent
			);
			log_error(
							"ctrl-event-cb",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Timeout_Pass) :
			log_error("ctrl-event-cb", "%s: PASS timeout", state->ip_address_str);
			gconfig.countTimeouts++;
			state->terminationCode = FAIL_TIMEOUT_PASS;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"PASS timeout (0x%x)",
							triggerEvent
			);
			log_error(
								"ctrl-event-cb",
								"%s: %s",
								state->ip_address_str,
								state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Timeout_Unknown) :
			state->terminationCode = FAIL_TIMEOUT_UNK;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"UNKNOWN timeout (0x%x) -- while in state '%s' (%d)",
							triggerEvent,
							thread_state_strings[state->state],
							state->state
			);
			log_error(
							"ctrl-event-cb",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Connect_Security) :
			log_info(
							"ctrl-event",
							"%s: TLS connection active",
							state->ip_address_str
			);
			log_ssl_data(bev, state);
			send_pbsz(bev, state);
			break;

	case (Event_Connect_Unknown) :
			snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"UNKNOWN connection event (0x%x) -- while in state '%s' (%d)",
						triggerEvent,
						thread_state_strings[state->state],
						state->state
			);
			log_error(
								"ctrl-event-cb",
								"%s: %s",
								state->ip_address_str,
								state->terminationDesc
			);
			state->terminationCode = FAIL_BAD_CONNECT;
			disconnect_abrupt(bev, state);
			break;

	case (Event_EOF_Primary_Sec) :
	case (Event_Error_Primary_Sec) :
			ERR_error_string_n(
												bufferevent_get_openssl_error(bev),
												sBuffer,
												MAX_STATIC_BUFFER_SIZE
			);
			log_warn(
							"ctrl-event-cb",
							"%s: Primary OpenSSL error : %s",
							state->ip_address_str,
							sBuffer
			);
			record_string_sprintf(
														state,
														RECKEY_OPENSSL_ERROR,
														"%s (0x%x) : %s",
														"Primary Negotiation error",
														triggerEvent,
														sBuffer
			);

			if (strcmp(sBuffer, "error:00000000:lib(0):func(0):reason(0)") == STR_MATCH) {
				log_debug(
									"ctrl-event-cb",
									"%s: Hit the blank OpenSSL error",
									state->ip_address_str
				);
				state->terminationCode = FAIL_BLANK_SSL_ERROR;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"Blank error in OpenSSL");
				disconnect_abrupt(bev, state);
				break;
			}

			log_info(
							"ctrl-event-cb",
							"%s: SSLv23 was refused. Trying SSLv3",
							state->ip_address_str
			);

			state->interestMask |= INTEREST_SSLV23_ERROR;
			SSL_set_shutdown(state->ctrlSec, SSL_RECEIVED_SHUTDOWN);
			SSL_shutdown(state->ctrlSec);
			state->ctrlSec = NULL;

			bufferevent_free(state->ctrlBev);

			state->state = S_INITIAL;
			state->ctrlUsingSec = false;

			DBuffer_clear(&state->itemDBuffer);
			DBuffer_clear(&state->ctrlDBuffer);
			DBuffer_clear(&state->dataDBuffer);

			ctrl_connect(state);
			break;

	case (Event_EOF_Secondary_Sec) :
	case (Event_Error_Secondary_Sec) :
			ERR_error_string_n(
												bufferevent_get_openssl_error(bev),
												sBuffer,
												MAX_STATIC_BUFFER_SIZE
			);
			log_warn(
							"ctrl-event-cb",
							"%s: Secondary OpenSSL error : %s",
							state->ip_address_str,
							sBuffer
			);
			record_string_sprintf(
														state,
														RECKEY_OPENSSL_ERROR,
														"%s (0x%x) : %s",
														"Secondary Negotiation error",
														triggerEvent,
														sBuffer
			);

			log_info(
							"ctrl-event-cb",
							"%s: SSLv3 was refused. Ending channel",
							state->ip_address_str
			);
			state->terminationCode = FAIL_SECONDARY_NEGOTIATION;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Failed to negotiate FTPS with SSLv3"
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Error_Unknown) :
			state->terminationCode = FAIL_ERROR_EVENT;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"Error event (0x%x) while in state '%s' (%d) : %s",
							triggerEvent,
							thread_state_strings[state->state],
							state->state,
							evutil_socket_error_to_string(
																						evutil_socket_geterror(
																															state->ctrlSocket
																						)
							)
			);
			log_warn(
							"ctrl-event-cb",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_EOF_User) :
			log_debug(
								"ctrl-event-cb",
								"%s: Disconnected after USER (0x%x). Poss early security req.",
								state->ip_address_str,
								triggerEvent
			);
			state->terminationCode = FAIL_DISCONNECT_USER;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Disconnect after sending USER"
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_EOF_User_Sec_Req) :
			log_debug(
								"ctrl-event-cb",
								"%s: Remote requires pre-USER security (0x%x), retrying",
								state->ip_address_str,
								triggerEvent
			);
			record_raw_dbuffer(
												state,
												RECKEY_USER_RESP,
												NULL,
												0,
												&state->ctrlDBuffer
			);
			handle_early_sec_req(bev, state);
			break;

	case (Event_Error_Banner) :
	case (Event_EOF_Banner) :
			state->terminationCode = FAIL_DISCONNECT_BANNER;
			snprintf(
					state->terminationDesc,
					MAX_STATIC_BUFFER_SIZE,
					"BANNER disconnect (0x%x) -- Allowed TCP connection, but disconnected before banner complete",
					triggerEvent
			);
			log_error(
								"ctrl-event-cb",
								"%s: %s",
								state->ip_address_str,
								state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Error_Port) :
			state->terminationCode = FAIL_DISCONNECT_PORT;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"PORT disconnect (0x%x) -- Disconnected after PORT sent",
							triggerEvent
			);
			log_error(
								"ctrl-event-cb",
								"%s: %s",
								state->ip_address_str,
								state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_EOF_Rude) :
			disconnect_rude(bev, state);
			break;

	case (Event_Error_Pass) :
	case (Event_EOF_Pass) :
			state->terminationCode = FAIL_DISCONNECT_PASS;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"PASS disconnect (0x%x) -- after PASS sent ['%s' (%d)]",
							triggerEvent,
							thread_state_strings[state->state],
							state->state
			);
			log_error(
							"ctrl-event-cb",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_EOF_Unknown) :
			state->terminationCode = FAIL_DISCONNECT_OTHER;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"UNKNOWN disconnect (0x%x) -- while in state '%s' (%d)",
							triggerEvent,
							thread_state_strings[state->state],
							state->state
			);
			log_error(
							"ctrl-event-cb",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;

	case (Event_Unknown) :
	default :
			state->terminationCode = FAIL_UNK_EVENT;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"UNKNOWN EVENT (0x%x) while in state %d",
							triggerEvent,
							state->state
			);
			log_error(
							"ctrl-event-cb",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
			);
			disconnect_abrupt(bev, state);
			break;
	}

	if (state->hostIsFinished) {
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
	}
}

// The read event dispatcher for the control thread
void ctrl_read_cb(struct bufferevent *bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace(
						"ctrl-read-cb",
						"%s: read cb in state '%s'",
						state->ip_address_str,
						thread_state_strings[state->state]
	);
	char strBuffer[MAX_STATIC_BUFFER_SIZE];
	int numBytes = 0;
	struct evbuffer *input = bufferevent_get_input(bev);

	switch (state->state) {
		case(S_INITIAL) :
			assert(state->delayedCtrlCB == NULL);
			state->delayedCtrlCB = ctrl_read_cb;
			log_error(
								"ctrl-read-cb",
								"%s: In initial read cb DELAYED",
								state->ip_address_str
			);
			break;
		case (S_CONNECTED) :
			read_banner(bev, args);
			break;
		case (S_USER_SENT) :
			read_user_resp(bev, args);
			break;
		case (S_PASS_SENT) :
			read_pass_resp(bev, args);
			break;
		case (S_PORT_SENT) :
			read_port_resp(bev, args);
			break;
		case (S_PWD_SENT) :
			read_pwd_resp(bev, args);
			break;
		case (S_PASV_SENT) :
			read_pasv_resp(bev, args);
			break;
		case (S_LIST_SENT) :
			read_list_resp(bev, args);
			break;
		case (S_LIST_XFER) :
			read_list_done(bev, args);
			break;
		case (S_FEAT_SENT) :
			read_feat_resp(bev, args);
			break;
		case (S_QUIT_SENT) :
			read_quit_resp(bev, args);
			break;
		case (S_HELP_SENT) :
			read_help_resp(bev, args);
			break;
		case (S_SYST_SENT) :
			read_syst_resp(bev, args);
			break;
		case (S_STAT_SENT) :
			read_stat_resp(bev, args);
			break;
		case (S_AUTH_TLS_SENT) :
			read_auth_tls_resp(bev, args);
			break;
		case (S_PBSZ_INIT_SENT) :
			read_pbsz_resp(bev, args);
			break;
		case (S_PROT_SENT) :
			read_prot_resp(bev, args);
			break;
		case (S_TYPE_SENT) :
			read_type_resp(bev, args);
			break;
		case (S_RETR_SENT) :
			read_retr_resp(bev, args);
			break;
		case (S_AUTH_SSL_SENT) :
			read_auth_ssl_resp(bev, args);
			break;
		case (S_RETR_XFER) :
			read_retr_done(bev, args);
			break;
		case (S_PORT_LIST_SENT) :
			read_port_list_resp(bev, args);
			break;
		case (S_PORT_LIST_XFER) :
			read_port_list_done(bev, args);
			break;
		case (S_SITE_SENT) :
			read_site_resp(bev, args);
			break;
		case (S_SECURITY_CONNECTING) :
			read_security_connecting(bev, args);
			break;
		case (S_RESET_WAIT) :
			read_reset_wait(bev, args);
			break;
		default :
			numBytes = evbuffer_remove(input, strBuffer, MAX_STATIC_BUFFER_SIZE);
			log_error(
								"ctrl-read-cb",
								"%s: Unknown read state :\n\tIP :  %s\n\tState : %d\n\tRead : %s",
								state->ip_address_str,
								state->ip_address_str,
								state->state,
								strBuffer
			);
			state->terminationCode = FAIL_REALLY_BAD_ERROR;
			snprintf(
				state->terminationDesc,
				MAX_STATIC_BUFFER_SIZE,
				"Unknown read state -- %d",
				state->state
			);
			record_misc_partial_raw_1(
																state,
																"Response recieved without a waiting state : %s",
																strBuffer,
																numBytes
			);
			return disconnect_abrupt(bev, state);
	}

	if (state->hostIsFinished) {
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
		return;
	}

	if (state->reqLimitReached) {
		disconnect_abrupt(bev, state);
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
		return;
	}
}

void ctrl_connect(hoststate_t* state) {
	int sock = create_nb_socket();
	struct bufferevent *bev = bufferevent_socket_new(
																									gconfig.ev_base,
																									sock,
																									BEV_OPT_CLOSE_ON_FREE
	);
	if (bev == NULL) {
		log_fatal(
							"ftp-ctrl-connect",
							"unable to create socket buffer event : |%s|",
							evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())
							);
	}
	struct sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(gconfig.port);
	destAddr.sin_addr.s_addr = state->ip_address;
	inet_ntop(
						AF_INET,
						&destAddr.sin_addr,
						state->ip_address_str,
						INET_ADDRSTRLEN
	);
	record_string(state, RECKEY_IP, state->ip_address_str);
	log_info("ftp-ctrl-connect", "%s: Connecting", state->ip_address_str);
	bufferevent_setcb(bev, ctrl_read_cb, NULL, ctrl_event_cb, state);

	int rc = bufferevent_socket_connect(
																		bev,
																		(struct sockaddr*) &destAddr,
																		sizeof(struct sockaddr_in)
	);
	if (rc < 0) {
		log_warn(
						"ftp-ctrl-connect",
						"%s: Failed attempted connection : %s",
						inet_ntoa(destAddr.sin_addr),
						evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())
		);
		bufferevent_free(bev);
		return;
	}
	bufferevent_set_timeouts(bev, &gconfig.readTimer, NULL);

	state->ctrlSocket = sock;
	state->ctrlBev = bev;
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

void ctrl_init(hoststate_t *state) {
	state->ctrlUsingSec = false;
	state->robotPresence = ROBOTS_UNK;
	state->xferType = XFER_UNK;
	state->startingSecond = gconfig.second;
	DBuffer_init(&state->ctrlDBuffer, DBUFFER_INIT_LARGE);
	DBuffer_init(&state->dataDBuffer, DBUFFER_INIT_LARGE);
	DBuffer_init(&state->itemDBuffer, DBUFFER_INIT_SMALL);
	state->numReqSent = 0;
	gconfig.open_connections++;
	state->savedReqTimer = NULL;
	state->lastState = S_UNSET_STATE;
	state->hostIsFinished = false;
	recorder_init(state);

	ctrl_connect(state);
}

