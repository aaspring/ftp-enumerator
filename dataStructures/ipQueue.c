#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>

#include "../include/ipQueue.h"
#include "../include/magicNumbers.h"

pthread_mutex_t ip_queue_lock = PTHREAD_MUTEX_INITIALIZER;

// enqueue ip address to be scanned in global fifo queue. thread-safe.
// called by the stdin-reader thread.
void ip_enqueue(ip_queue_t* queue, uint32_t ip) {
	pthread_mutex_lock(&ip_queue_lock);
	ip_queue_node_t *new = malloc(sizeof(ip_queue_node_t));
	assert(new);
	new->next = NULL;
	new->ip_address = ip;
	if (!queue->first) {
	        queue->first = new;
	} else {
	        queue->last->next = new;
	}
	queue->last = new;
	++queue->len;
	pthread_mutex_unlock(&ip_queue_lock);
}

// dequeue ip address to be scanned in global fifo queue. thread-safe.
// called by libevent thread when it needs more IPs to scan
uint32_t ip_dequeue(ip_queue_t* queue) {
	pthread_mutex_lock(&ip_queue_lock);
	if (!queue->first) {
	        pthread_mutex_unlock(&ip_queue_lock);
	        return 0;
	}
	uint32_t ip = queue->first->ip_address;
	ip_queue_node_t* next = queue->first->next;
	free(queue->first);
	queue->first = next;
	--queue->len;
	pthread_mutex_unlock(&ip_queue_lock);
	return ip;
}
