#ifndef READ_H
#define READ_H

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "../include/ftpEnumerator.h"

void read_banner(struct bufferevent *bev, void *args);
void read_syst_resp(struct bufferevent *bev, void *args);
void read_user_resp(struct bufferevent* bev, void* args);
void read_quit_resp(struct bufferevent* bev, void* args);
void read_pass_resp(struct bufferevent* bev, void* args);
void read_pwd_resp(struct bufferevent* bev, void* args);
void read_pasv_resp(struct bufferevent* bev, void* args);
void read_list_done(struct bufferevent *bev, void* args);
void read_list_resp(struct bufferevent* bev, void* args);
void read_feat_resp(struct bufferevent* bev, void* args);
void read_stat_resp(struct bufferevent* bev, void* args);
void read_help_resp(struct bufferevent* bev, void* args);
void read_retr_resp(struct bufferevent* bev, void* args);
void read_type_resp(struct bufferevent* bev, void* args);
void read_retr_done(struct bufferevent* bev, void* args);
void recoverable_unexpected_reply(hoststate_t* state, char* auxData);
void handle_early_sec_req(struct bufferevent* bev, hoststate_t* state);
void read_site_resp(struct bufferevent* bev, void* args);
void read_security_connecting(struct bufferevent* bev, void* args);
void handle_extra_resp_error(
		struct bufferevent* bev,
		hoststate_t* state,
		char* functionStr
);
void read_reset_wait(struct bufferevent* bev, hoststate_t* state);





#endif
