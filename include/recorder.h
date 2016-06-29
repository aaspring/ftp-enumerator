#ifndef RECORDER_H
#define RECORDER_H

#include <stdbool.h>
#include <jansson.h>

#include "ftpEnumerator.h"
#include "DBuffer.h"
#include "recordKeys.h"
#include "strQueue.h"

void recorder_init(hoststate_t* state);
void recorder_finish(hoststate_t* state);
void record_raw_dbuffer(
		hoststate_t* state,
		RecKey_e key,
		char* auxData,
		size_t auxLen,
		DBuffer_t* dBuffer
);

void record_raw_buffer(
		hoststate_t* state,
		RecKey_e key,
		char* auxData,
		size_t auxLen,
		char* buffer,
		size_t bufferLen
);
void record_base_raw_string(hoststate_t* state, RecKey_e key, char* string);
void record_bool(hoststate_t* state, RecKey_e key, bool value);
void record_string_sprintf(hoststate_t* state, RecKey_e key, char* format, ...);
void record_string(hoststate_t* state, RecKey_e key, const char* string);
void record_int(hoststate_t* state, RecKey_e key, int anInt);
void record_misc_string(hoststate_t* state, char* string);
void record_string_queue(hoststate_t* state, RecKey_e key, str_queue_t* queue);
void record_raw_string_queue(
		hoststate_t* state,
		RecKey_e key,
		str_queue_t* queue
);
void record_misc_partial_raw_1(
		hoststate_t* state,
		char* format,
		char* raw,
		size_t rawLen
);

void record_misc_partial_raw_2(
		hoststate_t* state,
		char* string,
		char* raw1,
		size_t rawLen1,
		char* raw2,
		size_t rawLen2
);

#endif // RECORDER_H
