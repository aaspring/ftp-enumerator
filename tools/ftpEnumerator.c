/*
	FTP Enumerator

	Description: Accepts a line delimited list of IP addresses via stdin,
	attempts to connect anonymously to each IP as an FTP client, enumerates
	the files, and records other metrics.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <db.h>
#include <netdb.h>
#include <dirent.h>
#include <libconfig.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/resource.h>

#include <event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/http.h>
#include <event2/listener.h>

#include <openssl/err.h>

#include "../include/ftpEnumerator.h"
#include "../include/logger.h"
#include "../include/ipQueue.h"
#include "../include/threadStates.h"
#include "../include/ctrlChannel.h"
#include "../include/outputDB.h"

#ifdef __APPLE__
#else
	#include <bsd/string.h>
#endif // __APPLE__

gconfig_t gconfig;
const struct timeval one_second = {1, 0};
bool gFinished = false;

char* thread_state_strings[] = {
	"initial",
	"connected",
	"USER sent",
	"PASS sent",
	"PORT sent",
	"PWD sent",
	"PASV sent",
	"data channel connected",
	"awaiting LIST done",
	"data transfering",
	"FEAT sent",
	"Stupid Dev",
	"HELP sent",
	"SYST sent",
	"STAT sent",
	"AUTH TLS sent",
	"PROT sent",
	"Initial PBSZ sent",
	"PBSZ sent",
	"TYPE sent",
	"RETR sent",
	"AUTH SSL sent",
	"awaiting RETR done",
	"PORT-LIST sent",
	"QUIT sent",
	"QUIT acknowledged",
	"PORT-LIST xfering",
	"SITE sent",
	"Security connecting",
	"RESET wait",
	"UNSET state",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer",
	"bad thread_state_strings pointer"
};


// thread to print on-screen status updates
void* status_updates(void *params) {
	while (1) {
		sleep(STATUS_INTERVAL);
		if (gFinished) {
			return NULL;
		}

		fprintf(
					stderr,
					"STATUS: Queued %u | In-progress: %u | Attempted %u | Finished %zu"
											" | Dead %zu | Timed out %zu | Refused %zu\n",
					gconfig.ipQueue.len,
					gconfig.open_connections,
					gconfig.count_attempts,
					gconfig.countComplete,
					gconfig.countDead,
					gconfig.countTimeouts,
					gconfig.countRefused

		);
	}
	return NULL;
}

void connect_new(uint32_t ip_address) {
	gconfig.count_attempts++;
	// metadata that describes the connection so that
	// we know what host we're communication within in cb
	hoststate_t *state = malloc(sizeof(hoststate_t));
	assert(state);
	memset(state, 0, sizeof(hoststate_t));
	str_queue_init(&state->dirQueue);
	state->ip_address = ip_address;
	state->state = S_INITIAL;
	// we always free
	ctrl_init(state);

}

void reload(evutil_socket_t fd, short triggerEvent, void* args) {
	uint32_t ip;
	int i = 0;
	int numAvailConn = gconfig.maxConcurConn - gconfig.open_connections;
	log_debug("reload", "%d available connections", numAvailConn);

	for (i = 0; i < numAvailConn; i++) {
		ip = ip_dequeue(&gconfig.ipQueue);

		if (ip == 0) {
			log_debug("reload", "End of ip queue found");
			if (gconfig.readinFinished) {
				log_debug("reload", "Finished loading channel events");
				evtimer_del(gconfig.reloadEvent);
				event_free(gconfig.reloadEvent);
				return;
			}
			else {
				return;
			}
		}

		connect_new(ip);
	}
}

void* worker(void *params) {
	log_info("ctrl-init", "thread started");
	gconfig.reloadEvent = event_new(
																	gconfig.ev_base,
																	-1,
																	EV_PERSIST,
																	reload,
																	NULL
	);
	evtimer_add(gconfig.reloadEvent, &one_second);

	/*
	gconfig.syncEvent = event_new(
																gconfig.ev_base,
																-1,
																EV_PERSIST,
																sync_db,
																NULL
	);
	evtimer_add(gconfig.syncEvent, &one_second);
	*/

	event_base_dispatch(gconfig.ev_base);

	log_info("ctrl-init", "libevent finished. thread ending.");
	return NULL;
}

void* readstdin(void *params) {
	log_info("readstdin", "thread started");
	char* line = malloc(INET_ADDRSTRLEN + 2);
	size_t len = INET_ADDRSTRLEN + 1;
	ssize_t read;
	int ret = 0;
	struct in_addr t;

	while (true) {
		read = getline(&line, &len, stdin);

		if (read < 0) {
			log_info(
							"readstdin",
							"READINGERROR - Got a return code of 0x%x from getline",
							read
			);
			break;
		}

		if ((read < 7) || (read > INET_ADDRSTRLEN)) {
			log_warn(
							"readstdin",
							"READINGERROR - ILLEGAL SIZE stdin input : %s",
							line
			);
			continue;
		}

		line[read - 1] = '\0'; // Account for the newline
		ret = inet_aton(line, &t);

		if (ret != 0) {
			ip_enqueue(&gconfig.ipQueue, t.s_addr);
		}
		else {
			log_warn(
							"readstdin",
							"READINGERROR - ILLEGAL Format for input : %s",
							line
			);
		}
	}

	log_info("readstdin", "thread ended");
	gconfig.readinFinished = true;
	free(line);
	return NULL;
}

void incrementSecond(evutil_socket_t fd, short triggerEvent, void* args) {
	gconfig.second++;
}

void* timer(void* args) {
	gconfig.timer_base = event_base_new();
	struct event *ev = event_new(
															gconfig.timer_base,
															-1,
															EV_PERSIST,
															incrementSecond,
															NULL
	);
	evtimer_add(ev, &one_second);
	event_base_dispatch(gconfig.timer_base);
	return NULL;
}

void write_metadata_file(void) {
	FILE* handle = NULL;
	char sBuffer[MAX_STATIC_BUFFER_SIZE];
	if (gconfig.metadataFilename == NULL) {
		handle = stdout;
	}
	else {
		handle = fopen(gconfig.metadataFilename, "w");
		if (handle == NULL) {
			printf("There was an error opening the metadata file.\n");
			printf("Writing to stdout.\n");
			handle = stdout;
		}
	}

	fprintf(handle, "{\n");
	sBuffer[MAX_STATIC_BUFFER_SIZE - 1] = '\0';
	gethostname(sBuffer, MAX_STATIC_BUFFER_SIZE - 1);

	fprintf(handle, "\t\"local-hostname\":\"%s\",\n", sBuffer);
	fprintf(handle, "\t\"username\":\"%s\",\n", USERNAME);
	fprintf(handle, "\t\"password\":\"%s\",\n", PASSWORD);
	fprintf(handle, "\t\"sending-interface\":\"%s\",\n", gconfig.interfaceName);
	fprintf(handle, "\t\"source-ip\":\"%s\",\n", gconfig.sourceIp);
	fprintf(handle, "\t\"dest-port\":\"%d\",\n", gconfig.port);
	fprintf(handle, "\t\"max-concur-conn\":\"%d\",\n", gconfig.maxConcurConn);
	fprintf(handle, "\t\"max-request-per-sec\":\"%d\",\n", gconfig.maxReqPerSec);
	fprintf(handle, "\t\"max-request-per-IP\":\"%d\",\n", gconfig.maxReqPerIp);


	fprintf(handle, "\t\"crawl-type\":\"");

	switch (gconfig.crawlType) {
	case (SUPER_SHORT_CRAWL) :
			fprintf(handle, "super_short\",\n");
			break;
	case (SHORT_CRAWL) :
			fprintf(handle, "short\",\n");
			break;
	case (NORMAL_CRAWL) :
			fprintf(handle, "normal\",\n");
			break;
	}

	if (gconfig.logFilename == NULL) {
		fprintf(handle, "\t\"logging-file\":\"stderr\",\n");
	}
	else {
		fprintf(handle, "\t\"logging-file\":\"%s\",\n", gconfig.logFilename);
	}

	fprintf(handle, "\t\"concurrent-cap\":\"%d\",\n", gconfig.maxConcurConn);
	fprintf(handle, "\t\"number-targets\":\"%d\",\n", gconfig.count_attempts);
	fprintf(handle, "\t\"number-completed\":\"%zu\",\n", gconfig.countComplete);
	fprintf(handle, "\t\"number-timed-out\":\"%zu\",\n", gconfig.countTimeouts);
	fprintf(handle, "\t\"number-dead\":\"%zu\",\n", gconfig.countDead);
	fprintf(handle, "\t\"number-refused\":\"%zu\",\n", gconfig.countRefused);
	fprintf(handle, "\t\"start-time\":\"%s\",\n", gconfig.startTime);

	time_t endTimeOb;
	struct tm *endInfo;
	time(&endTimeOb);
	endInfo = localtime(&endTimeOb);
	strftime(
					sBuffer,
					MAX_STATIC_BUFFER_SIZE,
					"%d%b%Y %H:%M:%S",
					endInfo
	);
	fprintf(handle, "\t\"end-time\":\"%s\",\n", sBuffer);

	fprintf(handle, "}\n");
	fclose(handle);
}

void usage() {
	printf("* == required\n");
	printf("[] == default\n");
	printf("() == available\n");
	printf("Command line arguments\n");
	printf("\t-t\tThe type of crawl\n");
	printf("\t\t(super_short, short, normal)\n");
	printf("\t\t[normal]\n");
	printf("\t-o & -d\tThe output database path\n");
	printf("\t\t[./%s<current time>.db]\n", DEFAULT_OUTPUT_DIR);
	printf("\t-i*\tThe interface to use\n");
	printf("\t-l\tWhere to write logging info to\n");
	printf("\t\t[stderr]\n");
	printf("\t-L\tMax requests per IP\n");
	printf("\t\t[%d]\n", DEFAULT_MAX_REQ_PER_IP);
	printf("\t-r\tThe max number of concurrent connections to use\n");
	printf("\t\t[%d]\n", DEFAULT_MAX_CONCUR_CONN);
	printf("\t-R\tThe number of requests per second to use\n");
	printf("\t\t[%d]\n", DEFAULT_MAX_REQ_PER_SEC);
	printf("\t-m\tThe location to write metadata to\n");
	printf("\t\t[stdout]\n");
	printf("\t-p\tThe port to use\n");
	printf("\t\t[%d]\n", DEFAULT_PORT);
	printf("\t-h\tPrint the help text\n");
	printf("\t-c\tA config file");
}

void read_config_file(int argc, char** argv) {
	config_t cfg;
	config_init(&cfg);
	bool found = false;
	int intHolder = 0;
	char* strHolder = NULL;
	int fd = 0;
	struct ifreq ifr;
	int i = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-c") == STR_MATCH) {
			found = true;
			if (!config_read_file(&cfg, argv[i + 1])) {
				printf("Could not read the default configs at %s\n", argv[i + 1]);
				printf("Exiting...\n");
				exit(1);
			}
			break;
		}
	}

	if (!found) {
		return;
	}

	if (config_lookup_int(&cfg, "port", &intHolder)) {
		gconfig.port = intHolder;
	}

	if (config_lookup_string(&cfg, "type", (const char**) &strHolder)) {
		if (strcmp(strHolder, "super_short") == STR_MATCH) {
			gconfig.crawlType = SUPER_SHORT_CRAWL;
		}
		else if (strcmp(strHolder, "short") == STR_MATCH) {
			gconfig.crawlType = SHORT_CRAWL;
		}
		else if (strcmp(strHolder, "normal") == STR_MATCH) {
			gconfig.crawlType = NORMAL_CRAWL;
		}
		else {
			printf("Unknown crawl type '%s'\n", strHolder);
			printf("Exiting...\n");
			exit(1);
		}
	}

	if (config_lookup_string(&cfg, "interface", (const char**) &strHolder)) {
		gconfig.interfaceName = strHolder;
		fd = socket(AF_INET, SOCK_DGRAM, 0);
		ifr.ifr_addr.sa_family = AF_INET;
		strlcpy(ifr.ifr_name, strHolder, IFNAMSIZ);
		ioctl(fd, SIOCGIFADDR, &ifr);
		close(fd);

		inet_ntop(
							AF_INET,
							&((struct sockaddr_in*) &ifr.ifr_addr)->sin_addr,
							gconfig.sourceIp,
							INET_ADDRSTRLEN
		);
	}

	if (config_lookup_string(&cfg, "logging", (const char**) &strHolder)) {
		if (strcmp(strHolder, "stderr") == STR_MATCH) {
			gconfig.logFilename = strHolder;
			gconfig.logFile = stderr;
		}
		else {
			gconfig.logFilename = strHolder;
			gconfig.logFile = fopen(strHolder, "w");
			if (gconfig.logFile == NULL) {
				printf("There was an error opening the logging file.\nExiting...\n");
				exit(1);
			}
		}
	}

	if (config_lookup_int(&cfg, "maxConcur", &intHolder)) {
		gconfig.maxConcurConn = intHolder;
		struct rlimit limitRet;
		int rc = getrlimit(RLIMIT_NOFILE, &limitRet);
		if (rc) {
			printf("There was a problem retrieving the number of files\n");
			printf("Exiting...\n");
			exit(1);
		}
		if ((gconfig.maxConcurConn * 2) >= limitRet.rlim_cur) {
			// 2x b/c each ctrl channel can open a data channel
			printf(
						"%d concurrent connections; %lld are allowed by the system\n",
						gconfig.maxConcurConn,
						(long long) limitRet.rlim_cur
			);
			printf("Change with 'ulimit -n XXXXX'\n");
			printf("Exiting...\n");
			exit(1);
		}
	}

	if (config_lookup_string(&cfg, "metadataFile", (const char**) &strHolder)) {
		gconfig.metadataFilename = strHolder;
	}

	if (config_lookup_int(&cfg, "maxRequestsPerSec", &intHolder)) {
		gconfig.maxReqPerSec = intHolder;
	}

	if (config_lookup_int(&cfg, "maxRequestsPerIP", &intHolder)) {
		gconfig.maxReqPerIp = intHolder;
		assert(gconfig.maxReqPerIp > 5);
	}
}

void destroy_gconfig_internals() {
	ERR_free_strings();
	free(gconfig.startTime);
	SSL_CTX_free(gconfig.primarySecCtx);
	SSL_CTX_free(gconfig.secondarySecCtx);
	event_base_free(gconfig.ev_base);
}

void initializeGconfig(int argc, char** argv) {
	memset(&gconfig, 0, sizeof(gconfig_t));

    if strcmp(PASSWORD, "<UNSET>") == 0 {
        printf("You must set PASSWORD in include/magicNumbers.h\n");
        exit(1);
    }

    if strcmp(BAD_PORT_ARG, "<UNSET>") == 0 {
        printf("You must set BAD_PORT_ARG in include/magicNumbers.h\n");
        exit(1);
    }

    if strcmp(USER_AGENT, "<UNSET>") == 0 {
        printf("You must set USER_AGENT in include/magicNumbers.h\n");
        exit(1);
    }

	SSL_library_init();
	SSL_load_error_strings();
#ifdef __APPLE__
	SSL_METHOD* primaryMethod = SSLv23_client_method();
	SSL_METHOD* secondaryMethod = SSLv3_client_method();
#else
	const SSL_METHOD* primaryMethod = SSLv23_client_method();
	const SSL_METHOD* secondaryMethod = SSLv3_client_method();
#endif

	gconfig.primarySecCtx = SSL_CTX_new(primaryMethod);
	SSL_CTX_set_options(gconfig.primarySecCtx, 0);

	gconfig.secondarySecCtx = SSL_CTX_new(secondaryMethod);
	SSL_CTX_set_options(gconfig.secondarySecCtx, 0);

	gconfig.readTimer.tv_sec	= READ_TIMEOUT;
	gconfig.readTimer.tv_usec	= 0;

	gconfig.ev_base = event_base_new();
	assert(gconfig.ev_base);

	read_config_file(argc, argv);

	int c = 0;
	int fd = 0;
	struct ifreq ifr;
	while((c = getopt(argc, argv, "t:i:l:r:m:p:R:L:c:h")) != -1) {
		switch (c) {
		case ('h') :
				usage();
				break;
		case ('i') :
				gconfig.interfaceName = optarg;
				fd = socket(AF_INET, SOCK_DGRAM, 0);
				ifr.ifr_addr.sa_family = AF_INET;
				strlcpy(ifr.ifr_name, optarg, IFNAMSIZ);
				ioctl(fd, SIOCGIFADDR, &ifr);
				close(fd);

				inet_ntop(
									AF_INET,
									&((struct sockaddr_in*) &ifr.ifr_addr)->sin_addr,
									gconfig.sourceIp,
									INET_ADDRSTRLEN
				);

				break;
		case ('t') :
				if (strcmp(optarg, "super_short") == STR_MATCH) {
					gconfig.crawlType = SUPER_SHORT_CRAWL;
				}
				else if (strcmp(optarg, "short") == STR_MATCH) {
					gconfig.crawlType = SHORT_CRAWL;
				}
				else if (strcmp(optarg, "normal") == STR_MATCH) {
					gconfig.crawlType = NORMAL_CRAWL;
				}
				else {
					printf("Unknown crawl type '%s'\n", optarg);
					printf("Exiting...\n");
					exit(1);
				}
				break;
		case ('l') :
				gconfig.logFilename = optarg;
				gconfig.logFile = fopen(optarg, "w");
				if (gconfig.logFile == NULL) {
					printf("There was an error opening the logging file.\nExiting...\n");
					exit(1);
				}
				break;
		case ('r') :
				gconfig.maxConcurConn = atoi(optarg);
				struct rlimit limitRet;
				int rc = getrlimit(RLIMIT_NOFILE, &limitRet);
				if (rc) {
					printf("There was a problem retrieving the number of files\n");
					printf("Exiting...\n");
					exit(1);
				}
				if ((gconfig.maxConcurConn * 2) >= limitRet.rlim_cur) {
					// 2x b/c each ctrl channel can open a data channel
					printf(
								"%d concurrent connections; %lld are allowed by the system\n",
								gconfig.maxConcurConn,
								(long long) limitRet.rlim_cur
					);
					printf("Change with 'ulimit -n XXXXX'\n");
					printf("Exiting...\n");
					exit(1);
				}
				break;
		case ('m') :
				gconfig.metadataFilename = optarg;
				break;
		case ('p') :
				gconfig.port = atoi(optarg);
				break;
		case ('R') :
				gconfig.maxReqPerSec = atoi(optarg);
				break;
		case ('L') :
				gconfig.maxReqPerIp = atoi(optarg);
				assert(gconfig.maxReqPerIp > 5);
				break;
		}
	}

	if (gconfig.interfaceName == NULL) {
		printf("You must supply an interface to use.\nExiting...\n");
		exit(1);
	}
	else {
		// This is just a saftey for me, should be removed before release
		assert(
					(strcmp(gconfig.sourceIp, "192.168.56.1") == STR_MATCH)
					|| (strcmp(gconfig.sourceIp, "141.212.122.224") == STR_MATCH)
		);
	}

	if (gconfig.logFilename == NULL) {
		printf("You must supply a log file location");
		exit(2);
	}

	if (gconfig.maxConcurConn == 0) {
		gconfig.maxConcurConn = DEFAULT_MAX_CONCUR_CONN;
	}

	if (gconfig.port == 0) {
		gconfig.port = DEFAULT_PORT;
	}

	if (gconfig.maxReqPerSec == 0) {
		gconfig.maxReqPerSec = DEFAULT_MAX_REQ_PER_SEC;
	}

	if (gconfig.maxReqPerIp == 0) {
		gconfig.maxReqPerIp = DEFAULT_MAX_REQ_PER_IP;
	}

	time_t startTimeObj;
	struct tm *startInfo;
	time(&startTimeObj);
	startInfo = localtime(&startTimeObj);
	gconfig.startTime = malloc(sizeof(char) * MAX_STATIC_BUFFER_SIZE);
	strftime(
					gconfig.startTime,
					MAX_STATIC_BUFFER_SIZE,
					"%d%b%Y %H:%M:%S",
					startInfo
	);
}

int main(int argc, char** argv) {
	initializeGconfig(argc, argv);

	log_init(gconfig.logFile, LOG_TRACE);
	log_error("", "\n\n\n\n\n\n\n\n\n\n\n");

	pthread_t timerThread;
	pthread_create(&timerThread, NULL, timer, NULL);

	pthread_t inputThread;
	pthread_create(&inputThread, NULL, readstdin, NULL);

	pthread_t statusThread;
	pthread_create(&statusThread, NULL, status_updates, NULL);

	pthread_t workerThread;
	pthread_create(&workerThread, NULL, worker, NULL);

	pthread_join(workerThread, NULL);
	pthread_join(inputThread, NULL);
	gFinished = true;
	pthread_join(statusThread, NULL);
	// timerThread will be killed implicitly

	write_metadata_file();
	log_info("main", "Destroying gconfig");
	destroy_gconfig_internals();

	log_info("main", "Closing log file");
	fclose(gconfig.logFile);

	printf("DONE\n");
	return EXIT_SUCCESS;
}
