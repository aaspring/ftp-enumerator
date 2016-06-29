#ifndef BADPORT_H
#define BADPORT_H

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

void read_port_resp(struct bufferevent* bev, void* args);
void send_port(struct bufferevent *bev, void* args);
void read_port_list_done(struct bufferevent *bev, void* args);
void read_port_list_resp(struct bufferevent* bev, void* args);

#endif
