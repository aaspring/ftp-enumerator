#ifndef FTPENUMERATOR_H
#define FTPENUMERATOR_H

#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <db.h>
#include <stdbool.h>
#include <jansson.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <event2/bufferevent.h>

#include "robotParser.h"
#include "magicNumbers.h"
#include "ipQueue.h"
#include "threadStates.h"
#include "DBuffer.h"
#include "strQueue.h"

typedef enum {
	UNK = 0,
	UNIX,
	WINDOWS,
	VXWORKS
} targetType_t;

typedef enum {
	ROBOTS_UNK,
	ROBOTS_NO,
	ROBOTS_YES
} robotPresence_t;

typedef enum {
	XFER_LIST,
	XFER_FILE,
	XFER_UNK
} xferType_t;

typedef enum {
	SUPER_SHORT_CRAWL,
	SHORT_CRAWL,
	NORMAL_CRAWL
} CrawlLevel_t;

typedef enum {
	SEC_UNK,
	SEC_PRIMARY,
	SEC_SECONDARY
} SecType_t;

typedef enum {
	VALID_LINE,
	VALID_CHUNK
} ValidationResult_t;

typedef enum {
	MULTI_RESP_UNK,
	MULTI_RESP_READ_ZERO,
	MULTI_RESP_READ_ONE
} MultiRespState_e;

typedef enum {
	RESP_INCOMPLETE,
	RESP_FOUND,
	RESP_EXTRA,
	RESP_ERROR
} RespResult_e;


typedef struct {
	json_t*					fullObj;
	json_t*					rawArray;
	json_t*					miscArray;
} Recorder_t;

typedef void (*MultiRespEvalFunc_f)(struct bufferevent* bev, void* args, RespResult_e res);
typedef void (*ResetWaitPost_f)(struct bufferevent* bev, void* args);

typedef struct hoststate {
	int 											state;
	int												lastState;

	int 											ctrlSocket;
	bool											ctrlUsingSec;
	SSL*											ctrlSec;
	int 											dataSocket;
	bool											dataIsConnected;
	bool											dataUsingSec;
	SSL*											dataSec;
	SecType_t									secType;
	bool											secProbe;
	bool											earlySecReq;

	uint32_t									secProbeStorageInt;
	char											secProbeStorageStr[MAX_STATIC_BUFFER_SIZE];

	struct sockaddr_in				dataDest;

	struct bufferevent* 			ctrlBev;
	struct bufferevent*				dataBev;

	uint32_t 									ip_address;
	char 											ip_address_str[INET_ADDRSTRLEN];
	uint32_t 									terminationCode;
	char											terminationDesc[MAX_STATIC_BUFFER_SIZE];
	uint32_t 									interestMask;
	str_queue_t								dirQueue;

	DBuffer_t									dataDBuffer;
	DBuffer_t									ctrlDBuffer;

	DBuffer_t									itemDBuffer;
	targetType_t							destType;
	robotPresence_t						robotPresence;

	xferType_t								xferType;
	bufferevent_data_cb				pasvAction;
	bufferevent_data_cb				pasvPost;

	bufferevent_data_cb				delayedCtrlCB;

	bufferevent_data_cb				postSecConnectCB;

	int												thisSecond;
	int												reqThisSecond;
	size_t										numReqSent;

	char											savedReq[MAX_STATIC_BUFFER_SIZE];
	struct event*							savedReqTimer;
	int												savedNextState;

	RobotObj_t								robotObj;

	int												startingSecond;
	bool											isVxWorks;
	int												listErrCount;
	char											savedPortReq[MAX_STATIC_BUFFER_SIZE];
	bool											reqLimitReached;

	size_t										lieCount;
	unsigned char							lieHash[SHA_DIGEST_LENGTH];
	bool											provedHonest;
	bool											lieHashSet;

	struct event*							retrWaitTimer;

	Recorder_t								recorder;

	MultiRespState_e					multiRespState;
	MultiRespEvalFunc_f				multiRespEvalFunc;
	char											multiRespFirstReplyCode[REPLY_CODE_SIZE];
	struct event*							multiRespTimer;

	struct event*							resetWaitTimer;
	ResetWaitPost_f						resetWaitPostFunc;

	bool											hostIsFinished;
} hoststate_t;

// global configuration and stats
typedef struct gconfig {
	ip_queue_t 								ipQueue;
	uint32_t 									open_connections;
	uint32_t 									count_attempts;


	struct event_base*				ev_base;

	struct timeval						readTimer;
	SSL_CTX*									primarySecCtx;
	SSL_CTX*									secondarySecCtx;
	CrawlLevel_t							crawlType;
	bool											readinFinished;

	int												second;
	struct event_base* 				timer_base;

	char											sourceIp[INET_ADDRSTRLEN];
	FILE*											logFile;
	int												maxConcurConn;
	struct event*							reloadEvent;
	//struct event*							syncEvent;

	int												port;
	int												maxReqPerSec;
	int												maxReqPerIp;

	char*											metadataFilename;
	char*											interfaceName;
	char*											logFilename;
	char*											startTime;
	size_t										countComplete;
	size_t										countTimeouts;
	size_t										countDead;
	size_t										countRefused;

} gconfig_t;

extern gconfig_t gconfig;
extern const struct timeval one_second;


#endif
