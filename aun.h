/* SPAUN                  */
/* aun.h                  */
/* (c) 2022 Martin Mather */

#ifndef AUN_H
#define AUN_H

#include <arpa/inet.h>//for sockaddr_in
#include "constants.h"
#include "queue.h"
#include "network.h"

class AUNClass {
	
	private:
		struct sockaddr_in si_me, si_other;
		int mysock;
		socklen_t slen = sizeof(si_other);
		int rxlen, mystn;
		uint8_t txack[AUN_HDR_SIZE];

		void opensock(int port);
		int gotdata(void);
		void send22(queue_t *qptr);

		struct aun_t {
			int stn;
		
			uint32_t rxhandle;
			time_t rxtime;
			
			uint32_t txhandle;
		};

		class StationClass {
			
			private:
				int count = 0, top = 0;
				aun_t *get(int stn);
				
			public:
				int monitor_flag = 0, mystn;
				aun_t stations[AUN_MAX_STATIONS], *active;
				int rxhandle(uint32_t handle);
				void activestn(int stn);
				void activesock(struct sockaddr_in *si_other);
				
		} otherstations;

	public:
		int monitor_flag = 0;
		uint8_t *rxbuf = 0;
		queue_t rx_q;
		
		int open(int station);
		void close(void);
		int poll(void);
		int send(struct queue_t *qptr);
		void monitor(int flag);
		
};

#endif
