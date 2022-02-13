/* SPAUN                  */
/* sp.c                   */
/* (c) 2022 Martin Mather */

#include <stdio.h> 
#include <stdint.h>
#include <stdlib.h> 
#include <time.h>

#include "sp.h"
#include "serial.h"
#include "aun.h"
#include "queue.h"

#define CRC_POLY	0x11021
#define ESC_CHAR	0xe5

#define SP_TIMEOUT	5		//seconds
#define SP_DELAY	5		//1 second/SP_DELAY
#define RX_BUF_SIZE	2048

static uint32_t rx_crc, tx_crc;

static int EscFlag = 0, rx_counter;
static uint8_t rx_fifo0, rx_fifo1;
static int rx_flag, rx_fifo_notfull;

static enum {
	RX_VOID, RX_FLAG, RX_DATA
} rx_state = RX_VOID;

static uint8_t sp_buf[128];
static uint8_t rx_buf2[RX_BUF_SIZE], *rx_buf = rx_buf2 + AUN_HDR_SIZE;
static uint8_t *rx_data_buf;

static enum {
	TX_IDLE, TX_OPEN, TX_DATA, TX_CLOSE, TX_CLOSEWAIT, TX_CLOSEDELAY
} tx_state = TX_IDLE;

static int tx_counter, tx_tail, tx_data_flag;
static uint8_t *tx_data_buf;

static uint8_t *tx_buf, tx_flag, tx_buf2[100];

//time stuff
clock_t sp_trigger;

//v3 state
sp_state_t sp_state = SP_IDLE;
sp_scout_t *sp_scout = NULL;
sp_scout_t sp_rx_scout;

int sp_optimistic = 1;




void sp_set_state(sp_state_t new_state) {
	if (sp_state != new_state) {
		printf("SP  : STATE = ");
		switch(new_state) {
			case SP_IDLE:
				printf("IDLE\n\n");
				break;
			case SP_DATAWAIT:
				printf("DATAWAIT\n");
				break;
			case SP_SCOUTWAIT:
				printf("SCOUTWAIT\n");
				break;
			case SP_ACKWAIT:
				printf("ACKWAIT\n");
				break;
		}
		sp_state = new_state;
	}

	if (sp_state == SP_IDLE) {
		aun_receiver_enable(1);
		//EscFlag = 0;
		//rx_state = RX_VOID;
		tx_state = TX_IDLE;
	}
}

void sp_reset(void) {
	printf("sp_reset\n");
	sp_set_state(SP_IDLE);
	EscFlag = 0;
	rx_state = RX_VOID;
}

void buf_dump(uint8_t *p, int l, int hex) {
	for (int i = 0; i < l; i++, p++) {
		if (hex) {
			printf("[%02x]", *p);
		} else {
			if (*p == 0xd)
				printf("\n");
			else if (*p >= 0x20 && *p < 0x7f)
				printf("%c", *p);
			else
				printf("[%02x]", *p);
		}
	}
	printf("\n");
}

static void crc_calc(uint8_t c, uint32_t *crc) {
	*crc = *crc ^ (c << 8);
	for (int i=0; i<8; i++) {
		if (*crc & 0x8000)
			*crc = (*crc << 1) ^ CRC_POLY;
		else
			*crc = *crc << 1;
	}
}


int sp_send_ack(uint16_t stn) {
	printf("SP  : TX ACK FROM %04x\n", stn);
	
	if (tx_state == TX_IDLE) {
		tx_buf2[0] = stn;
		tx_buf2[1] = stn >> 8;
		
		tx_buf = tx_buf2;
		tx_counter = 2;
		tx_flag = SP_FLAG_ACK;
		tx_state = TX_OPEN;
	} else
		printf("SP  : *** ERROR ALREADY TRANSMITTING!\n");
}


int sp_send_reply(uint16_t stn, uint8_t *databuf, int datalen) {
	uint8_t buf[2];
	
	printf("SP  : TX IMM REPLY FROM %04x, LEN=%d\n", stn, datalen);
	
	if (tx_state == TX_IDLE) {
		databuf[6] = stn;
		databuf[7] = stn >> 8;
		
		tx_buf = databuf + 6;
		tx_counter = datalen - 6;
		tx_flag = SP_FLAG_DATA;
		tx_state = TX_OPEN;	
	} else
		printf("SP  : *** ERROR ALREADY TRANSMITTING!\n");
		
	return 0;
}


void sp_check_queue() {
	//Is there anything in the queue?
	struct queue_t *p = q_get();
	if (p) {
		printf("SP: There's something in the queue!\n");
		//TRANSMIT IT OVER SP!
		sp_send_unicast(p->otherstn, p->buf2, p->len2);
		
		//we will need to remove it from the queue once transmitted!
	}
}

int sp_send_unicast(uint16_t stn, uint8_t *databuf, int datalen) {

	//if (sp_optimistic)
	//	aun_sendack();//SEND IT NOW
	
	static sp_scout_t scout;
	int ctrllen = 4, reqlen[] = {8, 8, 4, 4, 4, 0, 0, 4};
	
	scout.bcst = 0;
	scout.stn = stn;
	scout.ctrl = databuf[2] | 0x80;
	scout.port = databuf[1];
	
	if (scout.bcst) 
		ctrllen += 8;
	else if (!scout.port && scout.ctrl >= 0x81 && scout.ctrl <= 0x88) {
		ctrllen += reqlen[scout.ctrl - 0x81];
	}
	
	// databuf -> AUN header + data
	scout.ctrlbuf = databuf + 4;
	scout.ctrllen = ctrllen;	// SCOUT FRAME LENGTH
	
	scout.databuf = scout.ctrlbuf + ctrllen - 2;
	scout.datalen = datalen - 4 - ctrllen + 2; // DATA FRAME LENGTH
	
	printf("SP  : TX UNICAST FROM %04x, CTRL LEN=%d, DATA LEN=%d\n", stn, scout.ctrllen - 4, scout.datalen - 2);
	
	if (sp_send_scout(&scout)) {
		printf("SP  : failed to send scout\n");
		aun_receiver_enable(1);
		sp_set_state(SP_IDLE);
	}
	
}

	// Transmit scout (Broadcast&/Immediate/Data)
int sp_send_scout(sp_scout_t *p) {
	printf("SP  : TX SCOUT FROM %04x, PORT=%02x CTRL=%02x\n", p->stn, p->port, p->ctrl);
	
	if (tx_state == TX_IDLE) {
		uint8_t *q = p->ctrlbuf;
		q[0] = p->stn;
		q[1] = p->stn >> 8;
		q[2] = p->ctrl;
		q[3] = p->port;		
	
		if (p->bcst || !p->port) {			//Broadcast&/Immediate
			if (!p->port) {					//Immediate
				if (p->ctrl == 0x81 || p->ctrl == 0x88)	//Peek or Machine Type
					sp_set_state(SP_DATAWAIT);//Wait for data
				else
					sp_set_state(SP_SCOUTWAIT);//Wait for ACK
				
				sp_scout = p;
			}
		} else {
			sp_set_state(SP_SCOUTWAIT);		//Wait for ACK/NAK
			sp_scout = p;
		}

		uint8_t flag = p->bcst ? SP_FLAG_BROADCAST : SP_FLAG_SCOUT;
		
		tx_buf = q;
		tx_counter = p->ctrllen;
		tx_flag = flag;
		tx_state = TX_OPEN;
	} else
		printf("SP  : *** ERROR ALREADY TRANSMITTING!\n");		
	
	return 0;
}

	// Transmit data (having had scout acknowledged)
int sp_send_data(void) {
	printf("SP  : TX DATA FROM %04x, LEN=%d\n", sp_scout->stn, sp_scout->datalen - 2);

	if (tx_state == TX_IDLE) {
		sp_scout_t *p = sp_scout;
	
		p->databuf[0] = p->stn;
		p->databuf[1] = p->stn >> 8;

		tx_buf = p->databuf;
		tx_counter = p->datalen;
		tx_flag = SP_FLAG_DATA;
		tx_state = TX_OPEN;	
	
		sp_set_state(SP_ACKWAIT);
	} else
		printf("SP  : *** ERROR ALREADY TRANSMITTING!\n");			
	
	return 0;
}


	// End of valid frame (i.e. good CRC)
static void _end_of_frame() {
	//printf("FRAME: flag=%02x  buf_len=%d  data_len=%d\n", rx_flag, rx_counter, rx_head);
	
	if ((rx_flag == SP_FLAG_SCOUT && rx_counter >= 4) || (rx_flag != SP_FLAG_SCOUT && rx_counter == 2) ||
				(rx_flag == SP_FLAG_DATA && rx_counter >= 2)) {
		sp_scout_t f;
		
		f.stn = WORD(rx_buf, 0);
		f.ctrl = rx_buf[2];
		f.port = rx_buf[3];
		
		//printf("rx_buf: ");
		//for (int i = 0; i < rx_counter; i++)
		//	printf("%2x ", rx_buf[i]);
		//printf("\n");
		
		switch (rx_flag) {
			case SP_FLAG_DATA://ONLY USED IN RESPONSE TO PEEK OR MACHINE TYPE IMM OPS
				printf("SP  : RX IMM REPLY TO %04x, LEN=%d\n", f.stn, rx_counter - 2);
				
				if (sp_state == SP_DATAWAIT) {
					if (sp_scout->stn == f.stn) {
						printf("SP  : Data received, immediate request completed\n");
						printf("DATA LENGTH = %d :\n", rx_counter - 2);
						
						for (int i = 2; i < rx_counter; i++)
							printf("%02x ", rx_buf[i]);
						printf("\n");

						aun_sendreply(rx_buf, rx_counter);	

						sp_scout->result = 1;	// Succeeded
						sp_set_state(SP_IDLE);						
					} else
						printf("SP  : Wrong station: SRC=%04x DEST=%04x\n", sp_scout->stn, f.stn);		
				} else
					printf("SP  : Not expecting data!\n");
				break;
			case SP_FLAG_SCOUT:
				if (f.ctrl >= 0x80) {
					printf("SP  : RX SCOUT TO %04x, PORT=%02x CTRL=%02x, LEN=%d\n", f.stn, f.port, f.ctrl, rx_counter - 4);

					aun_send(rx_buf, rx_counter);
					sp_set_state(SP_IDLE);
				}
				break;
			case SP_FLAG_ACK:
				if (sp_state == SP_SCOUTWAIT || sp_state == SP_ACKWAIT) {
					if (sp_scout->stn == f.stn) {
						if (sp_state == SP_SCOUTWAIT) {
							printf("SP  : RX SCOUT ACK TO %04x\n", f.stn);

							// Scout ack
							// If not immediate, or immediate op is POKE, JSR, USER, or OS, send data.
							if (sp_scout->port || (sp_scout->ctrl >= 0x82 && sp_scout->ctrl <= 0x85)) {
								printf("SP  : Scout acked, send data frame...\n");
								sp_send_data();
								
							} else {//Immediate ops HALT, or CONT
								printf("Scout acked, immediate request completed\n");
								sp_scout->result = 1;

								if (!sp_optimistic)
									aun_sendack();

								sp_set_state(SP_IDLE);	

							}//Note, immediate ops PEEK, and MACHINE TYPE return a data frame.
							
						} else {//SP_ACKWAIT
							printf("SP  : RX DATA ACK TO %04x\n", f.stn);

							// Data frame ack
							sp_scout->result = 1;	// Succeeded

							printf("SP  : Data acked!\n");
							
							if (!sp_optimistic)
								aun_sendack();

							q_remove();//remove from queue
							
							sp_set_state(SP_IDLE);
							//printf("in buf count=%d\n", serial_get_count());

						}
					} else {
						printf("SP  : Wrong station: SRC=%04x DEST=%04x\n", sp_scout->stn, f.stn);
						sp_set_state(SP_IDLE);
					}
				} else {
					printf("SP  : Not expecting ACK!\n");
					sp_set_state(SP_IDLE);
				}
				break;
				
			case SP_FLAG_NAK:
				printf("SP  : RX SCOUT NAK TO %04x\n", f.stn);

				if (sp_state == SP_SCOUTWAIT) {
					if (sp_scout->stn == f.stn) {	// Implies no receive buffer for data.
						printf("SP  : Scout failed\n");
						sp_scout->result = -1;		// Failed
						sp_set_state(SP_IDLE);
						
						q_remove();//remove from queue
					} else
						printf("SP  : Wrong station!\n");
				} else
					printf("SP  : No expecting NAK!\n");
	
			break;
		}
	}
	//printf("exit _end_of_frame\n");
}

static void sp_put(uint8_t c, int docrc) {
	if (docrc)
		crc_calc(c, &tx_crc);
	
	serial_put(c);
	if (c == ESC_CHAR)
		serial_put(0);
}

void sp_poll(void) {
	uint8_t c, t;
	
	serial_poll();

	if (tx_state == TX_IDLE && serial_get_count() > 0) {
		serial_get(&c);
		//printf(".%02x", c);
		
		if (c == ESC_CHAR) {
			EscFlag = 1;
			//printf("ESC");
		} else {
			if (EscFlag) {
				EscFlag = 0;
				if (!c)
					c = ESC_CHAR;
				else {
					// Start &/ End of frame
					//printf("ESC(%02x) ", c);
					
					// Old frame?
					if (rx_crc)
						printf("SP  : RX BAD CRC!\n");
					else if (rx_state != RX_VOID)
						_end_of_frame();
					
					
					//printf("New frame %02x\n", c);
					
					// New frame
					if (c >= 0x80) {
						rx_flag = c;
						rx_counter = 0;
						rx_fifo_notfull = 2;
						rx_crc = 0;
						rx_state = RX_FLAG;
					} else
						rx_state = RX_VOID;
				}
			}
			
			if (rx_state != RX_VOID) {
				// FIFO
				crc_calc(c, &rx_crc);
				
				t = rx_fifo1;
				rx_fifo1 = rx_fifo0;
				rx_fifo0 = c;
				
				if (rx_fifo_notfull)
					rx_fifo_notfull--;
				else {
					switch (rx_state) {
						case RX_FLAG:	// First byte out of FIFO is the flag.
							rx_state = RX_DATA;
							break;
						case RX_DATA:
							if (rx_counter == RX_BUF_SIZE) { // Buffer full
								printf("SP RX BUFFER FULL!!!!\n");
								rx_state = RX_VOID;
							} else 
								rx_buf[rx_counter++] = t;
							break;
					}
				}
			}
		}
	}
	
	if (tx_state != TX_IDLE) {
		if (serial_put_free() > 10) {
			switch (tx_state) {
				case TX_OPEN:
					tx_tail = 0;
					tx_crc = 0;

					//serial_put(0);
					//serial_put(0);
					//serial_put(0);
					//serial_put(0);

					serial_put(ESC_CHAR);
					sp_put(tx_flag, 1);
					
					//printf("sp  : TX FLAG=%02x\n", tx_flag);
					tx_state = TX_DATA;
					printf("SP  : TX state = DATA\n");
					break;
				case TX_DATA:
					sp_put(tx_buf[tx_tail++], 1);
					if (tx_tail == tx_counter) {
						tx_state = TX_CLOSE;
						printf("SP  : TX state = CLOSE\n");
					}				
					break;
				case TX_CLOSE:
					sp_put(tx_crc >> 8, 0);
					sp_put(tx_crc, 0);
					
					serial_put(ESC_CHAR);
					serial_put(SP_FLAG_VOID);

					//serial_put(0);
					//serial_put(0);
					//serial_put(0);
					//serial_put(0);
					
					tx_state = TX_CLOSEWAIT;
					printf("SP  : TX state = CLOSEWAIT\n");
					break;
				case TX_CLOSEWAIT:
					if (serial_put_empty()) {
						tx_state = TX_CLOSEDELAY;//wait for beeb to catch a breath
						printf("SP  : TX state = CLOSEDELAY\n");
						sp_trigger = clock() + (CLOCKS_PER_SEC/SP_DELAY);
					}
					break;
				case TX_CLOSEDELAY:
					if (clock() > sp_trigger) {
						printf("SP  : TX state = IDLE\n");
						tx_state = TX_IDLE;
						if (sp_state == SP_IDLE)
							aun_receiver_enable(1);
						else
							sp_trigger = clock() + SP_TIMEOUT * CLOCKS_PER_SEC;
					}
					break;
			}
		}
	} else {
		if (sp_state != SP_IDLE) {
			if (clock() > sp_trigger) {
				printf("SP TRIGGER TIMEOUT\n");
				sp_set_state(SP_IDLE);
			}
		}

		if (sp_state == SP_IDLE)
			sp_check_queue();
	}
	

}