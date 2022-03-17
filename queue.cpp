/* SPAUN                  */
/* queue.cpp              */
/* (c) 2022 Martin Mather */


/*******************************/
/* Queue incoming IP datagrams */
/*******************************/

#include <iostream>
#include <string>
using namespace std;

#include <stdio.h>
#include <stdint.h>
#include <malloc.h>

#include "queue.h"

void QueueClass::flush(void) {
	// flush queue
	while (Q_count)
		this->remove();
}

int QueueClass::append(struct queue_t *q) {
	// add item to queue;
	// buf2 is a pointer to a block of memory allocated using malloc
	if (Q_count < Q_SIZE) {
		struct queue_t *p = &Q[Q_head++];
		Q_head %= Q_SIZE;
		Q_count++;
		
		*p = *q;
		
		//printf("Q_ADD: %d %d %d\n", Q_count, Q_head, Q_tail);
		return 0;
	}
	
	return -1;
}

void QueueClass::remove(void) {
	// remove first item in queue
	if (Q_count > 0) {
		uint8_t *p = Q[Q_tail++].buf;
		Q_tail %= Q_SIZE;
		Q_count--;
		
		if (p)
			free(p);//free block of memory

		//printf("Q_REM: %d %d %d\n", Q_count, Q_head, Q_tail);
	}
}

struct queue_t * QueueClass::get(void) {
	// return pointer to first item in queue, or zero if nothing queued
	if (Q_count)
		return &Q[Q_tail];
	
	return 0;
}

int QueueClass::empty(void) {
	return !Q_count;
}
