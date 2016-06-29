#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

#include "../include/ftpEnumerator.h"
#include "../include/dbKeys.h"
#include "../include/logger.h"
#include "../include/outputDB.h"
#include "../include/textParseHelper.h"
#include "../include/DBuffer.h"
#include "../include/interestCodes.h"
#include "../include/recorder.h"

#include "listParseHelper.h"

#define VX_WORKS_ERR_OPEN "Can't open"
#define VX_WORKS_ERR_OPEN_LEN sizeof(VX_WORKS_ERR_OPEN) - 1
#define VX_WORKS_HEADER "  size          date       time       name"
#define VX_WORKS_HEADER_LEN sizeof(VX_WORKS_HEADER) - 1
#define VX_WORKS_HEADER_SEP "--------       ------     ------    --------"
#define VX_WORKS_HEADER_SEP_LEN sizeof(VX_WORKS_HEADER_SEP) - 1

typedef enum {
	List_Linux,
	List_Windows,
	List_VxWorks,
} ListType_e;

bool is_useful_dir(char* sBuffer) {
	if (strcmp(sBuffer, "/proc/") == STR_MATCH) {
		return false;
	}

	if (strcmp(sBuffer, "/dev/") == STR_MATCH) {
		return false;
	}

	return true;
}

bool is_month_str(char* sBuffer) {
	if (
			(strcasecmp(sBuffer, "Jan") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Feb") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Mar") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Apr") == STR_MATCH)
			|| (strcasecmp(sBuffer, "May") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Jun") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Jul") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Aug") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Sep") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Oct") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Nov") == STR_MATCH)
			|| (strcasecmp(sBuffer, "Dec") == STR_MATCH)
	) {
		return true;
	}

	return false;
}

bool is_month_int(char* sBuffer) {
	int value = 0;
	int numScanned = sscanf(sBuffer, "%d", &value);

	if (numScanned != 1) {
		return false;
	}

	if ((value <= 12) && (value > 0)) {
		return true;
	}
	else {
		return false;
	}
}

bool is_day(char* sBuffer) {
	size_t tokLen = strlen(sBuffer);
	if (
			(tokLen <= 2)
			&& (isdigit(sBuffer[0]))
			&& (
					(tokLen == 1)
					|| (isdigit(sBuffer[1]))
			)
	) {
		return true;
	}
	else {
		return false;
	}
}

bool is_year_time(char* sBuffer) {
	int one = -1;
	int two = -1;
	int three = -1;
	size_t numScanned = 0;

	char* colonPtr = strchr(sBuffer, ':');
	if (colonPtr) {
		numScanned = sscanf(sBuffer, "%d:%d:%d", &one, &two, &three);
		if (numScanned == 3) {
			if (
					(one >= 0)
					&& (one < 24)
					&& (two >= 0)
					&& (two < 60)
					&& (three >= 0)
					&& (three < 60)
			) {
				return true;
			}
			else {
				return false;
			}
		}

		numScanned = sscanf(sBuffer, "%d:%d", &one, &two);
		if (
				(numScanned == 2)
				&& (one >= 0)
				&& (one < 24)
				&& (two >= 0)
				&& (two < 60)
		) {
			return true;
		}

		return false;
	}
	else {
		if (strlen(sBuffer) == 4) {
			return isdigit(sBuffer[0]) && isdigit(sBuffer[1])
					&& isdigit(sBuffer[2]) && isdigit(sBuffer[3]);
		}
		else {
			return false;
		}
	}
}

void build_dbuffer_from_token_iter(TokenQueue_t* tokenQueue, DBuffer_t* dBuffer) {
	TokenType_e curTokenType = Token_Unset;
	char sBuffer[MAX_STATIC_BUFFER_SIZE];

	DBuffer_clear(dBuffer);
	while (token_queue_iter_has_next(tokenQueue)) {
		curTokenType = token_queue_iter_next(
																				tokenQueue,
																				sBuffer,
																				MAX_STATIC_BUFFER_SIZE
		);

		switch (curTokenType) {
		case (Token_String) :
				DBuffer_append(dBuffer, sBuffer);
				break;

		case (Token_Space) :
				DBuffer_append(dBuffer, " ");
				break;

		case (Token_Tab) :
				DBuffer_append(dBuffer, "\t");
				break;

		default :
				assert(false);
		}
	}
}

int find_linux_name_field2(
		TokenQueue_t* tokenQueue,
		DBuffer_t* nameDBuffer
) {
	char sBuffer[MAX_STATIC_BUFFER_SIZE];
	TokenType_e curTokenType = Token_Unset;
	bool retVal = false;

	// Try format #1 :
	// Permissions ~~~~~ Month Day Year/Time <name>
	token_queue_iter_begin(tokenQueue);
	while (token_queue_iter_has_next(tokenQueue)) {
		curTokenType = token_queue_iter_next_string(
																								tokenQueue,
																								sBuffer,
																								MAX_STATIC_BUFFER_SIZE
		);
		if (curTokenType == Token_Empty) {
			retVal = false;
			goto CLEANUP;
		}

		if (is_month_str(sBuffer)) {
			curTokenType = token_queue_iter_next_string(
																								tokenQueue,
																								sBuffer,
																								MAX_STATIC_BUFFER_SIZE
			);
			if (curTokenType == Token_Empty) {
				retVal = false;
				goto CLEANUP;
			}

			if (is_day(sBuffer)) {
				curTokenType = token_queue_iter_next_string(
																										tokenQueue,
																										sBuffer,
																										MAX_STATIC_BUFFER_SIZE
				);
				if (curTokenType == Token_Empty) {
					retVal = false;
					goto CLEANUP;
				}

				if (is_year_time(sBuffer)) {
					while (token_queue_iter_has_next(tokenQueue)) {
						// Skip leading space
						curTokenType = token_queue_iter_next(
																								tokenQueue,
																								sBuffer,
																								MAX_STATIC_BUFFER_SIZE
						);
						if (curTokenType == Token_String) {
							token_queue_iter_rewind_string(tokenQueue);
							break;
						}
						else if (curTokenType == Token_Empty) {
							break;
						}

					}

					build_dbuffer_from_token_iter(tokenQueue, nameDBuffer);
					if (nameDBuffer->len != 0) {
						retVal = true;
						goto CLEANUP;
					}
					else {
						retVal = false;
						goto CLEANUP;
					}
				}
				else {
					token_queue_iter_rewind_string(tokenQueue);
					token_queue_iter_rewind_string(tokenQueue);
				}
			}
			else {
				token_queue_iter_rewind_string(tokenQueue);
			}
		}
	}
	token_queue_iter_end(tokenQueue);

	// Try format #2 :
	// Permissions ~~~~~ Day. Month Year/Time <name>
	token_queue_iter_begin(tokenQueue);
	while (token_queue_iter_has_next(tokenQueue)) {
		curTokenType = token_queue_iter_next_string(
																							tokenQueue,
																							sBuffer,
																							MAX_STATIC_BUFFER_SIZE
		);
		if (curTokenType == Token_Empty) {
			retVal = false;
			goto CLEANUP;
		}

		if (is_month_str(sBuffer)) {
			curTokenType = token_queue_iter_next_string(
																									tokenQueue,
																									sBuffer,
																									MAX_STATIC_BUFFER_SIZE
			);
			if (curTokenType == Token_Empty) {
				retVal = false;
				goto CLEANUP;
			}

			if (is_year_time(sBuffer)) {
				while (token_queue_iter_has_next(tokenQueue)) {
					// Skip leading space
					curTokenType = token_queue_iter_next(
																							tokenQueue,
																							sBuffer,
																							MAX_STATIC_BUFFER_SIZE
					);
					if (curTokenType == Token_String) {
						token_queue_iter_rewind_string(tokenQueue);
						break;
					}
					else if (curTokenType == Token_Empty) {
						break;
					}
				}

				build_dbuffer_from_token_iter(tokenQueue, nameDBuffer);
				if (nameDBuffer->len != 0) {
					retVal = true;
					goto CLEANUP;
				}
				else {
					retVal = false;
					goto CLEANUP;
				}
			}
			else {
				token_queue_iter_rewind_string(tokenQueue);
			}
		}
	}
	token_queue_iter_end(tokenQueue);

	// Try format #3 :
	// Permissions ~~~~~ Month# Day Year/Time <name>
	token_queue_iter_begin(tokenQueue);
	while (token_queue_iter_has_next(tokenQueue)) {
		curTokenType = token_queue_iter_next_string(
																								tokenQueue,
																								sBuffer,
																								MAX_STATIC_BUFFER_SIZE
		);
		if (curTokenType == Token_Empty) {
			retVal = false;
			goto CLEANUP;
		}

		if (is_month_int(sBuffer)) {
			curTokenType = token_queue_iter_next_string(
																								tokenQueue,
																								sBuffer,
																								MAX_STATIC_BUFFER_SIZE
			);
			if (curTokenType == Token_Empty) {
				retVal = false;
				goto CLEANUP;
			}

			if (is_day(sBuffer)) {
				curTokenType = token_queue_iter_next_string(
																										tokenQueue,
																										sBuffer,
																										MAX_STATIC_BUFFER_SIZE
				);
				if (curTokenType == Token_Empty) {
					retVal = false;
					goto CLEANUP;
				}

				if (is_year_time(sBuffer)) {
					while (token_queue_iter_has_next(tokenQueue)) {
						// Skip leading space
						curTokenType = token_queue_iter_next(
																								tokenQueue,
																								sBuffer,
																								MAX_STATIC_BUFFER_SIZE
						);
						if (curTokenType == Token_String) {
							token_queue_iter_rewind_string(tokenQueue);
							break;
						}
						else if (curTokenType == Token_Empty) {
							break;
						}

					}

					build_dbuffer_from_token_iter(tokenQueue, nameDBuffer);
					if (nameDBuffer->len != 0) {
						retVal = true;
						goto CLEANUP;
					}
					else {
						retVal = false;
						goto CLEANUP;
					}
				}
				else {
					token_queue_iter_rewind_string(tokenQueue);
					token_queue_iter_rewind_string(tokenQueue);
				}
			}
			else {
				token_queue_iter_rewind_string(tokenQueue);
			}
		}
	}
	token_queue_iter_end(tokenQueue);

	// Try format #4 :
	// Permissions ~~~~~ Day. Month# Year/Time <name>
	token_queue_iter_begin(tokenQueue);
	while (token_queue_iter_has_next(tokenQueue)) {
		curTokenType = token_queue_iter_next_string(
																							tokenQueue,
																							sBuffer,
																							MAX_STATIC_BUFFER_SIZE
		);
		if (curTokenType == Token_Empty) {
			retVal = false;
			goto CLEANUP;
		}

		if (is_month_int(sBuffer)) {
			curTokenType = token_queue_iter_next_string(
																									tokenQueue,
																									sBuffer,
																									MAX_STATIC_BUFFER_SIZE
			);
			if (curTokenType == Token_Empty) {
				retVal = false;
				goto CLEANUP;
			}

			if (is_year_time(sBuffer)) {
				while (token_queue_iter_has_next(tokenQueue)) {
					// Skip leading space
					curTokenType = token_queue_iter_next(
																							tokenQueue,
																							sBuffer,
																							MAX_STATIC_BUFFER_SIZE
					);
					if (curTokenType == Token_String) {
						token_queue_iter_rewind_string(tokenQueue);
						break;
					}
					else if (curTokenType == Token_Empty) {
						break;
					}
				}

				build_dbuffer_from_token_iter(tokenQueue, nameDBuffer);
				if (nameDBuffer->len != 0) {
					retVal = true;
					goto CLEANUP;
				}
				else {
					retVal = false;
					goto CLEANUP;
				}
			}
			else {
				token_queue_iter_rewind_string(tokenQueue);
			}
		}
	}
	token_queue_iter_end(tokenQueue);


CLEANUP :
	return retVal;
}

void parse_unix_dir_name(hoststate_t* state, char* linePtr) {
	TokenQueue_t tokenQueue;
	DBuffer_t fullDirDBuffer;
	DBuffer_t dirNameDBuffer;
	bool foundName = false;

	DBuffer_init(&dirNameDBuffer, DBUFFER_INIT_TINY);
	DBuffer_init(&fullDirDBuffer, DBUFFER_INIT_TINY);
	token_queue_init(&tokenQueue);

	break_tokens_preserve_space(&tokenQueue, linePtr);

	foundName = find_linux_name_field2(&tokenQueue, &dirNameDBuffer);
	if (!foundName) {
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_2(
												state,
												"Parsed a Linux directory line with no directory -- Path : %s -- Line : %s",
												DBuffer_start_ptr(&state->itemDBuffer),
												state->itemDBuffer.len,
												linePtr,
												strlen(linePtr)
		);
		log_info(
						"parse-unix-dir-name",
						"Parsed a Linux directory line with no directory %s: Dir : %s, Line : %s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->itemDBuffer),
						linePtr
		);
		goto CLEANUP;
	}

	// Remove trailing spaces
	while (
			(DBuffer_start_ptr(&dirNameDBuffer)[dirNameDBuffer.len - 1] == ' ')
			|| (DBuffer_start_ptr(&dirNameDBuffer)[dirNameDBuffer.len - 1] == '\t')
	) {
		DBuffer_remove_bytes(&dirNameDBuffer, 1);
	}

	if (
			(strcmp(".", DBuffer_start_ptr(&dirNameDBuffer)) == STR_MATCH)
			|| (strcmp("..", DBuffer_start_ptr(&dirNameDBuffer)) == STR_MATCH)
	) {
		// Is guaranteed to be a directory we've already crawled, ignore
		goto CLEANUP;
	}

	if (DBuffer_start_ptr(&dirNameDBuffer)[0] == '/') {
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_2(
															state,
															"Found an absolute file path (%s) in %s",
															DBuffer_start_ptr(&dirNameDBuffer),
															dirNameDBuffer.len,
															DBuffer_start_ptr(&state->itemDBuffer),
															state->itemDBuffer.len
		);
		goto CLEANUP;
	}

	DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&state->itemDBuffer));
	DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&dirNameDBuffer));
	if (DBuffer_start_ptr(&fullDirDBuffer)[fullDirDBuffer.len - 1] != '/') {
		DBuffer_append(&fullDirDBuffer, "/");
	}

	if (is_useful_dir(DBuffer_start_ptr(&fullDirDBuffer))) {
		str_enqueue(&state->dirQueue, DBuffer_start_ptr(&fullDirDBuffer));
		log_info(
						"parse-unix-dir-name",
						"%s: Adding directory name : |%s|",
						state->ip_address_str,
						DBuffer_start_ptr(&fullDirDBuffer)
		);
	}
	else {
		log_info(
						"parse-unix-dir-name",
						"%s: Ignoring useless dir : |%s|",
						state->ip_address_str,
						DBuffer_start_ptr(&fullDirDBuffer)
		);
	}

CLEANUP :
	token_queue_destroy(&tokenQueue);
	DBuffer_destroy(&fullDirDBuffer);
	DBuffer_destroy(&dirNameDBuffer);
	return;
}

void parse_unix_link_name(hoststate_t* state, char* linePtr) {
	TokenQueue_t tokenQueue;
	TokenQueue_t fieldTokenQueue;
	DBuffer_t fieldDBuffer;
	DBuffer_t realNameDBuffer;
	DBuffer_t fakeNameDBuffer;
	DBuffer_t fullDirDBuffer;
	char sBuffer[MAX_STATIC_BUFFER_SIZE];
	bool foundField = false;
	TokenType_e tokenType = Token_Unset;
	DBuffer_t* curDBufferPtr = NULL;

	DBuffer_init(&fieldDBuffer, DBUFFER_INIT_TINY);
	DBuffer_init(&realNameDBuffer, DBUFFER_INIT_TINY);
	DBuffer_init(&fakeNameDBuffer, DBUFFER_INIT_TINY);
	DBuffer_init(&fullDirDBuffer, DBUFFER_INIT_TINY);
	token_queue_init(&tokenQueue);
	token_queue_init(&fieldTokenQueue);

	break_tokens_preserve_space(&tokenQueue, linePtr);

	foundField = find_linux_name_field2(&tokenQueue, &fieldDBuffer);
	if (!foundField) {
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_2(
															state,
															"Parsed a Linux link line with no field -- Path : %s -- Line : %s",
															DBuffer_start_ptr(&state->itemDBuffer),
															state->itemDBuffer.len,
															linePtr,
															strlen(linePtr)
		);
		log_info(
						"parse-unix-link-name",
						"Parsed a Linux link line with no name field %s: Dir : %s, Line : %s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->itemDBuffer),
						linePtr
		);
		goto CLEANUP;
	}

	break_tokens_preserve_space(&fieldTokenQueue, DBuffer_start_ptr(&fieldDBuffer));

	token_queue_iter_begin(&fieldTokenQueue);
	curDBufferPtr = &fakeNameDBuffer;
	while (token_queue_iter_has_next(&fieldTokenQueue)) {
		tokenType = token_queue_iter_next(
																			&fieldTokenQueue,
																			sBuffer,
																			MAX_STATIC_BUFFER_SIZE
		);
		switch (tokenType) {
		case (Token_String) :
				if (strcmp(sBuffer, "->") == STR_MATCH) {
					curDBufferPtr = &realNameDBuffer;
				}
				else {
					DBuffer_append(curDBufferPtr, sBuffer);
				}
				break;

		case (Token_Space) :
				DBuffer_append(curDBufferPtr, " ");
				break;

		case (Token_Tab) :
				DBuffer_append(curDBufferPtr, "\t");
				break;

		default :
				assert(false);
		}
	}

	// Remove trailing spaces
	while (
				(DBuffer_start_ptr(&fakeNameDBuffer)[fakeNameDBuffer.len - 1] == ' ')
				|| (DBuffer_start_ptr(&fakeNameDBuffer)[fakeNameDBuffer.len - 1] == '\t')
	) {
		DBuffer_remove_bytes(&fakeNameDBuffer, 1);
	}
	while (
				(DBuffer_start_ptr(&realNameDBuffer)[realNameDBuffer.len - 1] == ' ')
				|| (DBuffer_start_ptr(&realNameDBuffer)[realNameDBuffer.len - 1] == '\t')
	) {
		DBuffer_remove_bytes(&realNameDBuffer, 1);
	}

	// Remove leading spaces from real name
	size_t totalLen = realNameDBuffer.len;
	char* tempBase = strdup(DBuffer_start_ptr(&realNameDBuffer));
	char* temp = tempBase;
	DBuffer_clear(&realNameDBuffer);
	size_t index = 0;
	while (
			(index < totalLen)
			&& (
					(temp[0] == ' ')
					|| (temp[0] == '\t')
			)
	) {
		temp++;
	}
	DBuffer_append(&realNameDBuffer, temp);
	free(tempBase);


	if ((fakeNameDBuffer.len == 0) || (realNameDBuffer.len == 0)) {
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_2(
															state,
															"Parsed a Linux link line with no real/fake name -- Path : %s -- Line : %s",
															DBuffer_start_ptr(&state->itemDBuffer),
															state->itemDBuffer.len,
															linePtr,
															strlen(linePtr)
		);
		log_info(
						"parse-unix-link-name",
						"%s: Parsed a Linux link line with no real/fake name : Dir : %s, Line : %s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->itemDBuffer),
						linePtr
		);
		goto CLEANUP;
	}

	if (
			(strcmp(".", DBuffer_start_ptr(&fakeNameDBuffer)) == STR_MATCH)
			|| (strcmp("..", DBuffer_start_ptr(&fakeNameDBuffer)) == STR_MATCH)
			|| (strcmp(".", DBuffer_start_ptr(&realNameDBuffer)) == STR_MATCH)
			|| (strcmp("..", DBuffer_start_ptr(&realNameDBuffer)) == STR_MATCH)
	) {
		// Is guaranteed to be a directory we've already crawled, ignore
		goto CLEANUP;
	}

	if (DBuffer_start_ptr(&realNameDBuffer)[realNameDBuffer.len - 1] != '/') {
		// Don't try to LIST linked files
		log_info(
						"parse-unix-link-name",
						"%s: Symbolic link to file %s -> %s",
						state->ip_address_str,
						DBuffer_start_ptr(&fakeNameDBuffer),
						DBuffer_start_ptr(&realNameDBuffer)
		);

		record_misc_partial_raw_2(
															state,
															"Likely file symlink ignored -- %s -> %s",
															DBuffer_start_ptr(&fakeNameDBuffer),
															fakeNameDBuffer.len,
															DBuffer_start_ptr(&realNameDBuffer),
															realNameDBuffer.len
		);
		goto CLEANUP;
	}

	DBuffer_init(&fullDirDBuffer, DBUFFER_INIT_TINY);
	DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&state->itemDBuffer));
	DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&fakeNameDBuffer));
	if (DBuffer_start_ptr(&fullDirDBuffer)[fullDirDBuffer.len - 1] != '/') {
		DBuffer_append(&fullDirDBuffer, "/");
	}

#ifdef FOLLOW_LINKS
	if (is_useful_dir(DBuffer_start_ptr(&fullDirDBuffer))) {
		str_enqueue(&state->dirQueue, DBuffer_start_ptr(&fullDirDBuffer));
		log_info(
						"parse-unix-link-name",
						"%s: Adding link name : %s going to %s",
						state->ip_address_str,
						DBuffer_start_ptr(&fakeNameDBuffer),
						DBuffer_start_ptr(&realNameDBuffer)
		);
	}
	else {
		log_info(
						"parse-unix-link-name",
						"%s: Ignoring uninteresting dir : %s going to %s",
						state->ip_address_str,
						DBuffer_start_ptr(&fakeNameDBuffer),
						DBuffer_start_ptr(&realNameDBuffer)
		);
	}
#else
	log_info(
					"parse-unix-link-name",
					"%s: Ignoring link name : |%s -> %s",
					state->ip_address_str,
					DBuffer_start_ptr(&fakeNameDBuffer),
					DBuffer_start_ptr(&realNameDBuffer)
	);
#endif

CLEANUP :
	token_queue_destroy(&tokenQueue);
	token_queue_destroy(&fieldTokenQueue);
	DBuffer_destroy(&fieldDBuffer);
	DBuffer_destroy(&realNameDBuffer);
	DBuffer_destroy(&fakeNameDBuffer);
	DBuffer_destroy(&fullDirDBuffer);
}

bool parse_list_data_unix(hoststate_t* state, char* strBuffer) {
	log_trace("parse-list-data-unix", "%s", state->ip_address_str);
	str_queue_t lineQueue;
	char sBuffer[DBUFFER_INIT_LARGE];

	str_queue_init_custom_buffer_size(&lineQueue, DBUFFER_INIT_LARGE);
	break_lines(&lineQueue, strBuffer);

	while (lineQueue.len != 0) {
		str_dequeue(&lineQueue, sBuffer, DBUFFER_INIT_LARGE);
		log_debug("parse-list-data-unix", "Parsing line |%s|", sBuffer);
		if (sBuffer[0] == 'd') {
			parse_unix_dir_name(state, sBuffer);
		}
		else if (sBuffer[0] == 'l') {
			parse_unix_link_name(state, sBuffer);
		}
		else if (
				(sBuffer[0] == '-')
				|| (strncasecmp(sBuffer, "total", 5) == STR_MATCH)
		) {
			// Is a file or a "total", ignore
		}
		else {
			if (strcmp(sBuffer, ".") == STR_MATCH) {
				continue;
			}

			state->interestMask |= INTEREST_CHECK_MISC;
			record_misc_partial_raw_1(
																state,
																"Don't know how to parse linux line : %s",
																sBuffer,
																strlen(sBuffer)
			);
		}
	}

	str_queue_destroy(&lineQueue);
	return true;
}

void parse_windows_dir_name(hoststate_t* state, char* linePtr) {
	TokenQueue_t tokenQueue;
	char tokenBuffer[MAX_STATIC_BUFFER_SIZE];
	TokenType_e tokenType = Token_Unset;
	DBuffer_t nameDBuffer;
	DBuffer_t fullDirDBuffer;
	bool foundName = false;

	token_queue_init(&tokenQueue);
	DBuffer_init(&nameDBuffer, DBUFFER_INIT_TINY);
	DBuffer_init(&fullDirDBuffer, DBUFFER_INIT_TINY);

	break_tokens_preserve_space(&tokenQueue, linePtr);
	while (tokenQueue.len != 0) {
		tokenType = token_dequeue(&tokenQueue, tokenBuffer, MAX_STATIC_BUFFER_SIZE);
		if ((tokenType == Token_Space) || (tokenType == Token_Tab)) {
			continue;
		}
		assert(tokenType == Token_String);

		if (strcmp(tokenBuffer, "<DIR>") == STR_MATCH) {

			int spaceCounter = 0;
			do {
				// Skip leading space
				tokenType = token_dequeue(&tokenQueue, tokenBuffer, MAX_STATIC_BUFFER_SIZE);
				spaceCounter++;
			} while (
							((tokenType == Token_Space ) || (tokenType == Token_Tab))
							&& (spaceCounter <= 10)
				);

			if (tokenType == Token_Empty) {
				// TODO : This is the case where there's no name on a line
				// TODO : figure out
				break;
			}

			while (tokenType != Token_Empty) {
				switch (tokenType) {
				case (Token_String) :
						DBuffer_append(&nameDBuffer, tokenBuffer);
						break;

				case (Token_Space) :
						DBuffer_append(&nameDBuffer, " ");
						break;

				case (Token_Tab) :
						DBuffer_append(&nameDBuffer, "\t");
						break;

				default :
						assert(false);
				}
				tokenType = token_dequeue(&tokenQueue, tokenBuffer, MAX_STATIC_BUFFER_SIZE);
			}

			if (nameDBuffer.len == 0) {
				break;
			}
			else {
				foundName = true;
			}

			DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&state->itemDBuffer));
			DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&nameDBuffer));
			if (DBuffer_start_ptr(&fullDirDBuffer)[fullDirDBuffer.len - 1] != '/') {
				DBuffer_append(&fullDirDBuffer, "/");
			}

			if (is_useful_dir(DBuffer_start_ptr(&fullDirDBuffer))) {
				str_enqueue(&state->dirQueue, DBuffer_start_ptr(&fullDirDBuffer));
				log_info(
								"parse-list-data-windows",
								"%s: Adding directory name : |%s|",
								state->ip_address_str,
								DBuffer_start_ptr(&fullDirDBuffer)
				);
			}
			else {
				log_info(
								"parse-list-data-windows",
								"%s: Ignoring un-useful dir : |%s|",
								state->ip_address_str,
								DBuffer_start_ptr(&fullDirDBuffer)
				);

			}
			break;
		}
	}

	if (!foundName) {
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_2(
															state,
															"Parsed a Windows line with no name field -- Path : %s -- Line : %s",
															DBuffer_start_ptr(&state->itemDBuffer),
															state->itemDBuffer.len,
															linePtr,
															strlen(linePtr)
		);
	}

	token_queue_destroy(&tokenQueue);
	DBuffer_destroy(&nameDBuffer);
	DBuffer_destroy(&fullDirDBuffer);
}

bool parse_list_data_windows(hoststate_t* state, char* strBuffer) {
	log_trace("parse-list-data-windows", "%s", state->ip_address_str);
	str_queue_t lineQueue;
	char lineSBuffer[MAX_STATIC_BUFFER_SIZE];

	str_queue_init_custom_buffer_size(&lineQueue, MAX_STATIC_BUFFER_SIZE);

	break_lines(&lineQueue, strBuffer);

	while (lineQueue.len != 0) {
		str_dequeue(&lineQueue, lineSBuffer, MAX_STATIC_BUFFER_SIZE);
		log_info(
						"parse-list-data-windows",
						"%s: Parsing line |%s|",
						state->ip_address_str,
						lineSBuffer
		);
		if (strstr(lineSBuffer, "<DIR>")) {
			parse_windows_dir_name(state, lineSBuffer);
		}
	}

	str_queue_destroy(&lineQueue);
	return true;
}
bool is_vxworks_size(char* sizeStr) {
	size_t i = 0;
	size_t len = strlen(sizeStr);

	if (len == 0) {
		return false;
	}

	for (i = 0; i < len; i++) {
		if (!isdigit(sizeStr[i])) {
			return false;
		}
	}
	return true;
}

bool is_vxworks_date(char* dateStr) {
	char month[4];

	if (strlen(dateStr) != 11) {
		return false;
	}

	memcpy(month, dateStr, 3);
	month[3] = '\0';
	if (
			(!is_month_str(month))
			|| (dateStr[3] != '-')
			|| (!isdigit(dateStr[4]))
			|| (!isdigit(dateStr[5]))
			|| (dateStr[6] != '-')
			|| (!isdigit(dateStr[7]))
			|| (!isdigit(dateStr[8]))
			|| (!isdigit(dateStr[9]))
			|| (!isdigit(dateStr[10]))
	) {
		return false;
	}
	return true;
}

bool is_vxworks_time(char* timeStr) {
	if (strlen(timeStr) != 8) {
		return false;
	}

	if (
			(!isdigit(timeStr[0]))
			|| (!isdigit(timeStr[1]))
			|| (timeStr[2] != ':')
			|| (!isdigit(timeStr[3]))
			|| (!isdigit(timeStr[4]))
			|| (timeStr[5] != ':')
			|| (!isdigit(timeStr[6]))
			|| (!isdigit(timeStr[7]))
	) {
		return false;
	}
	return true;
}

void parse_vxworks_dir_name(
		hoststate_t* state,
		char* lineSBuffer
) {
	TokenQueue_t tokenQueue;
	DBuffer_t nameDBuffer;
	DBuffer_t fullDirDBuffer;
	char sBuffer[MAX_STATIC_BUFFER_SIZE];
	TokenType_e tokenType = Token_Unset;
	bool errorFound = false;

	DBuffer_init(&fullDirDBuffer, DBUFFER_INIT_TINY);
	DBuffer_init(&nameDBuffer, DBUFFER_INIT_TINY);
	token_queue_init(&tokenQueue);

	break_tokens_preserve_space(&tokenQueue, lineSBuffer);

	token_queue_iter_begin(&tokenQueue);

	tokenType = token_queue_iter_next_string(
																					&tokenQueue,
																					sBuffer,
																					MAX_STATIC_BUFFER_SIZE
	);
	if ((tokenType != Token_String) || (!is_vxworks_size(sBuffer))) {
		errorFound = true;
		goto CLEANUP;
	}

	tokenType = token_queue_iter_next_string(
																					&tokenQueue,
																					sBuffer,
																					MAX_STATIC_BUFFER_SIZE
	);
	if ((tokenType != Token_String) || (!is_vxworks_date(sBuffer))) {
		errorFound = true;
		goto CLEANUP;
	}

	tokenType = token_queue_iter_next_string(
																					&tokenQueue,
																					sBuffer,
																					MAX_STATIC_BUFFER_SIZE
	);
	if ((tokenType != Token_String) || (!is_vxworks_time(sBuffer))) {
		errorFound = true;
		goto CLEANUP;
	}

	// Skip leading space
	do {
		tokenType = token_queue_iter_next(
																			&tokenQueue,
																			sBuffer,
																			MAX_STATIC_BUFFER_SIZE
		);
	} while ((tokenType == Token_Space) || (tokenType == Token_Tab));

	if (tokenType != Token_String) {
		errorFound = true;
		goto CLEANUP;
	}

	do {
		switch (tokenType) {
		case (Token_String) :
				if (strcmp(sBuffer, "<DIR>") != STR_MATCH) {
					DBuffer_append(&nameDBuffer, sBuffer);
				}
				break;

		case (Token_Space) :
				DBuffer_append(&nameDBuffer, " ");
			break;

		case (Token_Tab) :
				DBuffer_append(&nameDBuffer, "\t");
			break;

		default :
			assert(false);
		}

		tokenType = token_queue_iter_next(
																			&tokenQueue,
																			sBuffer,
																			MAX_STATIC_BUFFER_SIZE
		);
	} while (tokenType != Token_Empty);

	if (nameDBuffer.len == 0) {
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_2(
															state,
															"Parsed a VxWorks directory line with no field -- Path : %s -- Line : %s",
															DBuffer_start_ptr(&state->itemDBuffer),
															state->itemDBuffer.len,
															lineSBuffer,
															strlen(lineSBuffer)
		);
		log_info(
						"parse-unix-link-name",
						"Parsed a Linux link line with no name field %s: Dir : %s, Line : %s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->itemDBuffer),
						lineSBuffer
		);
		goto CLEANUP;
	}

	// Remove trailing spaces
	while (
				(DBuffer_start_ptr(&nameDBuffer)[nameDBuffer.len - 1] == ' ')
				|| (DBuffer_start_ptr(&nameDBuffer)[nameDBuffer.len - 1] == '\t')
	) {
		DBuffer_remove_bytes(&nameDBuffer, 1);
	}

	if (
			(DBuffer_string_matches(&nameDBuffer, "."))
			|| (DBuffer_string_matches(&nameDBuffer, ".."))
	) {
		log_info(
						"parse-vxworks-dir-name",
						"%s: Ignoring un-useful base dir : |%s|",
						state->ip_address_str,
						DBuffer_start_ptr(&nameDBuffer)
		);
		goto CLEANUP;
	}

	DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&state->itemDBuffer));
	DBuffer_append(&fullDirDBuffer, DBuffer_start_ptr(&nameDBuffer));
	if (DBuffer_start_ptr(&fullDirDBuffer)[fullDirDBuffer.len - 1] != '/') {
		DBuffer_append(&fullDirDBuffer, "/");
	}

	if (is_useful_dir(DBuffer_start_ptr(&fullDirDBuffer))) {
		str_enqueue(&state->dirQueue, DBuffer_start_ptr(&fullDirDBuffer));
		log_info(
						"parse-vxworks-dir-name",
						"%s: Adding directory name : |%s|",
						state->ip_address_str,
						DBuffer_start_ptr(&fullDirDBuffer)
		);
	}
	else {
		log_info(
						"parse-vxworks-dir-name",
						"%s: Ignoring unuseful dir : |%s|",
						state->ip_address_str,
						DBuffer_start_ptr(&fullDirDBuffer)
		);
	}

CLEANUP :
	if (errorFound) {
		record_misc_partial_raw_2(
															state,
															"Error parsing VxWorks line -- Path : %s -- Line : %s",
															DBuffer_start_ptr(&state->itemDBuffer),
															state->itemDBuffer.len,
															lineSBuffer,
															strlen(lineSBuffer)
		);
		log_info(
						"parse-vxworks-dir-name",
						"%s: Error parsing VxWorks line -- Path : %s -- Line : %s",
						state->ip_address_str,
						DBuffer_start_ptr(&state->itemDBuffer),
						lineSBuffer
		);
	}
	DBuffer_destroy(&nameDBuffer);
	DBuffer_destroy(&fullDirDBuffer);
	token_queue_destroy(&tokenQueue);
}

bool parse_list_data_vxworks(hoststate_t* state, char* listData) {
	log_trace("parse-list-data-vxworks", "%s", state->ip_address_str);
	str_queue_t lineQueue;
	char lineSBuffer[DBUFFER_INIT_LARGE];
	char firstLine[DBUFFER_INIT_LARGE];

	str_queue_init_custom_buffer_size(&lineQueue, DBUFFER_INIT_LARGE);

	break_lines(&lineQueue, listData);
	str_dequeue(&lineQueue, firstLine, DBUFFER_INIT_LARGE);
	// Check for VxWorks's "Con't open  "~~~~~"" error
	if (strncmp(firstLine, VX_WORKS_ERR_OPEN, VX_WORKS_ERR_OPEN_LEN) == STR_MATCH) {
		log_debug(
							"parse-list-data-vxworks",
							"%s: VxWorks unable to open dir '%s'",
							state->ip_address_str,
							firstLine
		);
		goto CLEANUP;
	}

	// Ensure that it's VxWorks's column header line
	if (strncmp(firstLine, VX_WORKS_HEADER, VX_WORKS_HEADER_LEN) != STR_MATCH) {
		log_info(
						"parse-list-data-vxworks",
						"%s: Column header line is wrong",
						state->ip_address_str
		);
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_1(
															state,
															"List data missing header line -- %s",
															firstLine,
															strlen(firstLine)
		);
		goto CLEANUP;
	}

	// Ensure that it's VxWorks's column header separator line
	str_dequeue(&lineQueue, firstLine, DBUFFER_INIT_LARGE);
	if (strncmp(firstLine, VX_WORKS_HEADER_SEP, VX_WORKS_HEADER_SEP_LEN) != STR_MATCH) {
		log_info(
						"parse-list-data-vxworks",
						"%s: Column header separator line is wrong",
						state->ip_address_str
		);
		state->interestMask |= INTEREST_CHECK_MISC;
		record_misc_partial_raw_1(
															state,
															"List data missing header separator line -- %s",
															firstLine,
															strlen(firstLine)
		);
		goto CLEANUP;
	}


	while (lineQueue.len != 0) {
		str_dequeue(&lineQueue, lineSBuffer, DBUFFER_INIT_LARGE);
		if (strstr(lineSBuffer, "<DIR>")) {
			log_debug(
								"parse-list-data-vxworks",
								"%s: Parsing line |%s|",
								state->ip_address_str,
								lineSBuffer
			);
			parse_vxworks_dir_name(state, lineSBuffer);
		}
	}

CLEANUP :
	str_queue_destroy(&lineQueue);
	return true;
}

bool begins_with_linux_permissions(char* string) {
	if (string == NULL) {
		return false;
	}

	if (
			(memcmp(string, "total", sizeof("total") - 1) == STR_MATCH)
			|| (memcmp(string, "Total", sizeof("Total") - 1) == STR_MATCH)
	) {
		return true;
	}

	switch(string[0]) {
	case ('-') :
	case ('l') :
	case ('d') :
			return true;
	default :
			return false;
	}
}

void check_lie(hoststate_t* state, char* strBuffer) {
	if (state->provedHonest) {
		return;
	}

	log_trace("check-lie", "%s", state->ip_address_str);
	unsigned char newHash[SHA_DIGEST_LENGTH];
	SHA1((unsigned char*) strBuffer, strlen(strBuffer), newHash);

	if (!state->lieHashSet) {
		log_debug("check-lie", "%s: Setting lie hash", state->ip_address_str);
		memcpy(state->lieHash, newHash, SHA_DIGEST_LENGTH);
		state->lieCount = 0;
		state->lieHashSet = true;
		return;
	}

	if (memcmp(newHash, state->lieHash, SHA_DIGEST_LENGTH) == STR_MATCH) {
		state->lieCount++;
		log_debug(
							"check-lie",
							"%s: Host lied #%d",
							state->ip_address_str,
							state->lieCount
		);
	}
	else {
		state->provedHonest = true;
		log_debug("check-lie", "%s: Host proven honest", state->ip_address_str);
	}
}

ListType_e id_list_type(hoststate_t* state, char* string) {
	if (state->destType == VXWORKS) {
		if (begins_with_linux_permissions(string)) {
			state->destType = UNIX;
			state->interestMask |= INTEREST_CHECK_MISC;
			record_misc_string(state, "Reported as VxWorks but has Linux LIST style");
			return List_Linux;
		}
		else {
			return List_VxWorks;
		}
	}
	else if (state->destType == UNIX) {
		if (strstr(string, "<DIR>")) {
			state->interestMask |= INTEREST_CHECK_MISC;
			record_misc_string(state, "Reported as Unix but has Windows LIST style");
			return List_Windows;
		}
		else {
			return List_Linux;
		}
	}
	else if (state->destType == WINDOWS) {
		if (begins_with_linux_permissions(string)) {
			state->destType = UNIX;
			state->interestMask |= INTEREST_CHECK_MISC;
			record_misc_string(state, "Reported as Windows but has Unix LIST style");
			return List_Linux;
		}
		else {
			return List_Windows;
		}
	}
	else {
		if (strstr(string, "<DIR>")) {
			state->interestMask |= INTEREST_CHECK_MISC;
			record_misc_string(state, "Heuristically guessing Windows LIST parsing");
			return List_Windows;
		}
		else {
			state->interestMask |= INTEREST_CHECK_MISC;
			record_misc_string(state, "Heuristically guessing Unix LIST parsing");
			return List_Linux;
		}
	}
}

bool parse_list_data(hoststate_t* state, char* strBuffer) {
	log_trace("parse-list-data", "%s", state->ip_address_str);

	if (gconfig.crawlType == SHORT_CRAWL) {
		return true;
	}

	check_lie(state, strBuffer);
	ListType_e listType = id_list_type(state, strBuffer);
	switch (listType) {
	case (List_Windows) :
			return parse_list_data_windows(state, strBuffer);

	case (List_VxWorks) :
			return parse_list_data_vxworks(state, strBuffer);

	case (List_Linux) :
	default :
			return parse_list_data_unix(state, strBuffer);
	}
}
