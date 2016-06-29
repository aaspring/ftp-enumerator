#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <jansson.h>

#include "../include/ftpEnumerator.h"
#include "../include/recorder.h"
#include "../include/logger.h"
#include "../include/recordKeys.h"

#include "base64.h"

#define JSON_NO_FLAGS 0

int encode_raw(const void* dataBuf, size_t dataLen, char* resBuf, size_t resLen) {
	return base64encode(dataBuf, dataLen, resBuf, resLen);
}

void recorder_init(hoststate_t* state) {
	log_trace("recorder-init", "");
	memset(&state->recorder, 0, sizeof(Recorder_t));
	state->recorder.fullObj = json_object();
	state->recorder.rawArray = json_array();
	state->recorder.miscArray = json_array();
}

void recorder_finish(hoststate_t* state) {
	json_object_set_new(
											state->recorder.fullObj,
											"rawdata",
											state->recorder.rawArray
	);
	json_object_set_new(
											state->recorder.fullObj,
											"misc",
											state->recorder.miscArray
	);
	char* full = json_dumps(state->recorder.fullObj, JSON_NO_FLAGS);
	printf("%s\n", full);
	fflush(stdout);
	free(full);
	json_decref(state->recorder.fullObj);
}

void record_raw_dbuffer(
		hoststate_t* state,
		RecKey_e key,
		char* auxData,
		size_t auxLen,
		DBuffer_t* dBuffer
) {
	return record_raw_buffer(
													state,
													key,
													auxData,
													auxLen,
													DBuffer_start_ptr(dBuffer),
													dBuffer->len
	);
}

void record_raw_buffer(
		hoststate_t* state,
		RecKey_e key,
		char* auxData,
		size_t auxLen,
		char* buffer,
		size_t bufferLen
) {
	size_t encodedSize = 0;
	char* encodedBuffer = NULL;

	json_t* obj = json_object();

	json_t* reqNumInt = json_integer(state->numReqSent);
	json_object_set_new(obj, "reqNum", reqNumInt);

	if (auxLen == 0) {
		json_t* auxData = json_null();
		json_object_set_new(obj, "aux", auxData);
	}
	else {
		encodedSize = base64_encode_size(auxLen);
		encodedBuffer = malloc(encodedSize);
		assert(0 == encode_raw(
						auxData,
						auxLen,
						encodedBuffer,
						encodedSize
		));
		json_object_set_new(obj, "aux", json_string(encodedBuffer));
		free(encodedBuffer);
		encodedBuffer = NULL;
		encodedSize = 0;
	}

	encodedSize = base64_encode_size(bufferLen);
	encodedBuffer = malloc(encodedSize);
	assert(0 == encode_raw(
					buffer,
					bufferLen,
					encodedBuffer,
					encodedSize
	));
	json_object_set_new(obj, "base", json_string(encodedBuffer));
	free(encodedBuffer);

	json_object_set_new(obj, "key", json_string(RecKeyStrings[key]));
	json_array_append_new(state->recorder.rawArray, obj);
}

void record_string(hoststate_t* state, RecKey_e key, const char* string) {
	json_object_set_new(
											state->recorder.fullObj,
											RecKeyStrings[key],
											json_string(string)
	);
}

void record_string_sprintf(
		hoststate_t* state,
		RecKey_e key,
		char* format,
		...
) {
	va_list ap;

	va_start(ap, format);
	size_t len = vsnprintf(NULL, 0, format, ap);
	va_end(ap);

	va_start(ap, format);
	char* string = malloc(len + 1);
	vsnprintf(string, len + 1, format, ap);
	va_end(ap);
	record_string(state, key, string);
	free(string);
}

void record_bool(hoststate_t* state, RecKey_e key, bool value) {
	if (value) {
		json_object_set_new(
											state->recorder.fullObj,
											RecKeyStrings[key],
											json_true()
		);
	}
	else {
		json_object_set_new(
											state->recorder.fullObj,
											RecKeyStrings[key],
											json_false()
		);
	}
}

void record_int(hoststate_t* state, RecKey_e key, int anInt) {
	json_object_set_new(
											state->recorder.fullObj,
											RecKeyStrings[key],
											json_integer(anInt)
	);
}

void record_misc_string(hoststate_t* state, char* string) {
	json_array_append_new(state->recorder.miscArray, json_string(string));
}

void record_misc_partial_raw_1(
		hoststate_t* state,
		char* format,
		char* raw,
		size_t rawLen
) {
	assert(strstr(format, "%s"));
	size_t encodedSize = base64_encode_size(rawLen);
	char* encodedBuffer = malloc(encodedSize);
	assert(0 == encode_raw(
					raw,
					rawLen,
					encodedBuffer,
					encodedSize
	));

	size_t joinedLen = snprintf(NULL, 0, format, encodedBuffer) + 1;
	char* joinedBuffer = malloc(joinedLen);
	snprintf(joinedBuffer, joinedLen, format, encodedBuffer);
	json_array_append_new(state->recorder.miscArray, json_string(joinedBuffer));
	free(encodedBuffer);
	free(joinedBuffer);
}

void record_misc_partial_raw_2(
		hoststate_t* state,
		char* format,
		char* raw1,
		size_t rawLen1,
		char* raw2,
		size_t rawLen2
) {
	char* firstFmt = strstr(format, "%s");
	assert(firstFmt++);
	assert(strstr(firstFmt, "%s"));

	size_t encodedSize1 = base64_encode_size(rawLen1);
	char* encodedBuffer1 = malloc(encodedSize1);
	assert(0 == encode_raw(
					raw1,
					rawLen1,
					encodedBuffer1,
					encodedSize1
	));

	size_t encodedSize2 = base64_encode_size(rawLen2);
	char* encodedBuffer2 = malloc(encodedSize2);
	assert(0 == encode_raw(
					raw2,
					rawLen2,
					encodedBuffer2,
					encodedSize2
	));

	size_t joinedLen = snprintf(NULL, 0, format, encodedBuffer1, encodedBuffer2) + 1;
	char* joinedBuffer = malloc(joinedLen);
	snprintf(joinedBuffer, joinedLen, format, encodedBuffer1, encodedBuffer2);
	json_array_append_new(state->recorder.miscArray, json_string(joinedBuffer));

	free(encodedBuffer1);
	free(encodedBuffer2);
	free(joinedBuffer);
}

void record_string_queue(hoststate_t* state, RecKey_e key, str_queue_t* queue) {
	json_t* newArray = json_array();
	char* sBuffer = malloc(queue->bufferSize);

	str_dequeue(queue, sBuffer, queue->bufferSize);
	while (sBuffer[0] != '\0') {
		json_array_append_new(newArray, json_string(sBuffer));
		str_dequeue(queue, sBuffer, queue->bufferSize);
	}
	json_object_set_new(state->recorder.fullObj, RecKeyStrings[key], newArray);

	free(sBuffer);
}

void record_raw_string_queue(
		hoststate_t* state,
		RecKey_e key,
		str_queue_t* queue
) {
	json_t* newArray = json_array();
	char* sBuffer = malloc(queue->bufferSize);
	size_t encodedSize = base64_encode_size(queue->bufferSize);
	char* encodedBuffer = malloc(encodedSize);


	str_dequeue(queue, sBuffer, queue->bufferSize);
	while (sBuffer[0] != '\0') {
		assert(0 == encode_raw(
						sBuffer,
						strlen(sBuffer),
						encodedBuffer,
						encodedSize
		));
		json_array_append_new(newArray, json_string(encodedBuffer));
		str_dequeue(queue, sBuffer, queue->bufferSize);
	}
	json_object_set_new(state->recorder.fullObj, RecKeyStrings[key], newArray);

	free(sBuffer);
	free(encodedBuffer);
}

void record_base_raw_string(hoststate_t* state, RecKey_e key, char* string) {
	size_t baseLen = strlen(string);
	size_t encodedLen = base64_encode_size(baseLen);
	char* encodedBuffer = malloc(encodedLen);

	assert(0 == encode_raw(
					string,
					baseLen,
					encodedBuffer,
					encodedLen
	));

	json_object_set_new(
										state->recorder.fullObj,
										RecKeyStrings[key],
										json_string(encodedBuffer)
	);

	free(encodedBuffer);
}
