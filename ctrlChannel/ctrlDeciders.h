#ifndef CTRLDECIDERS_H
#define CTRLDECIDERS_H

#include "parseResp.h"

#include "../include/ftpEnumerator.h"

typedef enum {
	USER_Unexpected,
	USER_TLS_530,
	USER_TLS_Other,
	USER_TLS_Optimistic,
	USER_Reject_530,
	USER_Reject_Explicit,
	USER_Reject_Double_1,
	USER_Virtual_Sites,
	USER_No_Pass,
	USER_Accept_User,
	USER_Early_Sec_Req,
	USER_Bad_Config,
	USER_Extra,
} UserRespID_e;

typedef enum {
	Event_Unknown,

	Event_Timeout_Banner,
	Event_Timeout_Read,
	Event_Timeout_TLS,
	Event_Timeout_Pass,
	Event_Timeout_Unknown,

	Event_Connect_Security,
	Event_Connect_Unknown,

	Event_Error_Primary_Sec,
	Event_Error_Secondary_Sec,
	Event_Error_Pass,
	Event_Error_Banner,
	Event_Error_Port,
	Event_Error_Unknown,

	Event_EOF_User,
	Event_EOF_User_Sec_Req,
	Event_EOF_Banner,
	Event_EOF_Rude,
	Event_EOF_Pass,
	Event_EOF_Primary_Sec,
	Event_EOF_Secondary_Sec,
	Event_EOF_Unknown,

} CtrlEventID_e;

typedef enum {
	PASS_Unexpected,
	PASS_OK,
	PASS_Firewall,
	PASS_Reject_530,
	PASS_Reject_421,
	PASS_Reject_Double_1_2,
	PASS_Reject_Double_3,
	PASS_Reject_Double_4,
	PASS_Extra,
	PASS_Bftpd_Crash,
	PASS_Child_Died,
	PASS_Reject_550,
	PASS_No_Entry,

} PassRespID_e;

UserRespID_e id_user_resp(hoststate_t* state, char* replyCode, RespResult_e res);
CtrlEventID_e id_ctrl_event(hoststate_t* state, short triggerEvent);
PassRespID_e id_pass_resp(hoststate_t* state, char* replyCode, RespResult_e res);

#endif
