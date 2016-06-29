#ifndef ROBOTPARSER_H
#define ROBOTPARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "magicNumbers.h"
#include "DBuffer.h"

typedef enum {
	START_OF_GROUP,
	GROUP_MEMBER_ALLOW,
	GROUP_MEMBER_DENY,
	UNK_TYPE
} FieldType_e;

typedef struct Record {
	FieldType_e							type;
	char*										value;
	struct Record*					next;
	size_t									len;
} Record_t;

typedef struct {
	Record_t*								first;
	Record_t*								last;
	Record_t*								curIter;
	uint32_t								len;
} RecordQueue_t;


typedef struct {
	RecordQueue_t*					compareQueue;
	DBuffer_t								groupDBuffer;
	bool										noRobotsFound;
} RobotObj_t;

bool robot_parser_init(
		RobotObj_t* robotObj,
		char* robotTxt,
		const char* userString
);
bool robot_parser_destroy(RobotObj_t* robotObj);
bool access_is_allowed(RobotObj_t* robotObj, char* testPath);





#endif
