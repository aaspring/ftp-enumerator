#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "../include/magicNumbers.h"
#include "../include/logger.h"

#include "listParseHelper.h"

static inline size_t size_t_min(size_t one, size_t two) {
	if (two < one) {
		return two;
	}
	else {
		return one;
	}
}

void add_token(TokenQueue_t* queue, TokenQueueNode_t* newNode) {

	if (!queue->first) {
		queue->first = newNode;
	} else {
		queue->last->next = newNode;
		newNode->prev = queue->last;
	}

	queue->last = newNode;
	queue->len++;
}

void token_queue_init(TokenQueue_t* queue) {
	memset(queue, 0, sizeof(TokenQueue_t));
}

void token_enqueue_string(TokenQueue_t* queue, char* newString) {
	TokenQueueNode_t* new = malloc(sizeof(TokenQueueNode_t));
	assert(new);
	new->type = Token_String;
	new->str = strdup(newString);
	assert(new->str);
	new->next = NULL;
	new->prev = NULL;

	add_token(queue, new);
}

void token_enqueue_space(TokenQueue_t* queue) {
	TokenQueueNode_t* new = malloc(sizeof(TokenQueueNode_t));
	assert(new);
	new->type = Token_Space;
	new->next = NULL;
	new->prev = NULL;
	add_token(queue, new);
}

void token_enqueue_tab(TokenQueue_t* queue) {
	TokenQueueNode_t* new = malloc(sizeof(TokenQueueNode_t));
	assert(new);
	new->type = Token_Tab;
	new->next = NULL;
	new->prev = NULL;
	add_token(queue, new);
}

TokenType_e token_dequeue(
		TokenQueue_t* queue,
		char* strBuffer,
		size_t bufferSize
) {
	assert(bufferSize >= 1);
	TokenType_e retType = Token_Empty;
	TokenQueueNode_t* retNode = queue->first;

	if (!retNode) {
		strBuffer[0] = '\0';
		return Token_Empty;
	}

	switch (retNode->type) {
	case (Token_Space) :
			strBuffer[0] = '\0';
			retType = Token_Space;
			break;

	case (Token_Tab) :
			strBuffer[0] = '\0';
			retType = Token_Tab;
			break;

	case (Token_String) :
			snprintf(strBuffer, bufferSize, "%s",retNode->str);
			free(retNode->str);
			retType = Token_String;
			break;

	default :
		assert(false);
	}

	TokenQueueNode_t* next = retNode->next;
	if (next) {
		next->prev = NULL;
	}

	free(retNode);

	queue->first = next;
	--queue->len;

	return retType;
}

void token_queue_destroy(TokenQueue_t* queue) {
	char buffer[2];

	while (queue->len != 0) {
		token_dequeue(queue, buffer, 2);
	}
	memset(queue, 0, sizeof(TokenQueue_t));
}

void token_queue_iter_begin(TokenQueue_t* queue) {
	queue->curIter = queue->first;
}

bool token_queue_iter_has_next(TokenQueue_t* queue) {
	if (queue->curIter == NULL) {
		return false;
	}
	return true;
}

TokenType_e token_queue_iter_next(
		TokenQueue_t* queue,
		char* sBuffer,
		size_t bufferSize
) {
	TokenType_e retType = Token_Empty;

	if (queue->curIter == NULL) {
		sBuffer[0] = '\0';
		return Token_Empty;
	}

	switch (queue->curIter->type) {
	case (Token_Space) :
			sBuffer[0] = '\0';
			retType = Token_Space;
			break;

	case (Token_Tab) :
			sBuffer[0] = '\0';
			retType = Token_Tab;
			break;

	case (Token_String) :
			snprintf(sBuffer, bufferSize, "%s", queue->curIter->str);
			retType = Token_String;
			break;

	default :
			assert(false);
	}

	queue->curIter = queue->curIter->next;
	return retType;
}

TokenType_e token_queue_iter_next_string(
		TokenQueue_t* queue,
		char* sBuffer,
		size_t bufferSize
) {
	TokenType_e curType = Token_Unset;
	do {
		curType = token_queue_iter_next(queue, sBuffer, bufferSize);
	} while ((curType == Token_Space) || (curType == Token_Tab));
	return curType;
}

TokenType_e token_queue_iter_rewind_string(TokenQueue_t* queue) {
	if (queue->curIter == NULL) {
		queue->curIter = queue->last;
		while ((queue->curIter) && (queue->curIter->type != Token_String)) {
			queue->curIter = queue->curIter->prev;
		}

		if (queue->curIter) {
			return Token_String;
		}
		else {
			queue->curIter = queue->first;
			return Token_Empty;
		}
	}

	do {
		queue->curIter = queue->curIter->prev;
	} while ((queue->curIter) && (queue->curIter->type != Token_String));

	if (queue->curIter) {
		return Token_String;
	}
	else {
		queue->curIter = queue->first;
		return Token_Empty;
	}
}

void token_queue_iter_end(TokenQueue_t* queue) {
	queue->curIter = NULL;
}

size_t extract_token(char* start, char* end, char* dest, size_t destSize) {
	assert(destSize > 0);
	size_t len = size_t_min(destSize - 1, end - start);

	if (len == 0) {
		dest[0] = '\0';
		return 0;
	}

	memcpy(dest, start, len);
	dest[len] = '\0';
	return len;
}

void break_tokens_preserve_space(TokenQueue_t* queue, char* string) {
	char* basePtr = string;
	char* spacePtr = NULL;
	char* tabPtr = NULL;
	char tokenSBuffer[MAX_STATIC_BUFFER_SIZE];
	size_t numChars = 0;
	size_t totalNumChars = 0;

	while (true) {
		spacePtr = strstr(basePtr, " ");
		tabPtr = strstr(basePtr, "\t");

		if ((!spacePtr) && (!tabPtr)) {
			break;
		}

		if (
				(!spacePtr)
				|| (
						(tabPtr)
						&& (tabPtr < spacePtr)
				)
		) {
			numChars = extract_token(
															basePtr,
															tabPtr,
															tokenSBuffer,
															MAX_STATIC_BUFFER_SIZE
			);
			if (numChars != 0) {
				token_enqueue_string(queue, tokenSBuffer);
			}
			token_enqueue_tab(queue);
			totalNumChars = totalNumChars + numChars + 1;
			basePtr = tabPtr + 1;
		}
		else {
			assert(spacePtr);
			assert((!tabPtr) || (spacePtr < tabPtr));
			numChars = extract_token(
															basePtr,
															spacePtr,
															tokenSBuffer,
															MAX_STATIC_BUFFER_SIZE
			);
			if (numChars != 0) {
				token_enqueue_string(queue, tokenSBuffer);
			}
			token_enqueue_space(queue);
			totalNumChars = totalNumChars + numChars + 1;
			basePtr = spacePtr + 1;
		}
	}

	// Add the last token
	token_enqueue_string(queue, basePtr);
	return;
}
