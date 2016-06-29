#ifndef IPQUEUE_H
#define IPQUEUE_H

#include <stdint.h>


// linked list node for in-memory processing queue
typedef struct ip_queue_node {
	uint32_t ip_address;
	struct ip_queue_node *next;
} ip_queue_node_t;

// queue of entries. should be managed via enqueue/dequeue
// for thread safe operation
typedef struct ip_queue {
	ip_queue_node_t *first;
	ip_queue_node_t *last;
	uint32_t len;
} ip_queue_t;

void ip_enqueue(ip_queue_t* queue, uint32_t ip);
uint32_t ip_dequeue(ip_queue_t* queue);


#endif
