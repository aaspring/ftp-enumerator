#ifndef STRQUEUE_H
#define STRQUEUE_H

#include <stdint.h>

#include "../include/magicNumbers.h"

// linked list node for in-memory processing queue
typedef struct str_queue_node {
	char*									str;
	struct str_queue_node* next;
	struct str_queue_node* prev;
} str_queue_node_t;

// queue of entries. should be managed via enqueue/dequeue
// for thread safe operation
typedef struct str_queue {
	str_queue_node_t*	first;
	str_queue_node_t* last;
	str_queue_node_t*	curIter;
	uint32_t 					len;
	uint32_t					bufferSize;
} str_queue_t;

void str_enqueue(str_queue_t* queue, char* newStr);
void str_dequeue(str_queue_t* queue, char* strBuffer, size_t strBufferSize);
void str_queue_init_custom_buffer_size(str_queue_t* queue, uint32_t bufferSize);
void str_queue_init(str_queue_t* queue);
void str_queue_destroy(str_queue_t* queue);

// Iterator-like functionality
void str_queue_iter_begin(str_queue_t* queue);
bool str_queue_iter_has_next(str_queue_t* queue);
void str_queue_iter_next(str_queue_t* queue, char* sBuffer, size_t sBufferSize);
void str_queue_iter_end(str_queue_t* queue);
void str_queue_iter_rewind(str_queue_t* queue);

#endif
