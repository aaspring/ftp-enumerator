#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "ftpReturnCodes.h"
#include "parseResp.h"
#include "ctrlEnd.h"
#include "ctrlSend.h"
#include "ftpCommands.h"
#include "ctrlRead.h"
#include "ctrlSecurity.h"

#include "../include/threadStates.h"
#include "../include/magicNumbers.h"
#include "../include/logger.h"
#include "../include/interestCodes.h"
#include "../include/terminationCodes.h"
#include "../include/ftpEnumerator.h"
#include "../include/outputDB.h"
#include "../include/dbKeys.h"
#include "../include/ctrlChannel.h"
#include "../include/recorder.h"
#include "../include/recordKeys.h"

void send_port_list(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-port-list", "%s", state->ip_address_str);
	sendSpigot(
						bev,
						state,
						S_PORT_LIST_SENT,
						"%s %s\r\n",
						CMD_LIST,
						state->savedPortReq
	);
	state->xferType = XFER_LIST;
}

void read_port_resp(struct bufferevent* bev, void* args) {
	char replyCode[REPLY_CODE_SIZE];
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-port-resp", "%s", state->ip_address_str);

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-port-resp",
								"%s: Partial response found",
								state->ip_address_str
			);
			break;

	case (RESP_FOUND) :
			record_raw_dbuffer(state, RECKEY_PORT_RESP, NULL, 0, &state->ctrlDBuffer);

			if (
					(is_reply_code(replyCode, FTP_BAD_PORT_DENY_1))
					|| (is_reply_code(replyCode, FTP_BAD_PORT_DENY_2))
			) {
				log_info(
								"read-port-resp",
								"%s: Destination refused to PORT bounce",
								state->ip_address_str
				);
				if (state->ctrlUsingSec) {
					state->terminationCode = SUCCESS_EXIT;
					snprintf(
									state->terminationDesc,
									MAX_STATIC_BUFFER_SIZE,
									"%s",
									"SUCCESS OK"
					);
					disconnect_pleasant(bev, state);
				}
				else {
					return setup_ending_optional_sec_check(bev, state);
				}
			}
			else if (is_reply_code(replyCode, FTP_BAD_PORT_ALLOW)) {
				log_info(
								"read-port-resp",
								"%s: Destination allowed PORT bounce setup",
								state->ip_address_str
				);
				state->interestMask |= INTEREST_PORT_SETUP_ALLOWED;
				send_port_list(bev, state);
			}
			else {
				unexpected_reply(bev, state, FAIL_UNK_BOUNCE_RESP, "read-port-resp");
			}
			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-port-resp");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void send_port(struct bufferevent *bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-port", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_PORT_SENT, "%s %s\r\n", CMD_PORT, BAD_PORT_ARG);
	log_trace(
						"send-port",
						"%s: Sent malicious port command",
						state->ip_address_str
	);
}

void read_port_list_done(struct bufferevent *bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-port-list-done", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-port-list-done",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_FOUND) :
			record_raw_dbuffer(
										state,
										RECKEY_BAD_PORT_LIST_DONE,
										NULL,
										0,
										&state->ctrlDBuffer
			);

			if (is_reply_code(replyCode, FTP_XFER_OK)) {
				log_info(
								"read-port-list-done",
								"%s: Allowed PORT bounce LIST",
								state->ip_address_str
				);
				state->interestMask |= INTEREST_PORT_LIST_ALLOWED;
			}

			if (state->ctrlUsingSec) {
				state->terminationCode = SUCCESS_EXIT;
				snprintf(
								state->terminationDesc,
								MAX_STATIC_BUFFER_SIZE,
								"%s",
								"SUCCESS OK"
				);
				disconnect_pleasant(bev, state);
			}
			else {
				setup_ending_optional_sec_check(bev, state);
			}

			break;

	case (RESP_EXTRA) :
			handle_extra_resp_error(bev, state, "read-port-list-done");
			break;

	case (RESP_ERROR) :
			return;
	}
}

void help_port_list_xfer(struct bufferevent* bev, hoststate_t* state) {
	log_trace("help-list-xfer", "%s", state->ip_address_str);

	change_state(state, S_PORT_LIST_XFER);

	if (evbuffer_get_length(bufferevent_get_input(bev)) != 0) {
		// This is to solve the issue of replies building up in the buffer when you
		// LIST an empty directory.
		log_debug(
						"help-port-list-xfer",
						"%s: LIST-done already in buffer",
						state->ip_address_str
		);
		read_port_list_done(bev, state);
	}
}

void read_port_list_resp(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-port-list-resp", "%s", state->ip_address_str);

	char replyCode[REPLY_CODE_SIZE];

	RespResult_e res = parse_response(bev, state, replyCode);
	switch (res) {
	case (RESP_INCOMPLETE) :
			log_debug(
								"read-port-list-resp",
								"%s: Partial response",
								state->ip_address_str
			);
			break;
	case (RESP_EXTRA) :
			log_debug(
								"read-port-list-resp",
								"%s: Extra data in buffer",
								state->ip_address_str
			);
			// no break
	case (RESP_FOUND) :
			record_raw_dbuffer(
										state,
										RECKEY_BAD_PORT_LIST_RESP,
										state->itemDBuffer.startPtr,
										state->itemDBuffer.len,
										&state->ctrlDBuffer
			);

			if (
					(is_reply_code(replyCode, FTP_LIST_IN))
					|| (is_reply_code(replyCode, FTP_LIST_IN_ALT))
			) {
				help_port_list_xfer(bev, state);
			}
			else {
				unexpected_reply(
												bev,
												state,
												FAIL_BAD_PORT_LIST_RESP,
												"read-port-list-resp"
				);
			}
			break;
	case (RESP_ERROR) :
			return;
	}
}
