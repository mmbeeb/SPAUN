/* SPAUN                  */
/* aun.c                  */
/* (c) 2022 Martin Mather */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>

#include "aun.h"
#include "queue.h"
#include "sp.h"


#define TXHANDLE	0x1000

static struct aun_t {
	uint32_t in_addr;
	struct sockaddr_in si;
	uint32_t rxhandle;
	clock_t rxtime;
	uint32_t txhandle;
	struct ebuf_t *txbuf;
	clock_t txtime;
	int txattempt;
} stations[AUN_MAX_STATIONS], *stnp;

static struct sockaddr_in si_me, si_other;
static int mysock, slen = sizeof(si_other);

static uint16_t mystn, otherstn;

static int rx_enable = 0, rxlen;

#define RXBUF_SIZE		AUN_HDR_SIZE+2048

//static uint8_t rxbuf1[RXBUF_SIZE], rxbuf2[RXBUF_SIZE];
static uint8_t *rxbuf = 0, txack[AUN_HDR_SIZE];


static void die(char *s) {
	perror(s);
	exit(1);
}

static void _opensock() {
	//create a UDP socket
	if ((mysock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		die("socket");
	
	//zero out the structure
	memset((char *) &si_me, 0, sizeof(si_me));
	
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(AUN_PORT_BASE + mystn);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//bind socket to port
	if (bind(mysock, (struct sockaddr*) &si_me, sizeof(si_me)) == -1)
		die("bind");

	//make socket non-blocking
	int flags = fcntl(mysock, F_GETFL, 0);
	if (flags == -1)
		die("get flags");

	if (fcntl(mysock, F_SETFL, flags | O_NONBLOCK) == -1)
		die("set flags");
}

void aun_receiver_enable(int state) {
	if (rx_enable != state) {
		printf("AUN : RX Enable = %d\n", state);
		rx_enable = state;
	}
}

static int set_stnp(uint16_t station) {
	if (station == 0xffff) {
		printf("AUN : CAN'T SEND BROADCASTS!\n");
	} else if (station < AUN_MAX_STATIONS) {
		stnp = &stations[station];
	
		struct sockaddr_in *sip = &stnp->si;
		if (!stnp->in_addr) {
			//Assume it's local
			memset((char *) sip, 0, sizeof(*sip));
			sip->sin_family = AF_INET;
			sip->sin_port = htons(AUN_PORT_BASE + station);
			
			if (inet_aton("127.0.0.1", &sip->sin_addr) == 0) {
				fprintf(stderr, "inet_aton() failed\n");
				exit(1);
			}
			
			stnp->in_addr = ntohl(sip->sin_addr.s_addr);
			stnp->txhandle = TXHANDLE;
		}
		return 0;
	} else
		printf("AUN: Station number (%04x) > MAX\n", station);
	
	return -1;
}

int aun_send(uint8_t *p, int plen) {
	//Convert header from SP to AUN and transmit
	uint16_t station = p[1] << 8 | p[0];

	if (!set_stnp(station)) {
		uint8_t aun_type = AUN_TYPE_UNICAST;
		
		if (!p[3])
			aun_type = AUN_TYPE_IMMEDIATE;

		uint32_t handle = (stnp->txhandle += 4);
		uint8_t *q = p - 4;

		q[0] = aun_type;
		q[1] = p[3];			//Port
		q[2] = p[2] & 0x7f;		//Control
		q[3] = 0;
		q[4] = handle & 0xff;
		q[5] = (handle >> 8) & 0xff;
		q[6] = (handle >> 16) & 0xff;
		q[7] = (handle >> 24) & 0xff;	
		
		if (aun_type == AUN_TYPE_UNICAST)
			printf("AUN : TX UNICAST");
		else
			printf("AUN : TX IMM REQUEST");
			
		printf(" TO %04x, PORT=%02x, CTRL=%02x, HANDLE=%08x, LEN=%d\n", station, q[1], q[2], handle, plen - 4);

		if (sendto(mysock, q, plen + 4, 0, (struct sockaddr*) &stnp->si, slen) == -1)
			die("sendto()");	
	} else
		printf("AUN send: station?\n");
}

void aun_sendack(void) {
	uint32_t handle	= txack[7] << 24 | txack[6] << 16 | txack[5] << 8 | txack[4];//for info only
	printf("AUN : TX ACK TO %04x, HANDLE=%08x\n", otherstn, handle);
	
	txack[0] = AUN_TYPE_ACK;
	
	//printf("AUN: ");
	//for (int i = 0; i < AUN_HDR_SIZE; i++)
	//	printf("%02x ", rxbuf[i]);
	//printf("\n");
	
	if (sendto(mysock, txack, AUN_HDR_SIZE, 0, (struct sockaddr*) &si_other, slen) == -1)
		die("sendto()");
}

void aun_sendreply(uint8_t *p, int plen) {
	//Reply to immediate request
	uint32_t handle	= txack[7] << 24 | txack[6] << 16 | txack[5] << 8 | txack[4];//for info only
	printf("AUN : TX REPLY TO %04x, HANDLE=%08x\n", otherstn, handle);

	uint8_t *q = p - 6;
	
	for (int i = 0; i < AUN_HDR_SIZE; i++)
		q[i] = txack[i];
	
	q[0] = AUN_TYPE_IMM_REPLY;
	
	if (sendto(mysock, q, plen + 6, 0, (struct sockaddr*) &si_other, slen) == -1)
		die("sendto()");
}

static int _gotdata(void) {
	//uint8_t *rxbuf = rxbuf1;
	uint8_t port;
	uint32_t handle;

	/*
	printf("ip=%08x\n", stnp->in_addr);
	for (int i = 0; i < rxlen; i++)
		printf("%02x ", rxbuf[i]);
	printf("\n");
	*/

	port = rxbuf[1];
	handle = rxbuf[7] << 24 | rxbuf[6] << 16 | rxbuf[5] << 8 | rxbuf[4];

	printf("AUN : RX type=%02x port=%02x cb=%02x handle=%08x\n", rxbuf[0], port, rxbuf[2], handle);

	if ((handle > stnp->rxhandle) || (clock() > stnp->rxtime)) {
		switch (rxbuf[0]) {// type
			case AUN_TYPE_BROADCAST:
				printf("BROADCAST\n");
				break;

			case AUN_TYPE_UNICAST:
			case AUN_TYPE_IMMEDIATE:
				if (rxbuf[0] == AUN_TYPE_UNICAST)				
					printf("AUN : RX UNICAST FROM %04x, LEN=%d\n", otherstn, rxlen-AUN_HDR_SIZE);
				else
					printf("AUN : RX IMM REQUEST FROM %04x, CTRL=%02x\n", otherstn, rxbuf[2]);
				
				stnp->rxhandle = handle;
				stnp->rxtime = clock() + AUN_RXTIMEOUT * CLOCKS_PER_SEC;
				
				//aun_receiver_enable(0);
				
				for (int i = 0; i < AUN_HDR_SIZE; i++)
					txack[i] = rxbuf[i];				//Copy the AUN header in case we need to send a reply.
				
				if (!q_append(otherstn, rxbuf, rxlen)) {
					rxbuf = 0;//Buffer handed over
					if (sp_optimistic)
						aun_sendack();//SEND IT NOW
				} else
					printf("COULD NOT ADD TO QUEUE\n");
				
				//sp_send_unicast(otherstn, rxbuf, rxlen);
				break;

			case AUN_TYPE_ACK:
				printf("AUN : RX ACK FROM %04x\n", otherstn);

				aun_receiver_enable(0);			
				sp_send_ack(otherstn);
				break;

			case AUN_TYPE_IMM_REPLY:
				printf("AUN : RX IMM REPLY FROM %04x, LEN=%d\n", otherstn, rxlen-AUN_HDR_SIZE);

				aun_receiver_enable(0);
				sp_send_reply(otherstn, rxbuf, rxlen);//we need to disable aun receiver until data sent over sp
				break;

			default:
				printf("AUN : RX TYPE?\n");
				break;
		}
	} else {
		printf("AUN : RX DUPLICATE!\n");
	}
}

static int _receiver(void) {
	//uint8_t *rxbuf = rx_enable ? rxbuf1 : rxbuf2;
	
	if (!rxbuf)
		rxbuf = malloc(RXBUF_SIZE);
	
	int len = recvfrom(mysock, rxbuf, RXBUF_SIZE, 0, (struct sockaddr *) &si_other, &slen);

	if (len == -1) {
		if (errno != EWOULDBLOCK) 
			die("recvfrom()");
	} else if (len >= AUN_HDR_SIZE) {
		printf("Received packet from %s:%d length=%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), len);
		
		otherstn = ntohs(si_other.sin_port) - AUN_PORT_BASE;
		//printf("stn=%d\n", otherstn);
		
		if (otherstn < AUN_MAX_STATIONS) {
			if (otherstn == mystn) 
				printf("AUN : Duplicate station %d\n", otherstn);
			else {
				stnp = &stations[otherstn];
				uint32_t in_addr = ntohl(si_other.sin_addr.s_addr);
				if (stnp->in_addr == 0) {
					printf("AUN : New station\n");
					stnp->in_addr = in_addr;
					stnp->si = si_other;
					stnp->txhandle = TXHANDLE;
				}
				
				if (stnp->in_addr != in_addr)
					printf("AUN : Duplicate station %d\n", otherstn);
				else {
					//printf("Station OK\n");
					//if (rx_enable) {
						rxlen = len;
						_gotdata();
					//} else
					//	printf("DATA DUMPED!!!!\n");
				}
			}
		} else
			printf("AUN : Station number out of range\n");
	}

}

int aun_open(uint16_t stn) {
	printf("aun_open stn=%d\n", stn);

	mystn = stn;	// remember my station number
	_opensock();
}

int aun_close(void) {
	printf("aun_close\n");
	if (rxbuf) {
		free(rxbuf);
		rxbuf = 0;
	}
	q_flush();
	close(mysock);
}

int aun_poll(void) {
	_receiver();
}

