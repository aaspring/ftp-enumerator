#ifndef THREADSTATES_H
#define THREADSTATES_H

#define S_INITIAL 0
#define S_CONNECTED 1
#define S_USER_SENT 2
#define S_PASS_SENT 3
#define S_PORT_SENT 4
#define S_PWD_SENT  5
#define S_PASV_SENT 6
#define S_DATA_CONN 7
#define S_LIST_SENT 8
#define S_LIST_XFER 9
#define S_FEAT_SENT 10
#define S_STUPID_DEV 11
#define S_HELP_SENT 12
#define S_SYST_SENT 13
#define S_STAT_SENT 14
#define S_AUTH_TLS_SENT 15
#define S_PROT_SENT 16
#define S_PBSZ_INIT_SENT 17
#define S_PBSZ_SENT 18
#define S_TYPE_SENT 19
#define S_RETR_SENT 20
#define S_AUTH_SSL_SENT 21
#define S_RETR_XFER 22
#define S_PORT_LIST_SENT 23
#define S_QUIT_SENT 24
#define S_QUIT_ACK 25
#define S_PORT_LIST_XFER 26
#define S_SITE_SENT 27
#define S_SECURITY_CONNECTING 28
#define S_RESET_WAIT 29
#define S_UNSET_STATE 30

//**************************
// UPDATE IN ftpEnumerator.c
//**************************

extern char* thread_state_strings[];

#endif
