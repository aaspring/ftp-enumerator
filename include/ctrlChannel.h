#ifndef CTRLCHANNEL_H
#define CTRLCHANNEL_H

#include "ftpEnumerator.h"

void ctrl_init(hoststate_t* state);
int create_nb_socket();
int extractBufferString(
												struct bufferevent* bev,
												char* strBuffer,
												hoststate_t* state,
												size_t bufferSize
);
void walk_dir_init(struct bufferevent* bev, void* args);
void walk_dir(struct bufferevent* bev, void* args);
void short_scan_1(struct bufferevent* bev, hoststate_t* state);
void ctrl_connect(hoststate_t* state);

#endif
