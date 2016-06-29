#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "../include/logger.h"
#include "../include/dataChannel.h"
#include "../include/ftpEnumerator.h"
#include "../include/ctrlChannel.h"
#include "../include/interestCodes.h"
#include "../include/terminationCodes.h"
#include "../include/dbKeys.h"
#include "../include/magicNumbers.h"
#include "../include/listParser.h"
#include "../include/outputDB.h"
#include "../include/recordKeys.h"
#include "../include/recorder.h"

extern void disconnect_abrupt(struct bufferevent* bev, void* args);
extern ValidationResult_t fill_DBuffer_all(
																					struct bufferevent* bev,
																					hoststate_t* state,
																					DBuffer_t* dBuffer

);

void data_close_channel(struct bufferevent* bev, hoststate_t* state);

void read_data_buffer(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("read-data-buffer", "%s", state->ip_address_str);

	bufferevent_enable(state->ctrlBev, EV_READ); // reset ctrl timeout

	fill_DBuffer_all(bev, state, &state->dataDBuffer);

	if (state->dataDBuffer.len > MAX_DATA_AMT) {
		state->terminationCode = FAIL_TRAP_DATA_AMT;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"Data channel size limit reached"
		);
		log_error(
							"read-data-buffer",
							"%s: %s",
							state->ip_address_str,
							state->terminationDesc
		);
		record_raw_dbuffer(
									state,
									RECKEY_TOO_MUCH_DATA,
									DBuffer_start_ptr(&state->itemDBuffer),
									state->itemDBuffer.len,
									&state->dataDBuffer
		);
		DBuffer_clear(&state->dataDBuffer);
		disconnect_abrupt(state->ctrlBev, state);

		return;
	}

	log_trace(
						"read-data-buffer",
						"%s: Successfully read data",
						state->ip_address_str
	);

}

void data_read_cb(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace("data-read-cb", "%s", state->ip_address_str);
	read_data_buffer(bev, args);
	if (state->hostIsFinished) {
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
	}
}

void data_event_cb(
											struct bufferevent* bev,
											short triggerEvent,
											void* args
) {
	hoststate_t* state = (hoststate_t*) args;
	if (triggerEvent & BEV_EVENT_CONNECTED) {
		log_trace("data-event-cb", "%s: Connected", state->ip_address_str);
		state->dataIsConnected = true;
	}
	else if (triggerEvent & BEV_EVENT_EOF) {
		log_trace("data-event-cb", "%s: Disconnect", state->ip_address_str);
		data_close_channel(bev, state);
	}
	else if (
					(triggerEvent & BEV_EVENT_READING)
					&& (triggerEvent & BEV_EVENT_ERROR)
	) {
		log_trace("data-event-cb", "%s: Recieved RST", state->ip_address_str);
		data_close_channel(bev, state);
	}
	else if (triggerEvent & BEV_EVENT_ERROR) {
		log_trace("data-event-cb", "%s: Data Event Error", state->ip_address_str);
		if (!state->dataIsConnected) {
			log_trace(
								"data-event-cb",
								"%s: Server sent RST in reponse to a data-channel SYN",
								state->ip_address_str
			);
			state->terminationCode = FAIL_DATA_CONNECT_RESET;
			snprintf(
							state->terminationDesc,
							MAX_STATIC_BUFFER_SIZE,
							"%s",
							"Server sent RST in response to a data-channel SYN"
			);
			disconnect_abrupt(state->ctrlBev, state);
			log_info("FREE NOTIFY", "%s", state->ip_address_str);
			memset(state, 0, sizeof(hoststate_t));
			free(state);
			return;
		}
	}
	else {
		log_trace(
							"data-event-cb",
							"%s: Non 'connected' event (0x%x)",
							state->ip_address_str,
							triggerEvent
		);
	}

}

void data_setup_channel(hoststate_t* state) {
	log_trace("setup-data-channel", "%s", state->ip_address_str);
	state->dataIsConnected = false;
	state->dataSocket = create_nb_socket();
	state->dataBev = bufferevent_socket_new(
																									gconfig.ev_base,
																									state->dataSocket,
																									BEV_OPT_CLOSE_ON_FREE
	);
	if (state->dataBev == NULL) {
		log_fatal(
							"setup-data-channel",
							"%s: Unable to create socket buffer event",
							state->ip_address_str
		);
	}

	bufferevent_setcb(state->dataBev, data_read_cb, NULL, data_event_cb, state);
	state->dataUsingSec = false;

	log_trace(
						"setup-data-channel",
						"%s: Began data channel initialized",
						state->ip_address_str
	);
}

void data_connect(hoststate_t* state) {
	assert(state->xferType != XFER_UNK);
	if (state->xferType == XFER_FILE) {
		log_trace(
							"data-connect",
							"%s: Connecting data channel for FILE",
							state->ip_address_str
		);
	}
	else {
		log_trace(
							"data-connect",
							"%s: Connecting data channel for LIST",
							state->ip_address_str
		);
	}

	int rc = bufferevent_socket_connect(
																	state->dataBev,
																	(struct sockaddr*) &state->dataDest,
																	sizeof(struct sockaddr_in)
	);
	if (rc < 0) {
		log_warn(
						"setup-data-channel",
						"%s: Failed attempted connection : %s",
						state->ip_address_str,
						evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())
		);
		bufferevent_free(state->dataBev);
		return;
	}
	bufferevent_enable(state->dataBev, EV_READ | EV_WRITE);
	DBuffer_clear(&state->dataDBuffer);

	log_trace("data-connect", "%s: Data channel SYN sent", state->ip_address_str);
}


void data_tls_init(struct bufferevent* bev, hoststate_t* state) {
	log_trace("data-tls-init", "%s", state->ip_address_str);
	if (state->secType == SEC_PRIMARY) {
		state->dataSec = SSL_new(gconfig.primarySecCtx);
		log_info("data-tls-init", "%s: Using SSLv23", state->ip_address_str);
	}
	else if (state->secType == SEC_SECONDARY) {
		state->dataSec = SSL_new(gconfig.secondarySecCtx);
		log_info("data-tls-init", "%s: Using SSLv3", state->ip_address_str);
	}
	else {
		state->terminationCode = FAIL_UNK_SECURITY;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s: Unknown data channel security required",
						state->ip_address_str
		);
		log_error("data-tls-init", state->terminationDesc);
		disconnect_abrupt(state->ctrlBev, state);
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
		return;
	}

	if (!SSL_set_fd(state->dataSec, state->dataSocket)) {
		log_error(
							"data-tls-init",
							"%s: ERROR in SSL_set_fd()",
							state->ip_address_str
		);
		state->terminationCode = FAIL_HARD_INTERNAL;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"ERROR setting data ssl filedesc"
		);
		disconnect_abrupt(state->ctrlBev, state);
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
		return;
	}
	bufferevent_disable(state->dataBev, EV_READ | EV_WRITE);
	state->dataBev = bufferevent_openssl_socket_new(
																									gconfig.ev_base,
																									-1,
																									state->dataSec,
																									BUFFEREVENT_SSL_CONNECTING,
																									BEV_OPT_CLOSE_ON_FREE
	);

	if (state->dataBev == NULL) {
		log_error(
							"data-tls-init"
							"%s: ERROR creating secure data socket",
							state->ip_address_str
		);
		state->terminationCode = FAIL_HARD_INTERNAL;
		snprintf(
						state->terminationDesc,
						MAX_STATIC_BUFFER_SIZE,
						"%s",
						"ERROR creating secure data socket"
		);
		disconnect_abrupt(state->ctrlBev, state);
		log_info("FREE NOTIFY", "%s", state->ip_address_str);
		memset(state, 0, sizeof(hoststate_t));
		free(state);
		return;
	}
	bufferevent_setcb(state->dataBev, data_read_cb, NULL, data_event_cb, state);
	bufferevent_set_timeouts(state->dataBev, &gconfig.readTimer, NULL);
	state->dataUsingSec = true;
  log_trace("data-tls-init", "%s: DONE", state->ip_address_str);
}

void data_close_channel(struct bufferevent* bev, hoststate_t* state) {
	log_trace("data-close-channel", "%s", state->ip_address_str);
	if (state->dataBev == NULL) {
		// already closed
		return;
	}

	struct evbuffer *input = bufferevent_get_input(state->dataBev);
	int inputLen = evbuffer_get_length(input);
	log_debug(
						"data-close-channel",
						"%s: There are %d bytes in the ev buffer",
						state->ip_address_str,
						inputLen
	);
	if (inputLen != 0) {
		read_data_buffer(bev, state);
		if (state->hostIsFinished) {
			log_info("FREE NOTIFY", "%s", state->ip_address_str);
			memset(state, 0, sizeof(hoststate_t));
			free(state);
			return;
		}
	}

	if (state->xferType == XFER_LIST) {
		char* bufferPtr = DBuffer_start_ptr(&state->dataDBuffer);
		if (bufferPtr != NULL) {
			record_raw_dbuffer(
										state,
										RECKEY_LIST_DATA,
										state->itemDBuffer.startPtr,
										state->itemDBuffer.len,
										&state->dataDBuffer
			);
			if (!DBuffer_is_string(&state->dataDBuffer)) {
				state->interestMask |= INTEREST_NULL_IN_LIST_DATA;
			}
			else {
				parse_list_data(state, DBuffer_start_ptr(&state->dataDBuffer));
			}


			int i = 0;
			int aChar = 0;
			for (i = 0; i < state->dataDBuffer.len; i++) {
				aChar = (int) DBuffer_start_ptr(&state->dataDBuffer)[i] - '\0';
				if (
						(aChar == 0x7f)
						|| (
								(aChar < 0x20)
								&& (aChar != 0x0a)
								&& (aChar != 0x0d)
								&& (aChar != 0x09)
						)

				) {
					state->interestMask |= INTEREST_CTRL_IN_LIST_DATA;
					break;
				}
			}

			DBuffer_clear(&state->dataDBuffer);
		}
	}

	if (state->dataUsingSec) {
		SSL_set_shutdown(state->dataSec, SSL_RECEIVED_SHUTDOWN);
		SSL_shutdown(state->dataSec);
		state->dataSec = NULL;
	}

	bufferevent_free(state->dataBev);
	state->dataIsConnected = false;
	state->dataBev = NULL;
	log_trace(
					"data-close-channel",
					"%s: Closed data channel",
					state->ip_address_str
	);

	if (state->delayedCtrlCB != NULL) {
		bufferevent_data_cb delayed = state->delayedCtrlCB;
		state->delayedCtrlCB = NULL;
		log_trace(
							"data-close-channel",
							"%s: Executing DELAYED ctrl cb",
							state->ip_address_str
		);
		delayed(state->ctrlBev, state);
	}
}
