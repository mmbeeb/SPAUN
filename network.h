/* SPAUN                  */
/* network.h              */
/* (c) 2022 Martin Mather */

#ifndef NETWORK_H
#define NETWORK_H

#include "constants.h"

struct net_t {
	int stn, ip_port;
	uint32_t in_addr;
	struct sockaddr_in si;
};

class NetworkClass {
	private:
		int count = 0;
		
	public:
		net_t stations[AUN_MAX_STATIONS];
		
		int add(int stn, string ip_addr);
		net_t *find(int stn);
		net_t *findsock(uint32_t in_addr, int ip_port);
};

extern NetworkClass mynetwork;

#endif
