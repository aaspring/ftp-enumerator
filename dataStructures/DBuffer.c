#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../include/DBuffer.h"
#include "../include/magicNumbers.h"
#include "../include/logger.h"

void _DBuffer_grow(DBuffer_t* dBuffer) {
	dBuffer->startPtr = realloc(dBuffer->startPtr, 2 * dBuffer->_size);
	dBuffer->_size = 2 * dBuffer->_size;
}

void DBuffer_append(DBuffer_t* dBuffer, char* newPiece) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	size_t newPieceLen = strlen(newPiece);
	DBuffer_append_bytes(dBuffer, newPiece, newPieceLen);
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
}

void DBuffer_destroy(DBuffer_t* dBuffer) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	free(dBuffer->startPtr);
	memset(dBuffer, 0, sizeof(DBuffer_t));
}

void DBuffer_init(DBuffer_t* dBuffer, size_t initSize) {
	dBuffer->startPtr = malloc(initSize);
	dBuffer->startPtr[0] = '\0';
	dBuffer->_size = initSize;
	dBuffer->len = 0;
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
}

char* DBuffer_start_ptr(DBuffer_t* dBuffer) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	return dBuffer->startPtr;
}

char* DBuffer_start_ptr_offset(DBuffer_t* dBuffer, size_t offset) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	return dBuffer->startPtr + offset;
}

void DBuffer_clear(DBuffer_t* dBuffer) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	dBuffer->startPtr[0] = '\0';
	dBuffer->len = 0;
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
}

void DBuffer_append_bytes(DBuffer_t* dBuffer, char* newBytes, size_t numBytes) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	while (numBytes + dBuffer->len > dBuffer->_size - 1) {
		_DBuffer_grow(dBuffer);
	}
	memcpy(dBuffer->startPtr + dBuffer->len, newBytes, numBytes);
	dBuffer->len += numBytes;
	dBuffer->startPtr[dBuffer->len] = '\0';
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
}

void DBuffer_remove_bytes(DBuffer_t* dBuffer, size_t numBytes) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	if (numBytes >= dBuffer->len) {
		return DBuffer_clear(dBuffer);
	}
	dBuffer->startPtr[dBuffer->len - numBytes] = '\0';
	dBuffer->len -= numBytes;
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
}

bool DBuffer_string_matches(DBuffer_t* dBuffer, const char* string) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	return strcmp(dBuffer->startPtr, string) == STR_MATCH;
}

bool DBuffer_is_string(DBuffer_t* dBuffer) {
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	int i = 0;
	for (i = 0; i < dBuffer->len; i++) {
		if (dBuffer->startPtr[i] == '\0') {
			return false;
		}
	}
	assert(dBuffer->startPtr[dBuffer->len] == '\0');
	return true;
}
