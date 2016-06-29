#ifndef OUTPUTDB_H
#define OUTPUTDB_H

#include "DBuffer.h"
#include "ftpEnumerator.h"
#include "dbKeys.h"

typedef struct OutputDBValue_t {
	uint32_t						reqNum;
	uint32_t						auxLen;
	void*								auxData;
	uint32_t						baseLen;
	void*								baseData;
} OutputDBValue_t;

void output_db_init(char* dbPath);
void output_db_close();
void output_db_put_dbuffer(
													hoststate_t* state,
													db_key keyIndex,
													DBuffer_t* inDBuffer
);
void output_db_put_dbuffer_with_aux(
													hoststate_t* state,
													db_key keyIndex,
													DBuffer_t* inDBuffer,
													void* inAuxData,
													uint32_t inAuxLen
);
void output_db_put_string(
											hoststate_t* state,
											db_key keyIndex,
											char* stringFmt,
											...
);
void output_db_put_string_with_aux(
											hoststate_t* state,
											db_key keyIndex,
											void* inAuxData,
											uint32_t inAuxLen,
											char* stringFmt,
											...
);
void output_db_put_bytes(
											hoststate_t* state,
											db_key keyIndex,
											void* bytes,
											size_t numBytes
);
//void sync_db(evutil_socket_t fd, short triggerEvent, void* args);

#endif
