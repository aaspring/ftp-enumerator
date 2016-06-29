#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>
#include <fnmatch.h>
#include <stdio.h>
#include <string.h>

#include "../include/robotParser.h"
#include "../include/logger.h"
#include "../include/magicNumbers.h"
#include "../include/DBuffer.h"
#include "../include/strQueue.h"
#include "../include/textParseHelper.h"

#include "robotStructs.h"

#define MAX_LINE_SIZE 1024
#define FNMATCH_NO_FLAGS 0
#define FNMATCH_MATCH 0

void patternize(const char* base, char* pattern, size_t patternSize) {
	log_trace("patternize", "%s", base);
	char* pre = NULL;
	char* temp = malloc(patternSize);

  if (base[0] != '/') {
  	pre = "/";
  }
  else {
  	pre = "";
  }

  snprintf(temp, patternSize, "%s%s", pre, base);

  if (base[strlen(base) - 1] == '$') {
  	int numChar = snprintf(pattern, patternSize, "%s", temp);
  	pattern[numChar - 1] = '\0';
  }
  else {
  	snprintf(pattern, patternSize, "%s*", temp);
  }
  free(temp);
}

int user_agent_compare(char* testUserAgent, const char* knownUserAgent) {
	log_trace("user-agent-compare", "%s -- %s", testUserAgent, knownUserAgent);
	size_t i = 0;
	int numMatch = 0;
	size_t knownLen = strlen(knownUserAgent);
	to_lower(testUserAgent);

	for (i = 0; i < knownLen; i++) {
			if (testUserAgent[i] == knownUserAgent[i]) {
				numMatch++;
				continue;
			}
			else {
				return numMatch;
			}
	}
	return numMatch;
}

void jump_white_space(char** ptr) {
	log_trace("jump-white-space", "%s", *ptr);
	while(
			(**ptr == ' ')
			|| (**ptr == '\t')
	) {
		(*ptr)++;
	}
}

void clean_lines(str_queue_t* lineQueue, str_queue_t* cleanLinesQueue) {
	log_trace("clean-lines", "");
	char lineSBuff[MAX_LINE_SIZE];
	char* newEoL = NULL;
	char* startPtr = NULL;

	while(lineQueue->len != 0) {
		str_dequeue(lineQueue, lineSBuff, MAX_LINE_SIZE);

		// Remove comments
		newEoL = strchr(lineSBuff, '#');
		if (newEoL) {
			*newEoL = '\0';
		}

		// Skip leading white-space
		startPtr = lineSBuff;
		jump_white_space(&startPtr);

		// Ignore if empty line TODO : Make the size of the smallest valid line?
		if (strlen(startPtr) == 0) {
			continue;
		}

		str_enqueue(cleanLinesQueue, startPtr);
	}
}

FieldType_e field_type(char* possField) {
	log_trace("field-type", "%s", possField);
	if (strcasestr(possField, "disallow")) {
		return GROUP_MEMBER_DENY;
	}
	if (strcasestr(possField, "allow")) {
		return GROUP_MEMBER_ALLOW;
	}
	else if (strcasestr(possField, "user-agent")) {
		return START_OF_GROUP;
	}
	else {
		return UNK_TYPE;
	}
}

void parse_lines(RecordQueue_t* allRecords, str_queue_t* lines) {
	log_trace("parse-lines", "");
	char wholeLine[MAX_LINE_SIZE];
	char strippedLine[MAX_LINE_SIZE];
	char* tok = NULL;
	char* field = NULL;
	char* value = NULL;
	Record_t* newRecord;
	char* tokenIter = NULL;

	str_queue_iter_begin(lines);
	while (str_queue_iter_has_next(lines)) {
		str_queue_iter_next(lines, wholeLine, MAX_LINE_SIZE);
		strippedLine[0] = '\0';
		tokenIter = wholeLine;
		int foundCount = 0;

		tok = strsep(&tokenIter, TOKEN_SEP);

		while (tok != NULL) {
			if (tok[0] != '\0') {
				strlcat(strippedLine, tok, MAX_LINE_SIZE);
				foundCount++;
				if (foundCount == 3) {
					break;
				}
			}

			tok = strsep(&tokenIter, TOKEN_SEP);
		}

		// Now we have a string of unspaced <= 3 tokens "<field>:<value>"

		tokenIter = strippedLine;

		field = strsep(&tokenIter, ":");
		if (tokenIter == NULL) {
			// Not of form "<field>:<value>"
			continue;
		}

		FieldType_e type = field_type(field);
		if (type == UNK_TYPE) {
			continue;
		}
		value = tokenIter;

		newRecord = create_record(type, value);
		record_enqueue(allRecords, newRecord);
	}
	str_queue_iter_end(lines);

}

void extract_applicable_records(
		RobotObj_t* robotObj,
		const char* userAgent,
		RecordQueue_t* allRecords,
		RecordQueue_t* applicRecords
) {
	log_trace("extract-applicable-records", "");
	Record_t* holder = NULL;
	int maxMatchNum = -1;
	Record_t* maxMatch = NULL;
	Record_t* starHolder = NULL;
	bool hasUserAgent = false;

	record_queue_iter_begin(allRecords);

	while (record_queue_iter_has_next(allRecords)) {
		holder = record_queue_iter_next(allRecords);
		assert(holder->type != UNK_TYPE);
		if (
				(holder->type == GROUP_MEMBER_ALLOW)
				|| (holder->type == GROUP_MEMBER_DENY)
		) {
			continue;
		}

		hasUserAgent = true;

		int numMatch = user_agent_compare(holder->value, userAgent);
		log_debug(
							"extract-applicable-records",
							"%s == %d",
							holder->value, numMatch
		);
		if (numMatch > maxMatchNum) {
			maxMatchNum = numMatch;
			maxMatch = holder;
		}
		if (strcmp(holder->value, "*") == STR_MATCH) {
			starHolder = holder;
			log_debug("extract-applicable-records", "Defaulting to the '*' group");
		}
	}

	if ((maxMatchNum == 0) && (starHolder != NULL)) {
		maxMatch = starHolder;
	}

	if (!hasUserAgent) {
		record_queue_iter_end(allRecords);
		return;
	}

	assert(maxMatch != NULL);
	log_info(
					"extract-applicable-records",
					"Best user-agent match -- %s",
					maxMatch->value
	);

	record_queue_iter_end(allRecords);

	DBuffer_append(&robotObj->groupDBuffer, "user-agent : ");
	DBuffer_append(&robotObj->groupDBuffer, maxMatch->value);
	DBuffer_append(&robotObj->groupDBuffer, "\r\n");

	while (allRecords->len != 0) {
		// Find the best matching Start-of-Group
		holder = record_dequeue(allRecords);
		if (holder != maxMatch) {
			destroy_record(holder);
		}
		else {
			break;
		}
	}

	// holder is now the best matching Start-of-Group

	destroy_record(holder);
	holder = record_dequeue(allRecords);
	while ((holder) && (holder->type == START_OF_GROUP)) {
		// Account for layered start-of-groups with the same members
		destroy_record(holder);
		holder = record_dequeue(allRecords);
	}

	// holder is the first of the members

	while (
				(holder)
				&& (
						(holder->type == GROUP_MEMBER_ALLOW)
						|| (holder->type == GROUP_MEMBER_DENY)
				)
	) {
		record_enqueue(applicRecords, holder);
		if (holder->type == GROUP_MEMBER_ALLOW) {
			DBuffer_append(&robotObj->groupDBuffer, "allow : ");
		}
		else {
			DBuffer_append(&robotObj->groupDBuffer, "disallow : ");
		}
		DBuffer_append(&robotObj->groupDBuffer, holder->value);
		DBuffer_append(&robotObj->groupDBuffer, "\r\n");
		holder = record_dequeue(allRecords);
	}

	// holder is now anything after the applicable members

	while (holder) {
		destroy_record(holder);
		holder = record_dequeue(allRecords);
	}
}

bool robot_parser_init(
		RobotObj_t* robotObj,
		char* robotsTxt,
		const char* userAgent
) {
	log_trace("robo-parser-init", "user-agent : %s", userAgent);
	log_trace("robo-parser-init", "robots.txt : %s", robotsTxt);

	memset(robotObj, 0, sizeof(RobotObj_t));
	DBuffer_init(&robotObj->groupDBuffer, DBUFFER_INIT_LARGE);

	size_t fullSize = strlen(robotsTxt);
	if (fullSize == 0) {
		robotObj->noRobotsFound = true;
		return true;
	}
	else {
		robotObj->noRobotsFound = false;
	}

	char patternLine[MAX_LINE_SIZE + 3];
	str_queue_t lineQueue;
	str_queue_t cleanLinesQueue;
	RecordQueue_t* allRecords = NULL;
	RecordQueue_t* applicRecords = NULL;
	Record_t* holder = NULL;
	Record_t* temp = NULL;
	char* robotsTxtCopy = strdup(robotsTxt);

	// Sanity check to make sure user-agent is lower case
	size_t len = strlen(userAgent);
	size_t i = 0;
	for (i = 0; i < len; i++) {
		assert((userAgent[i] > 0x5a) || (userAgent[i] < 0x41));
	}

	str_queue_init_custom_buffer_size(&lineQueue, MAX_LINE_SIZE);
	str_queue_init_custom_buffer_size(&cleanLinesQueue, MAX_LINE_SIZE);


	break_lines(&lineQueue, robotsTxtCopy);

	free(robotsTxtCopy);
	robotsTxtCopy = NULL;

	clean_lines(&lineQueue, &cleanLinesQueue);
	str_queue_destroy(&lineQueue);

	allRecords = record_queue_init();
	parse_lines(allRecords, &cleanLinesQueue);
	if (allRecords->len == 0) {
		log_info("robo-parser-init", "No useful records for handling robots");
		record_queue_destroy(allRecords);
		robotObj->compareQueue = record_queue_init();
		return true;
	}

	applicRecords = record_queue_init();
	extract_applicable_records(robotObj, userAgent, allRecords, applicRecords);
	record_queue_destroy(allRecords);

	robotObj->compareQueue = record_queue_init();
	while (applicRecords->len != 0) {
		holder = record_dequeue(applicRecords);
		patternize(holder->value, patternLine, MAX_LINE_SIZE + 3);
		temp = create_record(holder->type, patternLine);
		temp->len = strlen(holder->value);
		if (strchr(holder->value, '*')) {
			temp->len--;
		}
		record_enqueue(
									robotObj->compareQueue,
									temp
		);
		destroy_record(holder);
	}
	record_queue_destroy(applicRecords);

	return true;
}

bool robot_parser_destroy(RobotObj_t* robotObj) {
	return false;
}

bool access_is_allowed(RobotObj_t* robotObj, char* testPath) {
	log_trace("access-is-allowed", "Checking |%s|", testPath);

	if (robotObj->noRobotsFound) {
		return true;
	}

	Record_t* holder = NULL;
	size_t bestMatchSize = 0;
	Record_t* bestMatch = NULL;

	record_queue_iter_begin(robotObj->compareQueue);
	while (record_queue_iter_has_next(robotObj->compareQueue)) {
		holder = record_queue_iter_next(robotObj->compareQueue);
		if (fnmatch(holder->value, testPath, FNMATCH_NO_FLAGS) == FNMATCH_MATCH) {
			log_trace("access-is-allowed", "%s matching %s", holder->value, testPath);
			if (holder->len > bestMatchSize) {
				bestMatchSize = holder->len;
				bestMatch = holder;
			}
		}
	}
	record_queue_iter_end(robotObj->compareQueue);

	if (!bestMatch) {
		// Default case (allowed)
		return true;
	}
	else if (bestMatch->type == GROUP_MEMBER_ALLOW) {
		return true;
	}
	else {
		return false;
	}
}
