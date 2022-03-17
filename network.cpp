/* SPAUN                  */
/* network.cpp            */
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
//#include <fcntl.h>
//#include <errno.h>
#include <time.h>

#include "network.h"

NetworkClass mynetwork;

int NetworkClass::add(int stn, string ip_addr) {
	//add station to network
	cout << "New station " << stn;

	if (stn > 0 && stn < 255) {
		if (!find(stn)) {
			if (count < AUN_MAX_STATIONS) {

				net_t *p = &stations[count++];
				p->stn = stn;
				p->ip_port = AUN_PORT_BASE + stn;
				
				sockaddr_in *sip = &p->si;
				memset((char *) sip, 0, sizeof(*sip));
				sip->sin_family = AF_INET;

				if (inet_aton(ip_addr.c_str(), &sip->sin_addr) == 0) {
					fprintf(stderr, "inet_aton() failed\n");
					exit(1);
				}

				sip->sin_port = htons(p->ip_port);
				p->in_addr = ntohl(sip->sin_addr.s_addr);
				
				cout << " - Station added with IP_ADDR=" << ip_addr << " IP_PORT=" << p->ip_port << endl;
				return 0;
			} else
				cout << " - Too many stations\n";
		} else
			cout << " - Station already exists\n";
	} else
		cout << " - Station number invalid\n";
	return -1;
}

net_t *NetworkClass::find(int stn) {
	//find station
	net_t *p = stations;
	for (int i = 0; i < count; i++, p++)
		if (p->stn == stn)
			return p;
	return 0;
}

net_t *NetworkClass::findsock(uint32_t in_addr, int ip_port) {
	//find net with ip address & port
	net_t *p = stations;
	for (int i = 0; i < count; i++, p++)
		if (p->in_addr == in_addr && p->ip_port == ip_port)
			return p;
	return 0;
}

// end of file
