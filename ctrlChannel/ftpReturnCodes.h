#ifndef FTPRETURNCODES_H
#define FTPRETURNCODES_H

#define FTP_CONNECTED "220"
#define FTP_LOGIN_OK "230"
#define FTP_PATH_OK "257"
#define FTP_PASV_OK "227"
#define FTP_LIST_IN "150"
#define FTP_LIST_IN_ALT "125"
#define FTP_XFER_OK "226"
#define FTP_QUIT_OK "221"
#define FTP_SYST_OK "215"
#define FTP_TLS_OK "234"
#define FTP_PBSZ_OK "200"
#define FTP_PROT_OK "200"
#define FTP_UNK_CMD "500"
#define FTP_TYPE_OK "200"
#define FTP_BAD_CMD_SEQ "503"
#define FTP_PASS_REJECT "530"

#define FTP_PWD_OK "2XX"

#define FTP_HELP_OK "2XX"

#define FTP_FEAT_OK "2XX"

#define FTP_STAT_OK "2XX"

#define FTP_SITE_OK "2XX"

#define FTP_PASS_REJECT_421 "421"
#define FTP_PASS_REJECT_550 "550"

#define FTP_BAD_PORT_ALLOW "2XX"
#define FTP_BAD_PORT_DENY_1 "5XX"
#define FTP_BAD_PORT_DENY_2 "4XX"

// RETR resp
#define FTP_RETR_OK "150"
#define FTP_NO_FILE "550"
#define FTP_NO_FILE_2 "4XX"

// USER resp
#define FTP_SECURITY_REQ "534"
#define FTP_USER_RESP_530 "530"	// This has multiple meanings as a USER resp
#define FTP_USER_REJECT "532"
#define FTP_SECURITY_REQ_ALT "503"
#define FTP_SECURITY_REQ_ALT_3 "550"
#define FTP_SECURITY_OPTIMISTIC "5XX"
#define FTP_NO_PASS "2XX"
#define FTP_EARLY_SEC_REQ "4XX"
#define FTP_USER_OK "3XX"
#define FTP_NEED_USER_ACCT "332"
#define FTP_ACCT_ERR "500"

#endif
