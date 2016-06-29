#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "ctrlSend.h"
#include "parseResp.h"

#include "../include/logger.h"
#include "../include/ftpEnumerator.h"
#include "../include/terminationCodes.h"
#include "../include/outputDB.h"
#include "../include/DBuffer.h"
#include "../include/ctrlChannel.h"
#include "../include/interestCodes.h"
#include "../include/recorder.h"

void delete_all_timers(hoststate_t* state) {
	if (state->savedReqTimer != NULL) {
		evtimer_del(state->savedReqTimer);
		event_free(state->savedReqTimer);
		state->savedReqTimer = NULL;
	}

	if (state->retrWaitTimer != NULL) {
		evtimer_del(state->retrWaitTimer);
		event_free(state->retrWaitTimer);
		state->retrWaitTimer = NULL;
	}

	if (state->multiRespTimer != NULL) {
		evtimer_del(state->multiRespTimer);
		event_free(state->multiRespTimer);
		state->multiRespTimer = NULL;
	}

	if (state->resetWaitTimer != NULL) {
		evtimer_del(state->resetWaitTimer);
		event_free(state->resetWaitTimer);
		state->resetWaitTimer = NULL;
	}
}

void store_filesystem_type(hoststate_t* state) {
	switch (state->destType) {
	case (VXWORKS) :
			record_string(state, RECKEY_FS_TYPE, "VxWorks");
			break;

	case (WINDOWS) :
			record_string(state, RECKEY_FS_TYPE, "Windows");
			break;

	case (UNIX) :
			record_string(state, RECKEY_FS_TYPE, "Unix");
			break;

	case (UNK) :
			record_string(state, RECKEY_FS_TYPE, "Unk");
			break;

	default :
			record_string(state, RECKEY_FS_TYPE, "BAD");
			break;
	}
}

void writeCtrlTrailer(hoststate_t* state) {
	log_trace("writeCtrlTrailer", "%s", state->ip_address_str);

	if (state->dirQueue.len != 0) {
		state->interestMask |= INTEREST_CHECK_MISC;
		record_raw_string_queue(state, RECKEY_UNCRAWLED, &state->dirQueue);
	}

	if (state->dataDBuffer.len != 0) {
		record_raw_dbuffer(
											state,
											RECKEY_DATA_DBUFFER_DUMP,
											NULL,
											0,
											&state->dataDBuffer
		);
	}

	if (state->ctrlDBuffer.len != 0) {
		record_raw_dbuffer(
											state,
											RECKEY_CTRL_DBUFFER_DUMP,
											NULL,
											0,
											&state->ctrlDBuffer
		);
	}

	record_int(state, RECKEY_LAST_STATE, state->lastState);
	record_int(state, RECKEY_TERM_CODE, state->terminationCode);
	record_base_raw_string(state, RECKEY_TERM_DESC, state->terminationDesc);
	record_int(state, RECKEY_INTEREST_MASK, state->interestMask);
	record_int(state, RECKEY_TOTAL_NUM_REQ, state->numReqSent);
	record_int(state, RECKEY_LIST_ERR_COUNT, state->listErrCount);
	store_filesystem_type(state);
	record_int(
						state,
						RECKEY_DURATION,
						gconfig.second - state->startingSecond
	);
	recorder_finish(state);
	log_trace(
						"writeCtrlTrailer",
						"%s: Successfully wrote control output",
						state->ip_address_str
	);
}

void end_channel_now(struct bufferevent *bev, hoststate_t* state) {
	log_trace("end-channel-now", "%s: Ending channel", state->ip_address_str);
	fill_DBuffer_all(state->ctrlBev, state, &state->ctrlDBuffer);

	if (state->dataBev != NULL) {
		fill_DBuffer_all(state->dataBev, state, &state->dataDBuffer);
		if (state->dataUsingSec) {
			SSL_set_shutdown(state->dataSec, SSL_RECEIVED_SHUTDOWN);
			SSL_shutdown(state->dataSec);
			state->dataSec = NULL;
		}
		bufferevent_free(state->dataBev);
		state->dataBev = NULL;
		log_debug(
							"end-channel-now",
							"%s: Force closed data channel",
							state->ip_address_str
		);
	}

	writeCtrlTrailer(state);
	DBuffer_destroy(&state->ctrlDBuffer);
	DBuffer_destroy(&state->dataDBuffer);
	DBuffer_destroy(&state->itemDBuffer);
	str_queue_destroy(&state->dirQueue);
	gconfig.open_connections--;
	gconfig.countComplete++;

	delete_all_timers(state);

	bufferevent_free(state->ctrlBev);
	state->ctrlBev = NULL;
	state->hostIsFinished = true;
}

void disconnect_pleasant(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace(
						"disconnect-pleasant",
						"%s : Disconnecting (pleasant)",
						state->ip_address_str
	);
	if (state->lastState == S_UNSET_STATE) {
		state->lastState = state->state;
	}
	return send_quit(bev, state);
}

void disconnect_abrupt(struct bufferevent* bev, hoststate_t* state) {
	log_trace(
						"disconnect-abrupt",
						"%s : Disconnecting (abrupt)",
						state->ip_address_str
	);
	record_string(state, RECKEY_DISCONNECT_RESULT, "ABRUPT");
	if (state->lastState == S_UNSET_STATE) {
		state->lastState = state->state;
	}
	end_channel_now(bev, state);
}

void disconnect_rude(struct bufferevent* bev, void* args) {
	hoststate_t* state = (hoststate_t*) args;
	log_trace(
						"disconnect-rude",
						"%s : Disconnecting (rude)",
						state->ip_address_str
	);
	record_string(state, RECKEY_DISCONNECT_RESULT, "RUDE");
	if (state->lastState == S_UNSET_STATE) {
		state->lastState = state->state;
	}
	end_channel_now(bev, state);
}
