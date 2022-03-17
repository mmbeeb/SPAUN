/* SPAUN                  */
/* aun.cpp                */
/* (c) 2022 Martin Mather */

#include <iostream>
#include <string>
using namespace std;

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
#include "sp.h"

#define MON(msg) {if (monitor_flag) cout << "AUN (" << mystn << ") : " << msg << endl;}

static void die(const char *s) {
	perror(s);
	exit(1);
}

AUNClass::aun_t *AUNClass::StationClass::get(int stn) {
	
	if (stn != mystn) {//From myself?

		aun_t *p = stations;
		for (int i = 0; i < count; i++, p++)
			if (p->stn == stn)
				return p;
		
		//Not found so create?
		if (mynetwork.find(stn)) {//If station exists in network
			MON("New station " << stn)
			p = &stations[count++];
			p->stn = stn;
			p->rxhandle = 0;
			p->rxtime = 0;
			p->txhandle = AUN_TXHANDLE;
			return p;
		}
	}	

	return 0;
}

int AUNClass::StationClass::rxhandle(uint32_t handle) {
	//Is the handle from active station valid?
	int v = handle > active->rxhandle || time(0) > active->rxtime;
	if (v) {
		active->rxhandle = handle;
		active->rxtime = time(0) + AUN_RXTIMEOUT;
	}
	
	return v;
}

void AUNClass::StationClass::activestn(int stn) {
	//Called when sending
	active = get(stn);

	if (!active)
		MON("Station " << stn << " invalid ");
}

void AUNClass::StationClass::activesock(sockaddr_in *si_other) {
	//Called when receiving
	active = 0;

	//Find station in network
	uint32_t in_addr = ntohl(si_other->sin_addr.s_addr);
	int ip_port = ntohs(si_other->sin_port);	
	net_t *net = mynetwork.findsock(in_addr, ip_port);
	
	if (net)//If station exists
		active = get(net->stn);
		
	if (!active)
		MON("Unknown network or station")
}

void AUNClass::monitor(int flag) {
	monitor_flag = flag;
	otherstations.monitor_flag = flag;
}

void AUNClass::opensock(int port) {
	//create a UDP socket
	if ((mysock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		die("socket");
	
	//zero out the structure
	memset((char *) &si_me, 0, sizeof(si_me));
	
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(port);
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

void AUNClass::send22(struct queue_t *u) {
	//Populate AUN header and transmit

	if (!u->handle)
		u->handle = (otherstations.active->txhandle += 4);
	
	uint8_t *p = u->buf;

	p[0] = u->type; //aun_type;
	p[1] = u->port;			//Port
	p[2] = u->ctrl & 0x7f;		//Control
	p[3] = 0;
	
	p[4] = u->handle & 0xff;
	p[5] = (u->handle >> 8) & 0xff;
	p[6] = (u->handle >> 16) & 0xff;
	p[7] = (u->handle >> 24) & 0xff;	
	
	string aun_types[7] = {"", "BROADCAST", "UNICAST", "ACK", "NAK", "IMMEDIATE", "IMM_REPLY"};
	MON("TX " << aun_types[u->type] << " TO " << u->otherstn << hex
		<< " PORT=" << +u->port << " CTRL=" << +u->ctrl << " HANDLE="
		<< u->handle << dec << " LEN=" << u->len - AUN_HDR_SIZE)

	net_t *net = mynetwork.find(u->otherstn);

	if (net) {
		MON("Send packet to ip=" << inet_ntoa(net->si.sin_addr)
				<< " port=" << ntohs(net->si.sin_port) << " len=" << u->len)
				
		if (sendto(mysock, p, u->len, 0, (struct sockaddr*) &net->si, slen) == -1)
			MON("sendto() failed")
	}
}

int AUNClass::send(struct queue_t *u) {
	//If this returns with 0 it has claimed ownership of the malloc block in the queue_t.
	otherstations.activestn(u->otherstn);
	
	if (otherstations.active) {
		switch (u->type) {
			case AUN_TYPE_BROADCAST:
				break;
			case AUN_TYPE_UNICAST:
			case AUN_TYPE_IMMEDIATE:
			case AUN_TYPE_IMM_REPLY:
			case AUN_TYPE_ACK:
				send22(u);
				break;
		}
	} else
		MON("Send to station?");
		
	return -1;	
}

int AUNClass::gotdata(void) {
	rx_q.type = rxbuf[0];
	rx_q.handle = rxbuf[7] << 24 | rxbuf[6] << 16 | rxbuf[5] << 8 | rxbuf[4];

	MON("RX type=" << hex << rx_q.type << " port=" << +rx_q.port << " ctrl=" << +rx_q.ctrl << " handle=" << rx_q.handle << dec)
	
	int duplicate = 0;
	if (rx_q.type == AUN_TYPE_BROADCAST || rx_q.type == AUN_TYPE_UNICAST || rx_q.type == AUN_TYPE_IMMEDIATE) {
		//handle originated from other station so check it
		duplicate = !otherstations.rxhandle(rx_q.handle);
	}

	if (!duplicate) {
		rx_q.otherstn = otherstations.active->stn;		
		rx_q.ctrl = rxbuf[2];
		rx_q.port = rxbuf[1];		
		rx_q.buf = rxbuf;
		rx_q.len = rxlen;

		return 0;
	} else
		MON("RX DUPLICATE\n")
	
	return -1;
}

int AUNClass::poll(void) {
	//Returns with 0 if data received, with rx_q populated
	if (!rxbuf)
		rxbuf = (uint8_t*) malloc(AUN_RXBUFLEN);
	
	int len = recvfrom(mysock, rxbuf, AUN_RXBUFLEN, 0, (struct sockaddr *) &si_other, &slen);

	if (len == -1) {
		if (errno != EWOULDBLOCK) 
			die("recvfrom()");
	} else if (len >= AUN_HDR_SIZE) {
		MON("Received packet from ip=" << inet_ntoa(si_other.sin_addr) << " port=" << ntohs(si_other.sin_port) << " len=" << len)

		otherstations.activesock(&si_other);
		if (otherstations.active) {//valid address
			rxlen = len;
			return gotdata();
		}
	}

	return -1;
}

int AUNClass::open(int stn) {
	MON("Open station " << stn)

	mystn = stn;// remember my station number
	otherstations.mystn = stn;
	opensock(AUN_PORT_BASE + stn);
	return 0;
}

void AUNClass::close(void) {
	MON("Close")
	
	if (rxbuf) {
		free(rxbuf);
		rxbuf = 0;
	}
	::close(mysock);
}

// end of file

