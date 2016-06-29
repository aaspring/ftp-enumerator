#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include "../include/robotParser.h"

#include "robotStructs.h"

Record_t* create_record(FieldType_e type, char* value) {
	Record_t* record = malloc(sizeof(Record_t));
	record->type = type;
	record->value = strdup(value);
	record->next = NULL;
	return record;
}

void destroy_record(Record_t* record) {
	free(record->value);
	free(record);
}

// For the queue

RecordQueue_t* record_queue_init() {
	RecordQueue_t* temp = malloc(sizeof(RecordQueue_t));
	memset(temp, 0, sizeof(RecordQueue_t));
	return temp;
}

void record_queue_destroy(RecordQueue_t* queue) {
	while (queue->len != 0) {
		destroy_record(record_dequeue(queue));
	}
	free(queue);
}

void record_enqueue(RecordQueue_t* queue, Record_t* record) {
	queue->len++;
	if (queue->first == NULL) {
		queue->first = record;
		queue->last = record;
	}
	else {
		queue->last->next = record;
		queue->last = record;
	}
}

Record_t* record_dequeue(RecordQueue_t* queue) {
	if (queue->len == 0) {
		return NULL;
	}
	Record_t* temp = queue->first;
	queue->len--;
	if (queue->first == queue->last) {
		queue->first = NULL;
		queue->last = NULL;
	}
	else {
		queue->first = queue->first->next;
	}

	if (queue->first == NULL) {
		queue->last = NULL;
	}
	return temp;
}

void record_queue_iter_begin(RecordQueue_t* queue) {
	queue->curIter = queue->first;
}

bool record_queue_iter_has_next(RecordQueue_t* queue) {
	if (queue->curIter == NULL) {
		return false;
	}
	else {
		return true;
	}
}

Record_t* record_queue_iter_next(RecordQueue_t* queue) {
	if (queue->curIter == NULL) {
		return NULL;
	}

	Record_t* temp = queue->curIter;
	queue->curIter = queue->curIter->next;
	return temp;
}

void record_queue_iter_end(RecordQueue_t* queue) {
	queue->curIter = NULL;
}
