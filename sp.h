/* SPAUN                  */
/* sp.h                   */
/* (c) 2022 Martin Mather */

void buf_dump(uint8_t *p, int l, int hex);

void sp_poll(void);
void sp_reset(void);

int sp_send_ack(uint16_t stn);
int sp_send_reply(uint16_t stn, uint8_t *buf, int len);
int sp_send_unicast(uint16_t otherstn, uint8_t *buf, int datalen);

extern int sp_optimistic;

// SP_IDLE		- Not doing anything
// SP_DATAWAIT	- Rxed scout frame, ACK frame sent ... waiting for data frame
// SP_SCOUTWAIT	- Txed scout frame, waiting for ACK/NAK frame
// SP_ACKWAIT	- Txed data frame, waiting for ACK frame

typedef enum {
	SP_IDLE, SP_DATAWAIT, SP_SCOUTWAIT, SP_ACKWAIT
} sp_state_t;

extern sp_state_t sp_state;

typedef struct {
	int bcst, ctrllen, datalen, result;
	uint16_t stn;
	uint8_t ctrl, port, *ctrlbuf, *databuf;
} sp_scout_t;

int sp_send_scout(sp_scout_t *p);

#define SP_HDR_SIZE			4

// Frame flags
#define SP_FLAG_VOID		0x7e
#define SP_FLAG_DATA		0xbe
#define SP_FLAG_SCOUT		0xce
#define SP_FLAG_BROADCAST	0xde
#define SP_FLAG_ACK			0xee
#define SP_FLAG_NAK			0xfe

#define WORD(a, x) ((a[x+1] << 8) | a[x])
#define DWORD(a, x) ((a[x+3] << 24) | (a[x+2] << 16) | (a[x+1] << 8) | a[x])


