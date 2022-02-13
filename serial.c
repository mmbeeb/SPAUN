/* SPAUN                  */
/* serial.c               */
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
//#include <malloc.h>
#include <time.h>
#include <termios.h>


#include "serial.h"

static int real_port, port_fd;

#define PORT	25232	//BeebEm IP232 default
#define BACKLOG	1	//how many pending connections queue will hold

static struct sockaddr_in si_me, si_other;
static int l_sock = 0, c_sock = 0, slen = sizeof(si_other), connected = 0;

#define SBUF_SIZE	2048

#define TXBUF_SIZE	200
#define RXBUF_SIZE	200

static uint8_t txbuf[TXBUF_SIZE + 1], rxbuf[RXBUF_SIZE];
static int txcount = 0, not_clear_to_send = 1, not_request_to_send = 1, flag_byte = 0;
static int clock_divide = 0, trigger1 = 0, trigger2 = 0, baud_rate;

int s_bytes_in = 0, s_bytes_out = 0;

static struct sbuf_t {
	uint8_t buf[SBUF_SIZE];
	int free, count, tail, head, overflow;
} sbufs[2], *sbuf_output = &sbufs[0], *sbuf_input = &sbufs[1];

static void die(char *s) {
	perror(s);
	exit(1);
}

int serial_connected(void) {
	return connected;
}

static void _trigger_reset(void) {
	trigger1 = 0;
	trigger2 = 0;
}

static void _trigger_set(void) {
	static unsigned int t1 = 0, t2 = 0;
	unsigned int n1, n2;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	n1 = ts.tv_nsec / 10000000;		//centiseconds
	if (real_port)
		n2 = (ts.tv_nsec / (100000000/140));//figure (where it works) found by trial & error with real beeb
	else
		n2 = (ts.tv_nsec / (100000000/192)) >> clock_divide;
	trigger1 = trigger1 || (n1 != t1);
	trigger2 = trigger2 || (n2 != t2);
	t1 = n1;
	t2 = n2;
}

static void _send_nrts_state(void) {
	if (connected) {
		uint8_t buf[2];
		buf[0] = 0xff;
		buf[1] = not_request_to_send;
		if (send(c_sock, &buf, 2, 0) == -1)
			die("Serial: send rts");
		printf("[nRTS=%d]\n", buf[1]);
	}
}

void serial_set_nrts(int nrts) {
	not_request_to_send = nrts;
	_send_nrts_state();
}

static void _sbuf_print(struct sbuf_t *p) {
	printf("free=%d count=%d tail=%d head=%d overflow=%d\n", p->free, p->count, p->tail, p->head, p->overflow);
}

static void _sbuf_flush(struct sbuf_t *p) {
	memset(p, 0, sizeof(struct sbuf_t));
	p->free = SBUF_SIZE;
}

static void _sbuf_reset(void) {
	_sbuf_flush(sbuf_input);//input buffer
	_sbuf_flush(sbuf_output);//output buffer
	not_request_to_send = 0;
	_send_nrts_state();
	txcount = 0;
}

static int _sbuf_put(struct sbuf_t *p, uint8_t *c) {
	if (p->free == 0) {
		printf("Serial: overflow\n");
		p->overflow = 1;
		return -1;
	}

	p->buf[p->head++] = *c;
	p->head %= SBUF_SIZE;
	p->count++;
	p->free--;
	return 0;
}

static int _sbuf_get(struct sbuf_t *p, uint8_t *c) {
	if (p->count == 0)
		return -1;

	*c = p->buf[p->tail++];
	p->tail %= SBUF_SIZE;
	p->count--;
	p->free++;
	p->overflow = 0;
	return 0;
}

int serial_put_free(void) {
	return sbuf_output->free;
}

int serial_put_empty(void) {
	return sbuf_output->free == SBUF_SIZE;
}

void serial_flush(void) {
	_sbuf_flush(sbuf_input);//input buffer
	_sbuf_flush(sbuf_output);//output buffer
}

int serial_put(uint8_t c) {
	return	_sbuf_put(sbuf_output, &c);
}

int serial_put2(uint8_t *c, int len) {
	if (len > sbuf_output->free)
		return -1;

	for (int i = 0; i < len; i++)
		_sbuf_put(sbuf_output, c++);

	return 0;
}

void sgg(void) {
	_sbuf_print(sbuf_input);
}

int serial_get_count(void) {
	return sbuf_input->count;
}

int serial_get(uint8_t *c) {
	return _sbuf_get(sbuf_input, c);
}

int serial_get2(uint8_t *c, int len) {
	if (len > sbuf_input->count)
		return -1;

	for (int i = 0; i < len; i++)
		_sbuf_get(sbuf_input, c++);

	return 0;
}

int serial_rate(int b_rate) {
	const int bauds[] = {75, 150, 300, 1200, 2400, 4800, 9600, 19200};

	if (b_rate > 0 && b_rate <= 8) {
		baud_rate = bauds[b_rate - 1];
		clock_divide = (8 - b_rate);
		if (b_rate < 4)
				clock_divide++;
		printf("Serial: rate = %d , approx bytes/sec = %d , clk_div = %d\n", baud_rate, baud_rate / 10, clock_divide);
		_trigger_reset();

		if (real_port) {
			struct termios options;
			tcgetattr(port_fd, &options);//Get the current options
		
			//Set the baud rates to 19200...
			
			cfsetispeed(&options, B19200);
			cfsetospeed(&options, B19200);
		
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
	} else
		printf("Serial: invalid rate\n");
}

int serial_open(char *device, int b_rate) {
	
	real_port = strcmp(device, "*");
	printf("Serial: open '%s', real=%d\n", device, real_port);

	if (real_port) {
		printf("open real serial port!\n");
		port_fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
		if (port_fd == -1)//Could not open the port.			
			die("Serial: Unable to open port");
		else {
			fcntl(port_fd, F_SETFL, 0);
			fcntl(port_fd, F_SETFL, O_NONBLOCK);//was FNDELAY//No blocking when reading
			_sbuf_reset();
			_trigger_reset();
		}
	} else {
		//create a TCP socket
			if ((l_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
				die("Serial: socket");

		//zero out the structure
		memset((char *)&si_me, 0, sizeof(si_me));

		si_me.sin_family = AF_INET;
		si_me.sin_port = htons(PORT);
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
		printf("Serial: listening...\n");
			if (listen(l_sock, BACKLOG) == -1)
				die("Serial: listen");
	}

	serial_rate(b_rate);
}

int serial_close(void) {
	if (real_port)
		close(port_fd);
	else {
		close(c_sock);
		close(l_sock);
	}
}

int serial_poll(void) {
	if (real_port) {
		int n = read(port_fd, rxbuf, RXBUF_SIZE);
		for (int i = 0; i < n; i++) {
			uint8_t c = rxbuf[i];
			//printf("<<%02x", c);
			_sbuf_put(sbuf_input, &c); 
			s_bytes_in++;			
		}
		
		_trigger_set();
		if (trigger2) {
			uint8_t c;
			if (sbuf_output->count > 0) {
				_sbuf_get(sbuf_output, &c);
				write(port_fd, &c, 1);
				//printf(">>%02x", c);
				s_bytes_out++;
			}

			_trigger_reset();
		}
	} else {
		if (!connected) {
			c_sock = accept(l_sock, (struct sockaddr *)&si_other, &slen);

			if (c_sock == -1) {
				if (errno != EAGAIN && errno != EWOULDBLOCK)
					die("Serial: accept()");
			} else {
				fcntl(c_sock, F_SETFL, O_NONBLOCK);
				connected = 1;
				printf("Serial: CONNECTED\n");

				_sbuf_reset();
				_trigger_reset();
			}
		} else {
			int n = recv(c_sock, rxbuf, RXBUF_SIZE, 0);

			if (n < 0) { 
				if (errno != EWOULDBLOCK)
					die("Serial: recvfrom()");
			} else if (n == 0) {
				connected = 0;
				close(c_sock);
				c_sock = 0;
				printf("Serial: DISCONNECTED\n");
			} else {
				for (int i = 0; i < n; i++) {
					uint8_t c = rxbuf[i];
					//printf(">%02x", c);
					if (flag_byte) {
						flag_byte = 2;

						if (c == 0) {
							not_clear_to_send = 0;
							printf("[nCTS=0]\n");
						} else if (c == 1) {
							not_clear_to_send = 1;
							printf("[nCTS=1]\n");
						} else
							flag_byte = 0;
						
						if (flag_byte)
							_send_nrts_state();
					}
					else if (c == 0xff)
						flag_byte = 1;

					if (!flag_byte) {
						//printf("*%02x", c);
						_sbuf_put(sbuf_input, &c); 
						s_bytes_in++;
					} else if (flag_byte == 2)
						flag_byte = 0;
				}

			}
		}
	
		if (connected && !not_clear_to_send) {
			_trigger_set();
			
			if (trigger2) {
				if (sbuf_output->count > 0 && txcount < TXBUF_SIZE) {
					//move byte to txbuf
					_sbuf_get(sbuf_output, &txbuf[txcount]);
					if (txbuf[txcount++] == 0xff)
						txbuf[txcount++] = 0xff;
				}
			}

			if (trigger1) {
				if (txcount > 0) {
					//printf("SERIAL SEND: ");
					//for (int i = 0; i < txcount; i++)
					//	printf("%02x ", txbuf[i]);
					//printf("\n");
					
					
					if (send(c_sock, txbuf, txcount, 0) == -1)
						die("Serial: send");

					s_bytes_out += txcount;
					txcount = 0;
				}
			}
			
			_trigger_reset();
		}
	}
}

