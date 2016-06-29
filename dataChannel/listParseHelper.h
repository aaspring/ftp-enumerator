#ifndef LISTPARSEHELPER_H
#define LISTPARSEHELPER_H

#include <stdbool.h>
#include <inttypes.h>

typedef enum {
	Token_Unset,
	Token_Empty,
	Token_String,
	Token_Space,
	Token_Tab,
} TokenType_e;

typedef struct TokenQueueNode {
	char*										str;
	struct TokenQueueNode* 	next;
	struct TokenQueueNode* 	prev;
	TokenType_e							type;
} TokenQueueNode_t;

typedef struct {
	TokenQueueNode_t*	first;
	TokenQueueNode_t* last;
	TokenQueueNode_t*	curIter;
	uint32_t 					len;
} TokenQueue_t;

void token_queue_init(TokenQueue_t* queue);
void token_enqueue_string(TokenQueue_t* queue, char* newString);
void token_enqueue_space(TokenQueue_t* queue);
void token_enqueue_tab(TokenQueue_t* queue);
TokenType_e token_dequeue(
		TokenQueue_t* queue,
		char* strBuffer,
		size_t bufferSize
);
void token_queue_destroy(TokenQueue_t* queue);
void token_queue_iter_begin(TokenQueue_t* queue);
void token_queue_iter_end(TokenQueue_t* queue);
bool token_queue_iter_has_next(TokenQueue_t* queue);
TokenType_e token_queue_iter_next(
		TokenQueue_t* queue,
		char* sBuffer,
		size_t bufferSize
);
TokenType_e token_queue_iter_next_string(
		TokenQueue_t* queue,
		char* sBuffer,
		size_t bufferSize
);
TokenType_e token_queue_iter_rewind_string(TokenQueue_t* queue);
void break_tokens_preserve_space(TokenQueue_t* queue, char* string);

#endif // LISTPARSEHELPER_H
