#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "../include/logger.h"
#include "../include/ftpEnumerator.h"
#include "../include/listParser.h"
#include "../include/dbKeys.h"

#define SBUFFER_SIZE 4096

gconfig_t gconfig;

void record_misc_string(hoststate_t* state, char* string) {
	printf("----- %s\n", string);
	assert(
			(strcmp(string, "Reported as Unix but has Windows LIST style") == STR_MATCH)
			|| (strcmp(string, "Reported as VxWorks but has Linux LIST style") == STR_MATCH)
			|| (strcmp(string, "Reported as Windows but has Unix LIST style") == STR_MATCH)
	);
}
void record_misc_partial_raw_1(
		hoststate_t* state,
		char* format,
		char* raw,
		size_t rawLen
) {
	printf("----- %s\n", format);
	fflush(stdout);
	assert(false);
}

void record_misc_partial_raw_2(
		hoststate_t* state,
		char* string,
		char* raw1,
		size_t rawLen1,
		char* raw2,
		size_t rawLen2
) {
	printf("----- %s\n", string);
	fflush(stdout);
	assert(
			(strcmp(string, "Likely file symlink ignored -- %s -> %s") == STR_MATCH)
	);
}

int main(int argc, char** argv) {
	char* sBuffer = NULL;
	char* inDbPath = NULL;
	char* outDbPath = NULL;
	char* type = NULL;
	bool i = false;
	bool o = false;
	bool t = false;
	int opt = 0;
	char dirSBuffer[DBUFFER_INIT_LARGE];

	while ((opt = getopt(argc, argv, "i:o:t:")) != -1) {
		switch (opt) {
		case ('i') :
				inDbPath = optarg;
				i = true;
				break;
		case ('o') :
				outDbPath = optarg;
				o = true;
				break;
		case ('t') :
				type = optarg;
				t = true;
				break;
		}
	}

	if (!i || !o || !t) {
		printf("You must supply input (-i), output (-o), and type (-t) databases\n");
		printf("Exiting...\n");
		exit(1);
	}

	hoststate_t state;
	snprintf(state.ip_address_str, INET_ADDRSTRLEN, "%s", "1.1.1.1");
	if (strcmp(type, "linux") == 0) {
		printf("Parsing as a linux server\n");
		state.destType = UNIX;
	}
	else if (strcmp(type, "windows") == 0) {
		printf("Parsing as a Windows server\n");
		state.destType = WINDOWS;
	}
	else if (strcmp(type, "vxworks") == 0) {
		printf("Parsing as a VxWorks server\n");
		state.destType = VXWORKS;
	}
	else {
		printf("Parsing as a Unknown server\n");
		state.destType = UNK;
	}
	str_queue_init(&state.dirQueue);
	DBuffer_init(&state.itemDBuffer, DBUFFER_INIT_TINY);
	DBuffer_append(&state.itemDBuffer, "/");

	gconfig.crawlType = NORMAL_CRAWL;
	log_init(stderr, LOG_TRACE);

	FILE* file = fopen(inDbPath, "r");
	fseek(file, 0, SEEK_END);
	long fsize = ftell(file);
	fseek(file, 0, SEEK_SET);

	sBuffer = malloc(fsize + 1);
	fread(sBuffer, fsize, 1, file);

	parse_list_data(&state, sBuffer);

	FILE* outFile = fopen(outDbPath, "w");
	while (state.dirQueue.len != 0) {
		str_dequeue(&state.dirQueue, dirSBuffer, DBUFFER_INIT_LARGE);
		printf("C directory : %s\n", dirSBuffer);
		fwrite(dirSBuffer, sizeof(char), strlen(dirSBuffer), outFile);
		fwrite("\n", sizeof(char), 1, outFile);
	}
}
