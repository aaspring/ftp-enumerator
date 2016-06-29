#ifndef DATACHANNEL_H
#define DATACHANNEL_H

#include "ftpEnumerator.h"

void data_setup_channel(hoststate_t* state);
void data_connect(hoststate_t* state);
void data_tls_init(struct bufferevent* bev, hoststate_t* state);
void data_close_channel(struct bufferevent* bev, hoststate_t* state);

#endif
