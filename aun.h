/* SPAUN                  */
/* aun.h                  */
/* (c) 2022 Martin Mather */

#define AUN_MAX_STATIONS	255
#define AUN_MAX_BUFFERS	20
#define AUN_PORT_BASE	10000

#define AUN_RXBUFLEN	2048	// max length of receive buffer
#define AUN_TXDELAY	.1	// seconds
#define AUN_TXTIMEOUT	1	// seconds
#define AUN_RXTIMEOUT	5	// seconds
#define AUN_TXATTEMPTS	1

#define AUN_HDR_SIZE	8

#define AUN_TYPE_BROADCAST	1
#define AUN_TYPE_UNICAST	2
#define AUN_TYPE_ACK		3
#define AUN_TYPE_NACK		4
#define AUN_TYPE_IMMEDIATE	5
#define AUN_TYPE_IMM_REPLY	6

#define ECONET_MACHINEPEEK	8

int aun_open(uint16_t stn);
int aun_close(void);
int aun_poll(void);

int aun_send(uint8_t *p, int plen);
void aun_sendack(void);
void aun_sendreply(uint8_t *p, int plen);
void aun_receiver_enable(int state);
