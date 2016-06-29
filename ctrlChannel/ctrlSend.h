#ifndef SEND_H
#define SEND_H

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "../include/ftpEnumerator.h"

void change_state(hoststate_t* state, int newState);
void send_pass(struct bufferevent* bev, hoststate_t* state);
void send_feat(struct bufferevent* bev, hoststate_t* state);
void send_stat(struct bufferevent* bev, hoststate_t* state);
void send_pasv(struct bufferevent *bev, hoststate_t* state);
void send_list(struct bufferevent* bev, void* args);
void send_help(struct bufferevent* bev, hoststate_t* state);
void send_pwd(struct bufferevent *bev, hoststate_t* state);
void send_syst(struct bufferevent* bev, void* args);
void send_quit(struct bufferevent* bev, void* args);
void send_user(struct bufferevent *bev, void* args);
void send_type(struct bufferevent* bev, hoststate_t* state);
void send_retr(struct bufferevent* bev, void* args);
void data_connect(hoststate_t* state);
void sendSpigot(
								struct bufferevent* bev,
								hoststate_t* state,
								int nextState,
								char* format,
								...
);
void send_site(struct bufferevent* bev, void* args);



#endif
