#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include "../include/logger.h"

#define PORT 1234
#define MAX_STATIC_BUFFER_SIZE 1024

struct timeval readTimer;

static void read_cb(struct bufferevent* bev, void* args) {
	char* ipAddrStr = (char*) args;
	struct evbuffer* input = bufferevent_get_input(bev);
	size_t numChar = 0;
	char sBuffer[MAX_STATIC_BUFFER_SIZE];

	while (evbuffer_get_length(input) != 0) {
		numChar = evbuffer_remove(input, sBuffer, MAX_STATIC_BUFFER_SIZE);
		log_info("read-cb", "%s (%d): %.*s", ipAddrStr, numChar, numChar, sBuffer);
	}
}

static void event_cb(struct bufferevent* bev, short trigger, void* args) {
	char* ipAddrStr = (char*) args;
	if ((trigger & BEV_EVENT_TIMEOUT) && (trigger & BEV_EVENT_READING)){
		log_info("event-cb", "%s: Timeout, killing connection", ipAddrStr);
		bufferevent_free(bev);
		free(ipAddrStr);
	}
	else if (trigger & BEV_EVENT_EOF) {
		log_info("event-cb", "%s: Remote disconnected", ipAddrStr);
		bufferevent_free(bev);
		free(ipAddrStr);
	}
	else {
		log_info("event-cb", "%s: Unk : %x", ipAddrStr, trigger);
	}
}

static void accept_conn_cb(
		struct evconnlistener* listener,
		evutil_socket_t fd,
		struct sockaddr* address,
		int sockLen,
		void* ctx
) {
	log_trace("accept-conn-cb", "");
	struct event_base* base = NULL;
	struct bufferevent* bev = NULL;

	base = evconnlistener_get_base(listener);
	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);

	char* ipAddrStr = malloc(INET_ADDRSTRLEN + 1);

	if (address->sa_family == AF_INET) {
		snprintf(
				ipAddrStr,
				INET_ADDRSTRLEN,
				"%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8,
				(uint8_t) address->sa_data[2],
				(uint8_t) address->sa_data[3],
				(uint8_t) address->sa_data[4],
				(uint8_t) address->sa_data[5]
		);
	}
	else {
		snprintf(ipAddrStr, INET_ADDRSTRLEN, "IPv6");
	}

	log_info("accept-conn-cb", "New connection from %s", ipAddrStr);

	bufferevent_setcb(bev, read_cb, NULL, event_cb, ipAddrStr);
	bufferevent_set_timeouts(bev, &readTimer, NULL);
	bufferevent_enable(bev, EV_READ);
}

static void accept_error_cb(struct evconnlistener* listener, void* args) {
	log_trace("accept-error-cb", "");
	struct event_base* base = evconnlistener_get_base(listener);
	int err = EVUTIL_SOCKET_ERROR();
	fprintf(
					stderr,
					"Got an error %d (%s) on the listener. Shutting down.\n",
					err,
					evutil_socket_error_to_string(err)
	);
	event_base_loopexit(base, NULL);
}

int main() {
	struct event_base* base;
	struct evconnlistener* listener;
	struct sockaddr_in inAddr;

	log_init(stdout, LOG_TRACE);
	log_error("", "\n\n\n\n\n\n\n\n\n\n\n");

	readTimer.tv_sec	= 4;
	readTimer.tv_usec	= 0;

	base = event_base_new();
	assert(base);
	log_info("Created base", "");

	memset(&inAddr, 0, sizeof(struct sockaddr_in));

	inAddr.sin_family = AF_INET;
	inet_aton("141.212.122.225", (struct in_addr*) &inAddr.sin_addr.s_addr);
	inAddr.sin_port = htons(PORT);

	listener = evconnlistener_new_bind(
																		base,
																		accept_conn_cb,
																		NULL,
																		LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
																		-1,
																		(struct sockaddr*) &inAddr,
																		sizeof(inAddr)
	);
	assert(listener);
	log_info("Bound listener", "");

	evconnlistener_set_error_cb(listener, accept_error_cb);

	event_base_dispatch(base);
	log_info("Exited dispatcher", "");
}
