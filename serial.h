/* SPAUN                  */
/* serial.h               */
/* (c) 2022 Martin Mather */


#ifndef SERIAL_H
#define SERIAL_H

#include <arpa/inet.h>//for sockaddr_in
#include "constants.h"

class SerialClass {
	
	private:
		struct sockaddr_in si_me, si_other;
		int l_sock = 0, c_sock = 0;
		socklen_t slen = sizeof(si_other);

		int mystn, connected_flag, real_port, port_fd;
		string device;
		uint8_t txbuf[STXBUF_SIZE + 1], rxbuf[SRXBUF_SIZE];
		int txcount, trigger1, trigger2, baud_rate;
		unsigned int t1 = 0, t2 = 0;//used by trigger
		int ip_raw, flag_byte, not_clear_to_send = 1, not_request_to_send = 0;//handshaking

		
		void reset(void);
		void trigger_reset(void);
		void trigger_set(void);
		void send_nrts(void);
		
	public:
		class sbufClass {			
			public:
				uint8_t buf[SBUF_SIZE];
				int free, count, tail, head, overflow;
				void flush(void);
				int put(uint8_t *c);
				int get(uint8_t *c);
				int empty(void);
				
		} input, output;
		
		int bytes_in, bytes_out;

		int open(int stn, string device, int port);
		void close(void);
		void poll(void);
		void flush(void);
		int baud(int b_rate);
		void set_nrts(int nrts);
		int connected(void);
		
};

#endif
