#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "../include/cfaaBannerCheck.h"

#define ORACLE_NO_ANON_LOGIN "No anonymous login"
#define ORACLE_NO_ANON_ACCESS "No anonymous access"
#define ORACLE_CFAA_LAW_NUMBER "99-474"
#define ORACLE_CFAA_LAW_NAME "cfaa"

bool cfaa_login_not_allowed(char* banner, char* retMsg, size_t retMsgSize) {
	if (strcasestr(banner, ORACLE_NO_ANON_LOGIN)) {
		snprintf(
						retMsg,
						retMsgSize,
						"%s",
						"matched '" ORACLE_NO_ANON_LOGIN "'"
		);
		return true;
	}

	if (strcasestr(banner, ORACLE_NO_ANON_ACCESS)) {
		snprintf(
						retMsg,
						retMsgSize,
						"%s",
						"matched '" ORACLE_NO_ANON_ACCESS "'"
		);
		retMsg[retMsgSize - 1] = '\0';
		return true;
	}

	if (strcasestr(banner, ORACLE_CFAA_LAW_NUMBER)) {
		snprintf(
						retMsg,
						retMsgSize,
						"%s",
						"matched '" ORACLE_CFAA_LAW_NUMBER "'"
		);
		retMsg[retMsgSize - 1] = '\0';
		return true;
	}

	if (strcasestr(banner, ORACLE_CFAA_LAW_NAME)) {
		snprintf(
						retMsg,
						retMsgSize,
						"%s",
						"matched '" ORACLE_CFAA_LAW_NAME "'"
		);
		retMsg[retMsgSize - 1] = '\0';
		return true;
	}

	return false;
}
