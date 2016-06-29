#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "../include/textParseHelper.h"
#include "../include/strQueue.h"
#include "../include/logger.h"

void to_lower(char* string) {
	size_t len = strlen(string);
	size_t i = 0;

	for (i = 0; i < len; i++) {
		if ((string[i] >= 0x41) && (string[i] <= 0x5a)) {
			string[i] = string[i] + 0x20;
		}
	}
}

void break_lines(str_queue_t* lineQueue, char* chunk) {
	size_t lineLen = 0;
	char* fullBuffer = strdup(chunk);
	char* chunkCopy = fullBuffer;
	assert(chunkCopy);
	char* line = strsep(&chunkCopy, LINE_SEP);

	while (line != NULL) {
		lineLen = strlen(line);

		if (lineLen != 0) {
			if (line[lineLen - 1] == '\r') {
				line[lineLen - 1] = '\0';
				lineLen--;
			}
		}

		if (line[0] != '\0') {
			str_enqueue(lineQueue, line);
		}
		line = strsep(&chunkCopy, LINE_SEP);
	}

	free(fullBuffer);
	return;
}

void break_tokens_compress_space(str_queue_t* tokenQueue, char* chunk) {
	//log_trace("break-tokens", "%s", chunk);
	char* chunkCopy = strdup(chunk);
	assert(chunkCopy);
	char* line = strsep(&chunkCopy, TOKEN_SEP);
	while (line != NULL) {
		if (line[0] != '\0') {
			str_enqueue(tokenQueue, line);
		}
		line = strsep(&chunkCopy, TOKEN_SEP);
	}
	free(chunkCopy);
	return;
}
