#ifndef PARSERESP_H
#define PARSERESP_H

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "../include/ftpEnumerator.h"
#include "../include/DBuffer.h"

bool is_reply_code(char* foundReply, char* expectedReply);
bool is_reply_code_strict(char* foundReply, char* expectedReply);
char* get_last_line(DBuffer_t* dStrBuffer);
RespResult_e parse_response(
																struct bufferevent* bev,
																hoststate_t* state,
																char* foundReplyCode
);
void unexpected_reply(
											struct bufferevent* bev,
											hoststate_t* state,
											int failureCode,
											char* functionName
);
ValidationResult_t fill_DBuffer_line(
																		struct bufferevent* bev,
																		hoststate_t* state,
																		DBuffer_t* dBuffer
);
ValidationResult_t fill_DBuffer_all(
																		struct bufferevent* bev,
																		hoststate_t* state,
																		DBuffer_t* dBuffer

);
void log_misc_ctrlDBuffer(hoststate_t* state, char* auxData);
bool evbuffer_matches(struct evbuffer* evBuffer, char* str);

#endif
