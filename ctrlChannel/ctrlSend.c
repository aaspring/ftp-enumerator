#include "ctrlSend.h"
#include "ftpCommands.h"
#include "ctrlEnd.h"

#include "../include/logger.h"
#include "../include/magicNumbers.h"
#include "../include/threadStates.h"
#include "../include/ftpEnumerator.h"
#include "../include/ctrlChannel.h"
#include "../include/terminationCodes.h"
#include "../include/interestCodes.h"
#include "../include/dbKeys.h"
#include "../include/outputDB.h"
#include "../include/recorder.h"

inline void change_state(hoststate_t* state, int newState) {
	state->state = newState;
	DBuffer_clear(&state->ctrlDBuffer);
}

void multi_resp_init(hoststate_t* state) {
	memset(state->multiRespFirstReplyCode, 0, REPLY_CODE_SIZE);
	state->multiRespState = MULTI_RESP_READ_ZERO;
}

int socket_is_live(hoststate_t* state) {
	int error = 0;
	socklen_t len = sizeof(error);
	return getsockopt(state->ctrlSocket, SOL_SOCKET, SO_ERROR, &error, &len) == 0;
}

void sendSpigot_cb(evutil_socket_t fd, short triggerEvent, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace(
						"sendSpigot_cb",
						"%s: Sending delayed req : %s",
						state->ip_address_str,
						state->savedReq
	);

	if (socket_is_live(state)) {
		evbuffer_add(
								bufferevent_get_output(state->ctrlBev),
								state->savedReq,
								strlen(state->savedReq)
		);
		change_state(state, state->savedNextState);
	}
	else {
		state->interestMask |= INTEREST_CLOSED_SEND;
		log_debug("sendSpigot-cb", "%s: CLOSED_SEND", state->ip_address_str);
	}

	evtimer_del(state->savedReqTimer);
	event_free(state->savedReqTimer);
	state->savedReqTimer = NULL;
	state->reqThisSecond = 1;
	state->thisSecond = gconfig.second;
	state->numReqSent++;
}

void sendSpigot(
								struct bufferevent* bev,
								hoststate_t* state,
								int nextState,
								char* format,
								...
) {
	bool isClobbering = false;
	if (state->savedReqTimer != NULL) {
		isClobbering = true;
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_1(
														state,
														"Clobbering : |%s|",
														state->savedReq,
														strlen(state->savedReq)
		);
		log_debug(
							"sendSpigot",
							"%s: Clobbering : |%s|",
							state->ip_address_str,
							state->savedReq
		);
	}

	va_list va;
	va_start(va, format);
	vsnprintf(state->savedReq, MAX_STATIC_BUFFER_SIZE, format, va);
	va_end(va);

	log_trace("sendSpigot", "%s: %s", state->ip_address_str, state->savedReq);

	if (isClobbering) {
		record_misc_partial_raw_1(
															state,
															"Clobbered with : |%s|",
															state->savedReq,
															strlen(state->savedReq)
		);
		log_debug(
							"sendSpigot",
							"%s: Clobbered with : |%s|",
							state->ip_address_str,
							state->savedReq
		);
		state->savedNextState = nextState;
		// Already a timer in-flight, don't do anything
		return;
	}

	if (state->numReqSent % REQ_LIMIT_NOTIFY_INTERVAL == 0) {
		log_debug(
						"sendSpigot",
						"%s: Req limit at %d of %d",
						state->ip_address_str,
						state->numReqSent,
						gconfig.maxReqPerIp
		);
	}

	if (state->numReqSent > gconfig.maxReqPerIp - 1) {
			log_debug(
								"sendSpigot",
								"%s: Hit the request limit of %d",
								state->ip_address_str,
								gconfig.maxReqPerIp
			);
			state->terminationCode = FAIL_MAX_REQ_REACHED;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"Request limit at : %s",
							state->savedReq
			);
			state->reqLimitReached = true;
			return;
	}

	if (gconfig.second > state->thisSecond) {
		state->thisSecond = gconfig.second;
		state->reqThisSecond = 0;
	}

	if (state->reqThisSecond >= gconfig.maxReqPerSec) {
		state->savedReqTimer = evtimer_new(gconfig.ev_base, sendSpigot_cb, state);
		state->savedNextState = nextState;
		evtimer_add(state->savedReqTimer, &one_second);
		log_trace(
							"sendSpigot",
							"%s: Delayed req : %s",
							state->ip_address_str,
							state->savedReq
		);
	}
	else {
		if (socket_is_live(state)) {
			evbuffer_add(
									bufferevent_get_output(bev),
									state->savedReq,
									strlen(state->savedReq)
			);
			change_state(state, nextState);
		}
		else {
			state->interestMask |= INTEREST_CLOSED_SEND;
			log_debug("sendSpigot-cb", "%s: CLOSED_SEND", state->ip_address_str);
		}
		state->reqThisSecond++;
		state->numReqSent++;
	}
}

void send_type(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-type", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_TYPE_SENT, "%s\r\n", CMD_TYPE);
	log_trace("send-type", "%s: Sent TYPE", state->ip_address_str);
}

void send_retr(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-retr", "%s", state->ip_address_str);
	sendSpigot(
						bev,
						state,
						S_RETR_SENT,
						"%s %s\r\n",
						CMD_RETR,
						state->itemDBuffer.startPtr
	);
	state->xferType = XFER_FILE;
	data_connect(state);
	log_trace(
						"send-retr",
						"%s: Sent RETR : |%s|",
						state->ip_address_str,
						state->itemDBuffer.startPtr
	);

}

void send_pass(struct bufferevent* bev, hoststate_t* state){
	log_trace("send-pass", "%s", state->ip_address_str);
	multi_resp_init(state);
	sendSpigot(bev, state, S_PASS_SENT, "%s %s\r\n", CMD_PASS, PASSWORD);
	log_trace("send-pass", "%s: Sent password", state->ip_address_str);
}

void send_feat(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-feat", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_FEAT_SENT, "%s\r\n", CMD_FEAT);
	log_trace(
						"send-feat",
						"%s: Successfully sent the FEAT command",
						state->ip_address_str
	);
}

void send_stat(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-stat", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_STAT_SENT, "%s\r\n", CMD_STAT);
	log_trace("send-stat", "%s: Sent STAT", state->ip_address_str);
}

void send_pasv(struct bufferevent *bev, hoststate_t* state) {
	log_trace("send-pasv", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_PASV_SENT, "%s\r\n", CMD_PASV);
	log_trace("send-pasv", "%s: Successfully sent PASV", state->ip_address_str);
}


void send_list(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-list", "%s", state->ip_address_str);
	sendSpigot(
						bev,
						state,
						S_LIST_SENT,
						"%s %s\r\n",
						CMD_LIST,
						state->itemDBuffer.startPtr
	);
	state->xferType = XFER_LIST;
	data_connect(state);
	log_trace(
						"send-list",
						"%s: Successfully sent LIST %s",
						state->ip_address_str,
						state->itemDBuffer.startPtr
	);
}

void send_help(struct bufferevent* bev, hoststate_t* state) {
	log_trace("send-help", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_HELP_SENT, "%s\r\n", CMD_HELP);
	log_trace("send-help", "%s: Sent HELP", state->ip_address_str);
}


void send_pwd(struct bufferevent *bev, hoststate_t* state) {
	log_trace("send-pwd", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_PWD_SENT, "%s\r\n", CMD_PWD);
	log_trace("send-pwd", "%s: Sent PWD", state->ip_address_str);
}

void send_syst(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-syst", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_SYST_SENT, "%s\r\n", CMD_SYST);
	log_trace("send-syst", "%s: Sent SYST", state->ip_address_str);
}

void send_quit(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-quit", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_QUIT_SENT, "%s\r\n", CMD_QUIT);
	log_trace("send-quit", "%s: Sent QUIT", state->ip_address_str);
}

void send_user(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-user", "%s", state->ip_address_str);
	multi_resp_init(state);
	sendSpigot(bev, state, S_USER_SENT, "%s %s\r\n", CMD_USER, USERNAME);
	log_trace("send-user", "%s: Sent USER", state->ip_address_str);
}

void send_site(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("send-site", "%s", state->ip_address_str);
	sendSpigot(bev, state, S_SITE_SENT, "%s\r\n", CMD_SITE);
	log_trace("send-site", "%s: Sent USER", state->ip_address_str);
}
