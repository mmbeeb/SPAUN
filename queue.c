/* SPAUN                  */
/* queue.c                */
/* (c) 2022 Martin Mather */


/*******************************/
/* Queue incoming IP datagrams */
/*******************************/

#include <stdio.h>
#include <stdint.h>
#include <malloc.h>

#include "queue.h"
#include "aun.h"


static struct queue_t Q[Q_SIZE];
static int Q_count = 0, Q_tail = 0, Q_head = 0;

void q_flush(void) {
	// flush queue
	while (Q_count)
		q_remove();
}

int q_append(uint16_t otherstn, uint8_t *buf2, int len2) {
	// add item to queue;
	// buf2 is a pointer to a block of memory allocated using malloc
	if (Q_count < Q_SIZE) {
		struct queue_t *p = &Q[Q_head++];
		Q_head %= Q_SIZE;
		Q_count++;
		
		p->otherstn = otherstn;
		p->buf2 = buf2;
		p->len2 = len2;
		p->buf = buf2 + AUN_HDR_SIZE;
		p->len = len2 - AUN_HDR_SIZE;
		
		printf("Q_ADD: %d %d %d\n", Q_count, Q_head, Q_tail);
		return 0;
	}
	
	return -1;
}

int q_remove(void) {
	// remove first item in queue
	if (Q_count > 0) {
		uint8_t *p = Q[Q_tail++].buf2;
		Q_tail %= Q_SIZE;
		Q_count--;
		
		if (p)
			free(p);//free block of memory

		printf("Q_REM: %d %d %d\n", Q_count, Q_head, Q_tail);
	}
}

struct queue_t *q_get() {
	// return pointer to first item in queue, or zero if nothing queued
	if (Q_count)
		return &Q[Q_tail];
	
	return 0;
}
