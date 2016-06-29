#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "ctrlEnd.h"
#include "parseResp.h"

#include "../include/magicNumbers.h"
#include "../include/outputDB.h"
#include "../include/terminationCodes.h"
#include "../include/logger.h"
#include "../include/interestCodes.h"
#include "../include/recordKeys.h"
#include "../include/recorder.h"

bool is_reply_code(char* foundReply, char* expectedReply) {
	/*
	log_trace(
						"hasReturnCode", "?%s? --- %.*s",
						retCode,
						strlen(str) - 2,
						str);
						*/
#ifdef REPLY_CODE_STRICT
	assert(expectedReply[1] != 'X');
	assert(expectedReply[2] != 'X');
	return strncmp(foundReply, expectedReply, 3) == STR_MATCH;
#else
	return strncmp(foundReply, expectedReply, 1) == STR_MATCH;
#endif
}

bool is_reply_code_strict(char* foundReply, char* expectedReply) {
	assert((expectedReply[1] != 'X') && (expectedReply[2] != 'X'));
	return strncmp(foundReply, expectedReply, 3) == STR_MATCH;
}

void unexpected_reply(
											struct bufferevent* bev,
											hoststate_t* state,
											int failureCode,
											char* functionName
) {
	log_error(functionName, "%s: resp failure", state->ip_address_str);
	state->terminationCode = failureCode;
	snprintf(
					state->terminationDesc,
					MAX_STATIC_BUFFER_SIZE,
					"Response failure in %s",
					functionName
	);
	disconnect_pleasant(bev, state);
}

RespResult_e parse_response(
																struct bufferevent* bev,
																hoststate_t* state,
																char* foundReplyCode
) {
	log_trace("parse-response", "%s", state->ip_address_str);

	struct evbuffer* input = bufferevent_get_input(bev);
	int inputLen = evbuffer_get_length(input);

	while (inputLen != 0) {
		ValidationResult_t res = fill_DBuffer_line(bev, state, &state->ctrlDBuffer);

		if (res == VALID_CHUNK) {
			foundReplyCode[0] = '\0';
			log_debug("parse-response", "%s: Incomplete line", state->ip_address_str);
			break;
		}

		char* lastLine = get_last_line(&state->ctrlDBuffer);

		log_debug(
							"parse-response",
							"%s: last line : %s",
							state->ip_address_str,
							lastLine
		);

		if (
				(lastLine != NULL)
				&& (isdigit(lastLine[0]))
				&& (isdigit(lastLine[1]))
				&& (isdigit(lastLine[2]))
				&& (lastLine[3] == ' ')
		) {
			memcpy(foundReplyCode, lastLine, 3);
			foundReplyCode[3] = '\0';
			break;
		}
		else {
			foundReplyCode[0] = '\0';
		}

		inputLen = evbuffer_get_length(input);
	}


	if (state->ctrlDBuffer.len > MAX_CTRL_AMT) {
		state->terminationCode = FAIL_TRAP_CTRL_AMT;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"Too much data (%zuB) in state %d",
						state->ctrlDBuffer.len,
						state->state
		);
		log_error(
							"parse-response",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
		);
		record_raw_dbuffer(state, RECKEY_TOO_MUCH_DATA, NULL, 0, &state->ctrlDBuffer);
		DBuffer_clear(&state->ctrlDBuffer);
		disconnect_abrupt(bev, state);
		return RESP_ERROR;
	}

	if (!DBuffer_is_string(&state->ctrlDBuffer)) {
		state->terminationCode = FAIL_NULL_IN_RESP;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"A NULL was found in the response"
		);
		log_error(
							"parse-response",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
		);
		DBuffer_clear(&state->ctrlDBuffer);
		disconnect_abrupt(bev, state);
		return RESP_ERROR;
	}

	if (foundReplyCode[0] != '\0') {
		if (evbuffer_get_length(input) == 0) {
			return RESP_FOUND;
		}
		else {
			return RESP_EXTRA;
		}
	}
	else {
		return RESP_INCOMPLETE;
	}
}

char* get_last_line(DBuffer_t* dStrBuffer) {
	size_t len = dStrBuffer->len;
	char* startPtr = DBuffer_start_ptr(dStrBuffer);
	char* endPtr = startPtr + len - 1;
	if (len < 2) {
		return NULL;
	}

	if ((*endPtr == '\n') && (*(endPtr - 1) == '\r')) {
		endPtr -= 2;
	}

	while (endPtr > startPtr) {
		if (
				(*endPtr == '\n')
				&& (*(endPtr - 1) == '\r')
		) {
			return endPtr + 1;
		}
		endPtr--;
	}
	return startPtr;
}

ValidationResult_t fill_DBuffer_all(
																			struct bufferevent* bev,
																			hoststate_t* state,
																			DBuffer_t* dBuffer

) {
	char sBuffer[MAX_STATIC_BUFFER_SIZE];
	struct evbuffer* input = bufferevent_get_input(bev);
	ev_ssize_t numChar = 0;
	log_debug(
						"fill-DBuffer-chunk",
						"%s: %d bytes in buffer",
						state->ip_address_str,
						evbuffer_get_length(input)
	);

	while (evbuffer_get_length(input) != 0) {
		numChar = evbuffer_remove(input, sBuffer, MAX_STATIC_BUFFER_SIZE);
		DBuffer_append_bytes(dBuffer, sBuffer, numChar);
	}

	return VALID_CHUNK;
}

ValidationResult_t fill_DBuffer_line(
																		struct bufferevent* bev,
																		hoststate_t* state,
																		DBuffer_t* dBuffer
) {
	log_trace("fill-DBuffer-line", "%s", state->ip_address_str);
	char sBuffer[MAX_STATIC_BUFFER_SIZE];
	ev_ssize_t numChar = 0;
	struct evbuffer* input = bufferevent_get_input(bev);
	int i = 0;
	char* eolPtr = NULL;

	while (true) {
		numChar = evbuffer_copyout(input, sBuffer, MAX_STATIC_BUFFER_SIZE);
		for (i = 0; i < numChar - 1; i++) {
			if ((sBuffer[i] == '\r') && (sBuffer[i + 1] == '\n')) {
				eolPtr = sBuffer + i;
				break;
			}
		}

		if (eolPtr == NULL) {
			// There is no EOL, must be a chunk
			DBuffer_append_bytes(dBuffer, sBuffer, numChar);
			evbuffer_drain(input, numChar);
		}
		else {
			// There is at least 1 complete line in the sBuffer
			size_t lineLen = (eolPtr - sBuffer) + 2; // + 2 b/c strstr() at beginning
			DBuffer_append_bytes(dBuffer, sBuffer, lineLen);
			evbuffer_drain(input, lineLen);
			return VALID_LINE;
		}

		if (evbuffer_get_length(input) == 0) {
			return VALID_CHUNK;
		}
	}
}

bool evbuffer_matches(struct evbuffer* evBuffer, char* str) {
	size_t strLen = strlen(str);
	if (evbuffer_get_length(evBuffer) != strLen) {
		return false;
	}

	unsigned char* evBufferPtr = evbuffer_pullup(evBuffer, -1);

	return memcmp(str, evBufferPtr, strLen) == 0;
}
