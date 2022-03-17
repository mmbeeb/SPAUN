/* SPAUN                  */
/* queue.h                */
/* (c) 2022 Martin Mather */

#ifndef QUEUE_H
#define QUEUE_H

#include "constants.h"

struct queue_t {
	int type;
	int otherstn;
	uint8_t ctrl, port;
	uint32_t handle;
	int len;
	uint8_t *buf;
};

class QueueClass {
	private:
		struct queue_t Q[Q_SIZE];
		int Q_count = 0, Q_tail = 0, Q_head = 0;

	public:
		void flush(void);
		int append(struct queue_t *q);
		void remove(void);
		int empty(void);
		struct queue_t *get(void);
};

#endif
