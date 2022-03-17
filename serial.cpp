/* SPAUN                  */
/* serial.cpp             */
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
#include <time.h>
#include <termios.h>

#include "serial.h"

#define BACKLOG		1		//how many pending connections queue will hold

static void die(const char *s) {
	perror(s);
	exit(1);
}

int SerialClass::connected(void) {
	return connected_flag;
}

void SerialClass::trigger_reset(void) {
	trigger1 = 0;
	trigger2 = 0;
}

void SerialClass::trigger_set(void) {
	unsigned int n1, n2;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	n1 = ts.tv_nsec / 10000000;		//centiseconds
	
	//This is the maximum number of bytes the Beeb can handle per second
	n2 = (ts.tv_nsec / (1000000000/SBYTES_PER_SEC));

	trigger1 = trigger1 || (n1 != t1);
	trigger2 = trigger2 || (n2 != t2);
	t1 = n1;
	t2 = n2;
}

void SerialClass::send_nrts(void) {
	if (connected_flag && !ip_raw) {
		uint8_t buf[2];
		buf[0] = 0xff;
		buf[1] = not_request_to_send;
		if (send(c_sock, &buf, 2, 0) == -1)
			die("Serial: send rts");
		//printf("[nRTS=%d]\n", buf[1]);
	}
}

void SerialClass::set_nrts(int nrts) {
	if (!ip_raw) {
		not_request_to_send = nrts;
		send_nrts();
	}
}

void SerialClass::sbufClass::flush(void) {
	free = SBUF_SIZE;
	count = 0;
	tail = 0;
	head = 0;
	overflow = 0;
}

int SerialClass::sbufClass::put(uint8_t *c) {
	if (free == 0) {
		printf("Serial: overflow\n");
		overflow = 1;
		return -1;
	}

	buf[head++] = *c;
	head %= SBUF_SIZE;
	count++;
	free--;
	return 0;
}

int SerialClass::sbufClass::get(uint8_t *c) {
	if (count == 0)
		return -1;

	*c = buf[tail++];
	tail %= SBUF_SIZE;
	count--;
	free++;
	overflow = 0;
	return 0;
}

int SerialClass::sbufClass::empty(void) {
	return count == 0;
}

void SerialClass::reset(void) {
	input.flush();
	output.flush();

	not_request_to_send = 0;	
	flag_byte = 0;
	txcount = 0;
	
	trigger_reset();

	if (!ip_raw)
		send_nrts();
}

int SerialClass::baud(int b_rate) {
	const int bauds[] = {75, 150, 300, 1200, 2400, 4800, 9600, 19200};

	if (b_rate > 0 && b_rate <= 8) {
		baud_rate = bauds[b_rate - 1];
		//cout << "Serial (" << mystn << "): baud rate = " << baud_rate << endl;
		trigger_reset();

		if (real_port) {
			const int sbauds[]= {B75, B150, B300, B1200, B2400, B4800, B9600, B19200};
			struct termios options;
			tcgetattr(port_fd, &options);//Get the current options
		
			//Set the baud rates to 19200...
			
			cfsetispeed(&options, sbauds[b_rate - 1]);
			cfsetospeed(&options, sbauds[b_rate - 1]);
		
			cfmakeraw(&options);
			options.c_cflag |= (CLOCAL | CREAD);// Enable the receiver and set local mode

			// No parity (8N1)
			options.c_cflag &= ~PARENB;
			options.c_cflag &= ~CSTOPB;
			options.c_cflag &= ~CSIZE;
			options.c_cflag |= CS8;
			options.c_cflag &= ~CRTSCTS;// Disable hardware flow control

			options.c_cc[VMIN]  = 1;
			options.c_cc[VTIME] = 2;

			tcsetattr(port_fd, TCSANOW, &options);//Set the new options
		}
		return 0;
	}
	
	cout << "Serial (" << mystn << "): invalid rate\n";
	return -1;
}

int SerialClass::open(int stn, string device, int port) {

	mystn = stn;
	real_port = 1;
	ip_raw = 0;
	
	if (!device.compare("IP"))//with handshaking
		real_port = 0;
	else if (!device.compare("IPRAW")) {//without handshaking
		real_port = 0;
		ip_raw = 1;
	}
		
	cout << "Serial (" << mystn << "): Open device=" << device << "  real=" << real_port << "  raw=" << ip_raw << " ip_port=" << port << endl;

	if (real_port) {
		//printf("open real serial port!\n");
		port_fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
		if (port_fd == -1)//Could not open the port.			
			die("Serial: Unable to open port");
		else {
			fcntl(port_fd, F_SETFL, 0);
			fcntl(port_fd, F_SETFL, O_NONBLOCK);//was FNDELAY//No blocking when reading
			
			reset();
		}
	} else {
		//create a TCP socket
		if ((l_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			die("Serial: socket");

		//zero out the structure
		memset((char *)&si_me, 0, sizeof(si_me));

		si_me.sin_family = AF_INET;
		si_me.sin_port = htons(port);
		si_me.sin_addr.s_addr = INADDR_ANY;

		//bind socket to port
		if (bind(l_sock, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
			die("Serial: bind");

		//make socket non-blocking
		int flags = fcntl(l_sock, F_GETFL, 0);
		if (flags == -1)
			die("Serial: get flags");

		if (fcntl(l_sock, F_SETFL, flags | O_NONBLOCK) == -1)
			die("Serial: set flags");

		//listen for incoming connection
		cout << "Serial (" << mystn << "): listening...\n";

		if (listen(l_sock, BACKLOG) == -1)
			die("Serial: listen");
	}

	baud(SERIAL_BAUD);
	return 0;
}

void SerialClass::close(void) {
	if (real_port)
		::close(port_fd);
	else {
		::close(c_sock);
		::close(l_sock);
	}
}

void SerialClass::poll(void) {
	uint8_t c;
	
	if (real_port) {
		int n = read(port_fd, rxbuf, SRXBUF_SIZE);
		for (int i = 0; i < n; i++) {
			c = rxbuf[i];
			//printf("<<%02x", c);
			input.put(&c); 
			bytes_in++;			
		}
		
		trigger_set();
		if (trigger2) {
			if (output.count > 0) {
				output.get(&c);
				write(port_fd, &c, 1);
				//printf(">>%02x", c);
				bytes_out++;
			}

			trigger_reset();
		}
	} else {
		if (!connected_flag) {
			c_sock = accept(l_sock, (struct sockaddr *)&si_other, &slen);

			if (c_sock == -1) {
				if (errno != EAGAIN && errno != EWOULDBLOCK)
					die("Serial: accept()");
			} else {
				fcntl(c_sock, F_SETFL, O_NONBLOCK);
				connected_flag = 1;
				cout << "Serial (" << mystn << "): CONNECTED\n";

				reset();
			}
		} else {
			int n = recv(c_sock, rxbuf, SRXBUF_SIZE, 0);

			if (n < 0) { 
				if (errno != EWOULDBLOCK)
					die("Serial: recvfrom()");
			} else if (n == 0) {
				connected_flag = 0;
				::close(c_sock);
				c_sock = 0;
				cout << "Serial (" << mystn << "): DISCONNECTED\n";
			} else {
				for (int i = 0; i < n; i++) {
					c = rxbuf[i];
					//printf(">%02x", c);
					if (flag_byte) {
						flag_byte = 2;

						if (c == 0) {
							not_clear_to_send = 0;
							//printf("[nCTS=0]\n");
						} else if (c == 1) {
							not_clear_to_send = 1;
							//printf("[nCTS=1]\n");
						} else
							flag_byte = 0;
						
						if (flag_byte)
							send_nrts();
					}
					else if (!ip_raw && c == 0xff)
						flag_byte = 1;

					if (!flag_byte) {
						//printf("*%02x", c);
						input.put(&c); 
						bytes_in++;
					} else if (flag_byte == 2)
						flag_byte = 0;
				}
			}
		}

		if (connected_flag && (!not_clear_to_send || ip_raw)) {
			trigger_set();
			
			if (trigger2) {
				if (output.count > 0 && txcount < STXBUF_SIZE) {
					//move byte to txbuf
					output.get(&c);
					bytes_out++;

					//printf("+%02x", c);
					txbuf[txcount++] = c;
					if (!ip_raw && c == 0xff)
						txbuf[txcount++] = 0xff;
				}
			}

			//Don't send a datagram for each byte...
			if (trigger1) {
				if (txcount > 0) {
					//printf("SERIAL SEND: ");
					//for (int i = 0; i < txcount; i++)
					//	printf("%02x ", txbuf[i]);
					//printf("\n");
					
					
					if (send(c_sock, txbuf, txcount, 0) == -1)
						die("Serial: send");

					txcount = 0;
				}
			}
			
			trigger_reset();
		}
	}
}

