/* SPAUN                  */
/* queue.h                */
/* (c) 2022 Martin Mather */

#define Q_SIZE		10

struct queue_t {
	uint16_t otherstn;
	int len, len2;
	uint8_t *buf, *buf2;
};

void q_flush(void);
int q_append(uint16_t otherstn, uint8_t *buf2, int len2);
int q_remove(void);
struct queue_t *q_get();


