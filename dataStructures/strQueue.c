#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "../include/magicNumbers.h"
#include "../include/strQueue.h"
#include "../include/logger.h"

void str_queue_init(str_queue_t* queue) {
	memset(queue, 0, sizeof(str_queue_t));
	queue->bufferSize = MAX_STATIC_BUFFER_SIZE;
}

void str_queue_init_custom_buffer_size(str_queue_t* queue, uint32_t bufferSize) {
	assert(bufferSize != 0);
	memset(queue, 0, sizeof(str_queue_t));
	queue->bufferSize = bufferSize;
}

void str_enqueue(str_queue_t* queue, char* newString) {
	str_queue_node_t *new = malloc(sizeof(str_queue_node_t));
	assert(new);
	new->str = strndup(newString, queue->bufferSize - 1);
	assert(new->str);
	new->next = NULL;
	new->prev = NULL;

	if (!queue->first) {
		queue->first = new;
	} else {
		queue->last->next = new;
		new->prev = queue->last;
	}
	queue->last = new;
	++queue->len;
}

void str_dequeue(str_queue_t* queue, char* strBuffer, size_t strBufferSize) {
	if (!queue->first) {
		strBuffer[0] = '\0';
		return;
	}
	snprintf(strBuffer, strBufferSize, "%s", queue->first->str);

	str_queue_node_t* next = queue->first->next;
	if (next) {
		next->prev = NULL;
	}

	free(queue->first->str);
	free(queue->first);

	queue->first = next;
	--queue->len;
}

void str_queue_destroy(str_queue_t* queue) {
	char* sBuffer = malloc(sizeof(char) * queue->bufferSize);
	while (queue->len != 0) {
		str_dequeue(queue, sBuffer, queue->bufferSize);
	}
	free(sBuffer);
	memset(queue, 0, sizeof(str_queue_t));
}


void str_queue_iter_begin(str_queue_t* queue) {
	queue->curIter = queue->first;
}

bool str_queue_iter_has_next(str_queue_t* queue) {
	if (queue->curIter == NULL) {
		return false;
	}
	return true;
}

void str_queue_iter_next(str_queue_t* queue, char* sBuffer, size_t sBufferSize) {
	if (queue->curIter == NULL) {
		sBuffer[0] = '\0';
		return;
	}
	snprintf(sBuffer, sBufferSize, "%s", queue->curIter->str);
	queue->curIter = queue->curIter->next;
}

void str_queue_iter_rewind(str_queue_t* queue) {
	if (queue->curIter == NULL) {
		return;
	}
	queue->curIter = queue->curIter->prev;
}

void str_queue_iter_end(str_queue_t* queue) {
	queue->curIter = NULL;
}
