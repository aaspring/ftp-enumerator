#ifndef IMPORACLES_H
#define IMPORACLES_H

// *****************************************************************************
// Pure-FTPd
// *****************************************************************************
#define ORACLE_EARLY_SEC_REQ "cleartext"
#define ORACLE_BAD_PERMISSIONS "Can't change directory to"

// *****************************************************************************
// Various
// *****************************************************************************
#define ORACLE_FIREWALL_AUTH "220 Firewall Authentication required before proceeding with service\r\n"
#define ORACLE_VX_WORKS "VxWorks"

#define ORACLE_OPTIONAL_PRE_SEC "503 You are logged now.\r\n"
#define ORACLE_NO_TLS "534 Local policy on server does not allow TLS secure connections.\r\n"

// *****************************************************************************
// Banner oracles
// *****************************************************************************
#define ORACLE_VPS_NO_HOST_1 "550 There is no place for you to log in. Create domain for IP"
#define ORACLE_VPS_NO_HOST_2 "500 Sorry, no server available to handle request on"
#define ORACLE_SESSION_LIMIT "421 Session limit reached, closing control connection\r\n"
#define ORACLE_VSFTPD_BANNER_ERROR "500 OOPS: vsftpd:"

// *****************************************************************************
// PASS response oracles
// *****************************************************************************
#define ORACLE_421_PASS_REJECT "421-Access denied - wrong user name or password\r\n421 aborted"
#define ORACLE_DOUBLE_PASS_RESP_1 "421 Login incorrect.\r\n"
#define ORACLE_DOUBLE_PASS_RESP_2 "530 Login incorrect.\r\n"
#define ORACLE_DOUBLE_PASS_RESP_3A "530 User anonymous cannot log in.\r\n"
#define ORACLE_DOUBLE_PASS_RESP_3B "Login failed.\r\n"
#define ORACLE_DOUBLE_PASS_RESP_4A "530 Anonymous user not allowed.\r\n"
#define ORACLE_DOUBLE_PASS_RESP_4B "530 Login incorrect.\r\n"
#define ORACLE_CMS_MSG_ERROR "CMS_MSG_READ_ACCOUNT"
#define ORACLE_CHILD_DIED "500 OOPS: child died\r\n"
#define ORACLE_PASS_NO_ENTRY "500 OOPS: no entry found!\r\n"

// *****************************************************************************
// USER response oracles
// *****************************************************************************
#define ORACLE_USER_SEC_REQ_1 "SSL"
#define ORACLE_USER_SEC_REQ_2 "TLS"
#define ORACLE_USER_SEC_REQ_3 "331 Anonymous sessions must use encryption.\r\n"
#define ORACLE_PERMISSION_DENIED "331 Permission denied.\r\n"
#define ORACLE_VIRTUAL_SITES_1 "331 Valid hostname is expected.\r\n"
#define ORACLE_VIRTUAL_SITES_2 "530 Valid hostname is expected.\r\n"
#define ORACLE_UNABLE_SEC_ANON "421 Unable to set up secure anonymous FTP\r\n"
#define ORACLE_USER_RESP_NO_ANON "421 Anonymous logins are not allowed here.\r\n"
#define ORACLE_DOUBLE_USER_RESP_1A "530 This FTP server does not allow anonymous logins.\r\n"
#define ORACLE_DOUBLE_USER_RESP_1B "331 Please specify the password.\r\n"

#endif
