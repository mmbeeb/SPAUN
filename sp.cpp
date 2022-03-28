/* SPAUN                  */
/* sp.cpp                 */
/* (c) 2022 Martin Mather */

#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>

using namespace std;

#include <stdio.h> 
#include <stdint.h>
#include <stdlib.h> 
#include <time.h>

#include "sp.h"

// Not happy with MON.  It is a bit of a kludge but it seems to work.
#define MON(msg) {if (monitor_flag) cout << "SP  (" << mystn <<") : " << msg << endl;}

#define CRC_POLY	0x11021
#define ESC_CHAR	0xe5

/*
void crc_noise(uint32_t *crc) {
	if (!(rand() % 20)) {
		*crc = 0xffff;
		cout << "*\n";
	}
}*/

SPCollectionClass mysp;

int SPCollectionClass::open(int stn, string device, int serial_ip_port) {
	if (count < SP_MAX_STATIONS) {
		stations[count] = make_unique<SPClass>();
		return stations[count++]->open(stn, device, serial_ip_port);
	}
	return -1;
};

void SPCollectionClass::test(void) {
	for (int i = 0; i < count; i++)
		stations[0]->test();
}

void SPCollectionClass::close(void) {
	for (int i = 0; i < count; i++)
		stations[i]->close();
};

void SPCollectionClass::reset(void) {
	for (int i = 0; i < count; i++)
		stations[i]->reset();
}

void SPCollectionClass::poll(void) {
	for (int i = 0; i < count; i++)
		stations[i]->poll();
}

int SPCollectionClass::connected(void) {
	int r = 0;
	for (int i = 0; i < count; i++)
		if (stations[i]->connected()) {
			cout << "Station " << stations[i]->mystn << " still connected\n";
			r = 1;
		}
	return r;
}

void SPCollectionClass::monitor(int flag) {
	for (int i = 0; i < count; i++)
		stations[i]->monitor(flag);
}

void SPClass::CRCClass::calc(uint8_t c) {
	crc = crc ^ (c << 8);
	for (int i=0; i<8; i++) {
		if (crc & 0x8000)
			crc = (crc << 1) ^ CRC_POLY;
		else
			crc = crc << 1;
	}
}

void SPClass::monitor(int flag) {
	monitor_flag = flag;
	aun.monitor(flag);
	//cout << "MONITOR = " << monitor_flag << endl;
}

int SPClass::connected(void) {
	return serial.connected();
}

int SPClass::open(int stn, string device, int serial_ip_port) {
	MON("Open : station=" << stn << " device=" << device << " port=" << serial_ip_port)
	mystn = stn;
	int r = serial.open(stn, device, serial_ip_port);
	if (!r)
		aun.open(stn);
	
	return r;
}

void SPClass::close(void) {
	MON("Close")
	tx_q.flush();
	serial.close();
	aun.close();
}

void SPClass::reset(void) {
	MON("Reset")
	tx_q.flush();
	set_state(SP_IDLE);
	EscFlag = 0;
	rx_state = RX_VOID;
}

void SPClass::set_state(sp_state_t new_state) {
	/*if (monitor_flag && sp_state != new_state) {
		cout << "SP  (" << mystn <<") : STATE = ";
		switch(new_state) {
			case SP_IDLE:
				cout << "IDLE\n";
				break;
			case SP_DATAWAIT:
				cout << "DATAWAIT";
				break;
			case SP_SCOUTWAIT:
				cout << "SCOUTWAIT";
				break;
			case SP_ACKWAIT:
				cout << "ACKWAIT";
				break;
			case SP_RESET:
				cout << "RESET";
				break;
			case SP_RESETWAIT:
				cout << "RESETWAIT";
				break;
			case SP_REMOTEACKWAIT:
				cout << "REMOTEACKWAIT";
		}
		cout << endl;
	}*/
	sp_state = new_state;

	if (sp_state == SP_IDLE) {
		set_tx_state(TX_IDLE);
	}
}

void SPClass::set_tx_state(tx_state_t new_state) {
/*	if (monitor_flag && tx_state != new_state) {
		cout << "SP  (" << mystn <<") : TX state = ";
		switch (new_state) {
			case TX_IDLE:
				cout << "IDLE";
				break;
			case TX_OPEN:
				cout << "OPEN";
				break;
			case TX_DATA:
				cout << "DATA";
				break;
			case TX_CLOSE:
				cout << "CLOSE";
				break;
			case TX_CLOSEWAIT:
				cout << "CLOSEWAIT";
				break;
			case TX_CLOSEDELAY:
				cout << "CLOSEDELAY";
				break;
		}
		cout << endl;
	}*/
	tx_state = new_state;
}

void SPClass::aun_ack(queue_t *u) {
	//Send ack to AUN
	queue_t q;
	
	q = *u;//copy handle etc.
	q.type = AUN_TYPE_ACK;					
	q.buf = tx_aunack;
	q.len = AUN_HDR_SIZE;
	aun.send(&q);
}

int SPClass::send(queue_t *u) {
	//If this returns with 0 it has claimed ownership of the malloc block in the queue_t.
	int r = -1;
	switch (u->type) {
		case AUN_TYPE_BROADCAST:
			break;
		case AUN_TYPE_UNICAST:
			r = tx_q.append(u);//Add to queue.
			if (!r && optimistic)
				aun_ack(u);//Send ACK now!
			break;			
		case AUN_TYPE_IMMEDIATE:
			r = tx_q.append(u);//Add to queue.
			if (!r && optimistic && u->ctrl != ECONET_PEEK && u->ctrl != ECONET_MACHINEPEEK)//Send ACK now?
				aun_ack(u);
			break;			
		case AUN_TYPE_IMM_REPLY:
			if (tx_q.empty())//Only add if queue empty.
				r = tx_q.append(u);
			break;
		case AUN_TYPE_ACK://Acks are not queued
			send_ack(u->otherstn);
			break;
	}
	return r;
}

void SPClass::check_queue(void) {
	//TX is idle, is there anything in the queue?
	tx_u = tx_q.get();
	if (tx_u) {
		if (!tx_u->type)
			tx_q.remove();
		else {
			//cout << "SP  : There's something in the queue!\n";
			//TRANSMIT IT OVER SP!
	
			if (tx_u->type == AUN_TYPE_IMM_REPLY)
				send_reply();
			else
				send_scout();

			if (!optimistic || tx_u->type != AUN_TYPE_UNICAST)
				tx_u->type = 0;//One shot!
		}
	}
}

int SPClass::tx_open(int flag, int stn, uint8_t *buf, int len) {
	//Begin transmission
	MON("tx_open flag=" << hex << flag << dec << " stn=" << stn << " len=" << len << endl)
	
	if (tx_state != TX_IDLE)
		MON("tx_open *** WARNING : ALREADY TRANSMITTING!")
	else {
		buf[0] = stn;
		buf[1] = stn >> 8;

		tx_flag = flag;		
		tx_buf = buf;
		tx_counter = len;
		set_tx_state(TX_OPEN);
		return 0;
	}
	
	return -1;
}

void SPClass::send_scout(void) {
	// Transmit scout (Immediate/Data)
	MON("TX SCOUT FROM " << tx_u->otherstn << hex << " PORT=" << +tx_u->port << " CTRL=" << +tx_u->ctrl << dec)

	int reqlen[] = {0, 8, 8, 4, 4, 4, 0, 0, 4};

	tx_ctrllen = 4;		
	if (!tx_u->port && tx_u->ctrl >= 1 && tx_u->ctrl <= 8) {
		tx_ctrllen += reqlen[tx_u->ctrl];
	}
	
	uint8_t *p = tx_u->buf + 4;

	p[2] = tx_u->ctrl | 0x80;
	p[3] = tx_u->port;
	
	if (!tx_open(SP_FLAG_SCOUT, tx_u->otherstn, p, tx_ctrllen)) {
		sp_state_t s = SP_SCOUTWAIT;//Wait for ACK/NAK
		if (!tx_u->port) {//Immediate
			if (tx_u->ctrl == 1 || tx_u->ctrl == 8)//Peek or Machine Type
				s = SP_DATAWAIT;//Wait for data
		}
		set_state(s);
	}
}

void SPClass::send_data(void) {
	// Transmit data (having had scout acknowledged)
	MON("TX DATA FROM " << tx_u->otherstn)

	int l = 4 + tx_ctrllen - 2;
	if (!tx_open(SP_FLAG_DATA, tx_u->otherstn, tx_u->buf + l, tx_u->len - l))
		set_state(SP_ACKWAIT);
}

void SPClass::send_ack(int stn) {
	// Transmit ACK
	MON("TX ACK FROM " << stn)
	
	if (sp_state == SP_REMOTEACKWAIT) {
		set_state(SP_IDLE);
		tx_open(SP_FLAG_ACK, stn, tx_ackbuf, 2);
	} else
		MON("Unexpected ACK")
}

void SPClass::send_reply(void) {
	// Transmit Immediate Reply
	MON("TX IMM REPLY FROM " << tx_u->otherstn << " LEN=" << tx_u->len)
	
	if (sp_state == SP_REMOTEACKWAIT) {
		set_state(SP_IDLE);
		tx_open(SP_FLAG_DATA, tx_u->otherstn, tx_u->buf + 6, tx_u->len - 6);
	} else
		MON("Unexpected reply")
}

void SPClass::send_reset(void) {
	// Transmit Reset
	MON("TX RESET, MY STN=" << mystn)
	tx_ackbuf[2] = 0;
	tx_ackbuf[3] = 0;
	
	if (!tx_open(SP_FLAG_RESET, mystn, tx_ackbuf, 4))
		set_state(SP_RESET);
}

void SPClass::test(void) {
	//send_reset();
}

void SPClass::end_of_frame(void) {
	// End of valid frame (i.e. good CRC)
	//printf("FRAME: flag=%02x  buf_len=%d\n", rx_flag, rx_counter);
	
	if ((rx_flag == SP_FLAG_SCOUT && rx_counter >= 4) || (rx_flag != SP_FLAG_SCOUT && rx_counter == 2) ||
				(rx_flag == SP_FLAG_DATA && rx_counter >= 2)) {

		int ackflag = 0;
		queue_t q;

		q.otherstn = rx_buf[1] << 8 | rx_buf[0];
		q.port = rx_buf[3];
		q.ctrl = rx_buf[2] & 0x7f;
		q.handle = 0;
		q.buf = rx_buf - 4;
		q.len = rx_counter + 4;
		
		//printf("rx_buf: ");
		//for (int i = 0; i < rx_counter; i++)
		//	printf("%2x ", rx_buf[i]);
		//printf("\n");
		
		switch (rx_flag) {
			case SP_FLAG_RESET:
				MON("RX RESET\n")
				reset();
				//send_reset();
				//sending it too quickly fails
				trigger = clock() + (CLOCKS_PER_SEC/SP_TXDELAY);
				set_state(SP_RESETWAIT);
				break;
				
			case SP_FLAG_DATA://ONLY USED IN RESPONSE TO PEEK OR MACHINE TYPE IMM OPS
				MON("RX IMM REPLY TO " << q.otherstn << " LEN=" << rx_counter - 2)
				
				if (sp_state == SP_DATAWAIT) {
					if (tx_u->otherstn == q.otherstn) {
						MON("Data received, immediate request completed")
						//printf("DATA LENGTH = %d :\n", rx_counter - 2);
						
						//for (int i = 2; i < rx_counter; i++)
						//	printf("%02x ", rx_buf[i]);
						//printf("\n");

						q = *tx_u;//copy handle etc.
						q.type = AUN_TYPE_IMM_REPLY;
						q.buf = rx_buf - 6;
						q.len = rx_counter + 6;
						aun.send(&q);

						set_state(SP_IDLE);						
					} else
						MON("Wrong station: SRC=" << tx_u->otherstn << " DEST=" << q.otherstn)
				} else
					MON("Not expecting data!")
				break;
				
			case SP_FLAG_SCOUT:
				MON("RX SCOUT TO " << q.otherstn << hex << " PORT=" 
						<< +q.port << " CTRL=" << +q.ctrl << dec << " LEN=" << rx_counter - 4)
						
				q.type = !q.port ? AUN_TYPE_IMMEDIATE : AUN_TYPE_UNICAST;					
				aun.send(&q);
				set_state(SP_REMOTEACKWAIT);
				trigger = clock() + SP_RXTIMEOUT * CLOCKS_PER_SEC;
				break;
				
			case SP_FLAG_ACK:
				if (sp_state == SP_SCOUTWAIT || sp_state == SP_ACKWAIT) {
					if (tx_u->otherstn == q.otherstn) {
						ackflag = 1;
						if (sp_state == SP_SCOUTWAIT) {
							MON("RX SCOUT ACK TO " << q.otherstn)

							// Scout ack
							// If not immediate, or immediate op is POKE, JSR, USER, or OS, send data.
							if (tx_u->port || (tx_u->ctrl >= 2 && tx_u->ctrl <= 5)) {
								//printf("SP  : Scout acked, send data frame...\n");
								send_data();
								ackflag = 0;
							} else {//Immediate ops HALT, or CONT
								MON("Scout acked, immediate request completed")
							}//Note, immediate ops PEEK, and MACHINE TYPE return a data frame.
							
						} else //SP_ACKWAIT
							MON("RX DATA ACK TO " << q.otherstn)
						
						if (ackflag && !optimistic)
							aun_ack(tx_u);
					} else {
						MON("Wrong station: SRC=" << tx_u->otherstn << " DEST=" << q.otherstn)
						set_state(SP_IDLE);
					}
				} else {
					MON("Not expecting ACK!")
					set_state(SP_IDLE);
				}
				break;
				
			case SP_FLAG_NAK:
				MON("RX SCOUT NAK TO " << q.otherstn)

				if (sp_state == SP_SCOUTWAIT) {
					ackflag = (tx_u->otherstn == q.otherstn);// Implies no receive buffer for data.

					if (ackflag)
						MON("Scout failed")
					else
						MON("Wrong station!")
				} else
					MON("Not expecting NAK!")
	
			break;
		}
		
		if (ackflag) {
			tx_q.remove();//remove from queue
			set_state(SP_IDLE);
		}
	}
}

void SPClass::put_raw(uint8_t c) {
	//cout << hex << +c << " " << dec;
	serial.output.put(&c);
}

void SPClass::put(uint8_t c, int docrc) {
	if (docrc)
		tx_crc.calc(c);

	put_raw(c);
	if (c == ESC_CHAR)
		put_raw(0);
}

void SPClass::poll(void) {
	uint8_t c, t;
	
	serial.poll();
	
	if (!aun.poll()) {
		if (!send(&aun.rx_q))
			aun.rxbuf = 0;//Buffer handed over (else it just gets reused)
	}

	if (tx_state == TX_IDLE && !serial.input.empty()) {
		serial.input.get(&c);
		//cout << hex << +c << " " << dec;
		
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
					
					//crc_noise(&rx_crc.crc);
					
					// Old frame?
					if (rx_state != RX_VOID) {
						++frame_count;
						if (!rx_crc.crc)
							end_of_frame();
						else {
							++bad_count;
							MON("****** RX BAD CRC #" << bad_count << " total frames=" << frame_count)
						}
					}
				
					//MON("New frame " << hex << +c << dec);
					
					// New frame
					if (c >= 0x80) {
						rx_flag = c;
						rx_counter = 0;
						rx_fifo_notfull = 2;
						rx_crc.crc = 0;
						rx_state = RX_FLAG;
					} else
						rx_state = RX_VOID;
				}
			}
			
			if (rx_state != RX_VOID) {
				// FIFO
				rx_crc.calc(c);
				
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
							if (rx_counter == SP_RXBUFLEN) { // Buffer full
								MON("RX BUFFER FULL!!!!")
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
		if (serial.output.free > 10) {
			switch (tx_state) {
				case TX_OPEN:
					tx_tail = 0;
					tx_crc.crc = 0;

					put_raw(ESC_CHAR);
					put(tx_flag, 1);
					
					set_tx_state(TX_DATA);
					break;
				case TX_DATA:
					put(tx_buf[tx_tail++], 1);
					if (tx_tail == tx_counter)
						set_tx_state(TX_CLOSE);			
					break;
				case TX_CLOSE:
					put(tx_crc.crc >> 8, 0);
					put(tx_crc.crc, 0);

					put_raw(ESC_CHAR);
					put_raw(SP_FLAG_VOID);
					
					set_tx_state(TX_CLOSEWAIT);
					break;
				case TX_CLOSEWAIT:
					if (serial.output.empty()) {
						set_tx_state(TX_CLOSEDELAY);//wait for beeb to catch a breath
						trigger = clock() + (CLOCKS_PER_SEC/SP_TXDELAY);
					}
					break;
				case TX_CLOSEDELAY:
					if (clock() > trigger) {
						set_tx_state(TX_IDLE);
						if (sp_state != SP_IDLE)
							trigger = clock() + SP_TIMEOUT * CLOCKS_PER_SEC;
					}
					break;
			}
		}
	} else {
		if (sp_state != SP_IDLE) {
			if (clock() > trigger) {
				//MON("TRIGGER TIMEOUT");
				if (sp_state == SP_RESETWAIT)
					send_reset();
				else
					set_state(SP_IDLE);
			}
		}

		if (sp_state == SP_IDLE)
			check_queue();
	}
}

// End of file
