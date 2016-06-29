#ifndef CTRLSHARED_H
#define CTRLSHARED_H

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "../include/ftpEnumerator.h"

void disconnect_abrupt(struct bufferevent* bev, hoststate_t* state);
void disconnect_pleasant(struct bufferevent* bev, void* state);
void disconnect_rude(struct bufferevent* bev, void* args);
void delete_all_timers(hoststate_t* state);

#endif
