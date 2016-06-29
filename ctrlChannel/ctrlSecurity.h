#ifndef TLS_H
#define TLS_H

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "../include/ftpEnumerator.h"

void init_tls_connection(struct bufferevent* bev, hoststate_t* state);
void log_ssl_data(struct bufferevent* bev, hoststate_t* state);
void read_auth_tls_resp(struct bufferevent* bev, void* args);
void read_auth_ssl_resp(struct bufferevent* bev, void* args);
void read_pbsz_resp(struct bufferevent* bev, void* args);
void read_prot_resp(struct bufferevent* bev, void* args);
void send_auth_tls(struct bufferevent* bev, hoststate_t* state);
void send_auth_ssl(struct bufferevent* bev, hoststate_t* state);
void send_pbsz(struct bufferevent* bev, hoststate_t* state);
void setup_ending_optional_sec_check(struct bufferevent* bev, void* args);
void setup_middle_optional_sec_check(struct bufferevent* bev, void* args);

#endif
