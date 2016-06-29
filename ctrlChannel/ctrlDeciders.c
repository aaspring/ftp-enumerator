#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <strings.h>

#include "ctrlDeciders.h"
#include "impOracles.h"
#include "ftpReturnCodes.h"
#include "parseResp.h"

#include "../include/ftpEnumerator.h"
#include "../include/logger.h"
#include "../include/interestCodes.h"

UserRespID_e id_user_resp_5XX(hoststate_t* state, char* replyCode) {
	if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_VIRTUAL_SITES_2)) {
		// 530 ~~~~~~~~~~~~~
		return USER_Virtual_Sites;
	}
	else if (is_reply_code_strict(replyCode, FTP_USER_RESP_530)) {
		// 530
		if (
				(strcasestr(state->ctrlDBuffer.startPtr, ORACLE_USER_SEC_REQ_1))
				|| (strcasestr(state->ctrlDBuffer.startPtr, ORACLE_USER_SEC_REQ_2))
		) {
			return USER_TLS_530;
		}
		else {
			return USER_Reject_530;
		}
	}
	else if (
					(is_reply_code_strict(replyCode, FTP_SECURITY_REQ))
					|| (is_reply_code_strict(replyCode, FTP_SECURITY_REQ_ALT))
					|| (is_reply_code_strict(replyCode, FTP_SECURITY_REQ_ALT_3))
	) {
		// 534
		// 503
		// 550
		return USER_TLS_Other;
	}
	else if (is_reply_code_strict(replyCode, FTP_USER_REJECT)) {
		// 532
		return USER_Reject_Explicit;
	}
	else if (is_reply_code_strict(replyCode, FTP_ACCT_ERR)) {
		// 500
		return USER_Reject_Explicit;
	}
	else {
		// 5XX
		return USER_TLS_Optimistic;
	}
}

UserRespID_e id_user_resp_3XX(hoststate_t* state, char* replyCode) {
	if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_VIRTUAL_SITES_1)) {
		// 331 ~~~~~~~~~~~
		return USER_Virtual_Sites;
	}
	else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_USER_SEC_REQ_3)) {
		// 331 ~~~~~~~~~~~
		return USER_TLS_Other;
	}
	else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_PERMISSION_DENIED)) {
		// 331 ~~~~~~~~
		return USER_Reject_Explicit;
	}
	else if (is_reply_code_strict(replyCode, FTP_NEED_USER_ACCT)) {
		// 332
		return USER_Reject_Explicit;
	}
	else {
		// 3XX
		return USER_Accept_User;
	}
}

UserRespID_e id_user_resp_4XX(hoststate_t* state, char* replyCode) {
	if (strcasestr(state->ctrlDBuffer.startPtr, ORACLE_EARLY_SEC_REQ)) {
		// ???'cleartext'???
		return USER_Early_Sec_Req;
	}
	else if (strcasestr(state->ctrlDBuffer.startPtr, ORACLE_BAD_PERMISSIONS)) {
		// ???'Can't change directory to'???
		return USER_Bad_Config;
	}
	else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_UNABLE_SEC_ANON)) {
		// 421 Unable to set up secure anonymous FTP
		return USER_Reject_Explicit;
	}
	else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_USER_RESP_NO_ANON)) {
		// 421 Anonymous logins are not allowed here.
		return USER_Reject_Explicit;
	}
	else {
		// 4XX
		return USER_Unexpected;
	}
}

UserRespID_e id_user_resp_double(hoststate_t* state) {
	struct evbuffer* inputBuffer = bufferevent_get_input(state->ctrlBev);
	char* ctrlBufferPtr = DBuffer_start_ptr(&state->ctrlDBuffer);

	log_error(
						"read-user-resp",
						"%s: Extra data in buffer %s <+> %.*s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->ctrlDBuffer),
						evbuffer_get_length(bufferevent_get_input(state->ctrlBev)),
						evbuffer_pullup(inputBuffer, -1)
	);

	if (
			(strcmp(ctrlBufferPtr, ORACLE_DOUBLE_USER_RESP_1A) == STR_MATCH)
			&& (evbuffer_matches(inputBuffer, ORACLE_DOUBLE_USER_RESP_1B))
	) {
		return USER_Reject_Double_1;
	}
	else {
		return USER_Extra;
	}
}

UserRespID_e id_user_resp(hoststate_t* state, char* replyCode, RespResult_e res) {
	if (res == RESP_EXTRA) {
		return id_user_resp_double(state);
	}

	switch (replyCode[0]) {
	case ('5') :
			return id_user_resp_5XX(state, replyCode);

	case ('4') :
			return id_user_resp_4XX(state, replyCode);

	case ('3') :
			return id_user_resp_3XX(state, replyCode);

	case ('2') :
			return USER_No_Pass;

	default :
		return USER_Unexpected;
	}
}

CtrlEventID_e id_ctrl_event_timeout(hoststate_t* state, short triggerEvent) {
	if (
			(triggerEvent & BEV_EVENT_READING)
			&& (state->state == S_CONNECTED)
	) {
		return Event_Timeout_Banner;
	}
	if (
			(triggerEvent & BEV_EVENT_READING)
			&& (state->state == S_PASS_SENT)
	) {
		return Event_Timeout_Pass;
	}
	else if (state->state == S_SECURITY_CONNECTING) {
		return Event_Timeout_TLS;
	}
	else if (triggerEvent & BEV_EVENT_READING){
		return Event_Timeout_Read;
	}
	else {
		return Event_Timeout_Unknown;
	}
}

CtrlEventID_e id_ctrl_event_connected(hoststate_t* state, short triggerEvent) {
	if (state->ctrlUsingSec) {
		return Event_Connect_Security;
	}
	else {
		return Event_Connect_Unknown;
	}
}

CtrlEventID_e id_ctrl_event_error(hoststate_t* state, short triggerEvent) {
	if (
			(state->state == S_AUTH_TLS_SENT)
			|| (state->state == S_AUTH_SSL_SENT)
			|| (state->state == S_SECURITY_CONNECTING)
	) {
		if (state->secType == SEC_PRIMARY) {
			return Event_Error_Primary_Sec;
		}
		else if (state->secType == SEC_SECONDARY) {
			return Event_Error_Secondary_Sec;
		}
		else {
			return Event_Error_Unknown;
		}
	}
	else if (
					(state->state == S_PASS_SENT)
					&& (triggerEvent & BEV_EVENT_READING)
	) {
		return Event_Error_Pass;
	}
	else if (
					(state->state == S_CONNECTED)
					&& (triggerEvent & BEV_EVENT_READING)
	) {
		return Event_Error_Banner;
	}
	else if (
					(state->state == S_PORT_SENT)
					&& (triggerEvent & BEV_EVENT_READING)
	) {
		return Event_Error_Port;
	}
	else {
		return Event_Error_Unknown;
	}
}

CtrlEventID_e id_ctrl_event_eof(hoststate_t* state, short triggerEvent) {
	if (state->state == S_USER_SENT) {
		if (strcasestr(state->ctrlDBuffer.startPtr, ORACLE_EARLY_SEC_REQ)) {
			return Event_EOF_User_Sec_Req;
		}
		else {
			return Event_EOF_User;
		}
	}
	else if (
			(state->state == S_AUTH_TLS_SENT)
			|| (state->state == S_AUTH_SSL_SENT)
			|| (state->state == S_SECURITY_CONNECTING)
	) {
		if (state->secType == SEC_PRIMARY) {
			return Event_EOF_Primary_Sec;
		}
		else if (state->secType == SEC_SECONDARY) {
			return Event_EOF_Secondary_Sec;
		}
		else {
			return Event_EOF_Unknown;
		}
	}
	else if (state->state == S_CONNECTED) {
		return Event_EOF_Banner;
	}
	else if (state->state == S_QUIT_SENT) {
		return Event_EOF_Rude;
	}
	else if (state->state == S_PASS_SENT) {
		return Event_EOF_Pass;
	}
	else {
		return Event_EOF_Unknown;
	}

}

CtrlEventID_e id_ctrl_event(hoststate_t* state, short triggerEvent) {
	if (triggerEvent & BEV_EVENT_TIMEOUT) {
		return id_ctrl_event_timeout(state, triggerEvent);
	}
	else if (triggerEvent & BEV_EVENT_CONNECTED) {
		return id_ctrl_event_connected(state, triggerEvent);
	}
	else if (triggerEvent & BEV_EVENT_ERROR) {
		return id_ctrl_event_error(state, triggerEvent);
	}
	else if (triggerEvent & BEV_EVENT_EOF) {
		return id_ctrl_event_eof(state, triggerEvent);
	}
	else {
		return Event_Unknown;
	}

}

PassRespID_e id_pass_resp_double(hoststate_t* state) {
	struct evbuffer* inputBuffer = bufferevent_get_input(state->ctrlBev);
	char* ctrlBufferPtr = DBuffer_start_ptr(&state->ctrlDBuffer);

	log_error(
						"read-pass-resp",
						"%s: Extra data in buffer %s <+> %.*s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->ctrlDBuffer),
						evbuffer_get_length(bufferevent_get_input(state->ctrlBev)),
						evbuffer_pullup(inputBuffer, -1)
	);
	if (
			(
				(strcmp(ctrlBufferPtr, ORACLE_DOUBLE_PASS_RESP_1) == STR_MATCH)
					&& (evbuffer_matches(inputBuffer, ORACLE_DOUBLE_PASS_RESP_1))
			)
			||
			(
					(strcmp(ctrlBufferPtr, ORACLE_DOUBLE_PASS_RESP_2) == STR_MATCH)
					&& (evbuffer_matches(inputBuffer, ORACLE_DOUBLE_PASS_RESP_2))
			)
	) {
		return PASS_Reject_Double_1_2;
	}
	else if (
					(strcmp(ctrlBufferPtr, ORACLE_DOUBLE_PASS_RESP_3A) == STR_MATCH)
					&& (evbuffer_matches(inputBuffer, ORACLE_DOUBLE_PASS_RESP_3B))
	) {
		return PASS_Reject_Double_3;
	}
	else if (
					(strcmp(ctrlBufferPtr, ORACLE_DOUBLE_PASS_RESP_4A) == STR_MATCH)
					&& (evbuffer_matches(inputBuffer, ORACLE_DOUBLE_PASS_RESP_4B))
	) {
		return PASS_Reject_Double_4;
	}
	else if (
					(strcmp(ctrlBufferPtr, ORACLE_CHILD_DIED) == STR_MATCH)
					|| (evbuffer_matches(inputBuffer, ORACLE_CHILD_DIED))
	) {
		return PASS_Child_Died;
	}
	else {
		return PASS_Extra;
	}
}

PassRespID_e id_pass_resp(hoststate_t* state, char* replyCode, RespResult_e res) {
	if (res == RESP_EXTRA) {
		return id_pass_resp_double(state);
	}

	if (strcmp(
						DBuffer_start_ptr(&state->ctrlDBuffer),
						ORACLE_FIREWALL_AUTH
			) == STR_MATCH
	) {
		return PASS_Firewall;
	}
	else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_PASS_NO_ENTRY)) {
		return PASS_No_Entry;
	}
	else if (strstr(
									DBuffer_start_ptr(&state->ctrlDBuffer),
									ORACLE_CMS_MSG_ERROR
					)
	) {
		return PASS_Bftpd_Crash;
	}
	else if (is_reply_code(replyCode, FTP_LOGIN_OK)) {
		return PASS_OK;
	}
	else if (is_reply_code_strict(replyCode, FTP_PASS_REJECT)) {
		return PASS_Reject_530;
	}
	else if (is_reply_code_strict(replyCode, FTP_PASS_REJECT_550)) {
		return PASS_Reject_550;
	}
	else if (DBuffer_string_matches(&state->ctrlDBuffer, ORACLE_421_PASS_REJECT)) {
		return PASS_Reject_421;
	}
	else if (is_reply_code_strict(replyCode, FTP_PASS_REJECT_421)) {
		return PASS_Reject_421;
	}
	else {
		return PASS_Unexpected;
	}
}
