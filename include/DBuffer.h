#ifndef DBUFFER_H
#define DBUFFER_H

#include <stdbool.h>

typedef struct {
	char*										startPtr;
	size_t									len;
	size_t									_size;
} DBuffer_t;

#define DBUFFER_INIT_TINY 64
#define DBUFFER_INIT_SMALL 256
#define DBUFFER_INIT_LARGE 1024

void DBuffer_destroy(DBuffer_t* dBuffer);
void DBuffer_init(DBuffer_t* dBuffer, size_t initSize);
void DBuffer_append(DBuffer_t* dBuffer, char* newPiece);
char* DBuffer_start_ptr(DBuffer_t* dBuffer);
char* DBuffer_start_ptr_offset(DBuffer_t* dBuffer, size_t offset);
void DBuffer_clear(DBuffer_t* dBuffer);
void DBuffer_append_bytes(DBuffer_t* dBuffer, char* newBytes, size_t numBytes);
void DBuffer_remove_bytes(DBuffer_t* dBuffer, size_t numBytes);
bool DBuffer_string_matches(DBuffer_t* dBuffer, const char* string);
bool DBuffer_is_string(DBuffer_t* dBuffer);



#endif
