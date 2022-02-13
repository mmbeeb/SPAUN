/* SPAUN                  */
/* main.c                 */
/* (c) 2022 Martin Mather */

#define _POSIX_C_SOURCE 1

#include <ctype.h>
#include <stdio.h> 
#include <stdint.h>
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/socket.h> //cygwin

#include "serial.h"
#include "sp.h"
#include "aun.h"

void set_no_buffer() {
	struct termios term;
	tcgetattr(0, &term);
	term.c_lflag &= ~ICANON;
	tcsetattr(0, TCSANOW, &term);
}

int charsWaiting(int fd) {
	int count;
	
	if (ioctl(fd, FIONREAD, &count) == -1)
		exit (EXIT_FAILURE);
	
	return count;
}

static void die(char *s) {
	perror(s);
	exit(1);
}

int main(int argc, char** argv[]) {
	clock_t timeout1 = time(0);
	char *device = "/dev/ttyS0";
	
	if (argc == 2) {
		device = (char *)argv[1];
	}

	serial_open(device, 8);
	//serial_open("*", 8);
	int bytes_in = 0, bytes_out = 0, lineout=0;
	
	aun_open(104);
	aun_receiver_enable(1);

	int q = 0, c, mode = 0, connected = 0;
	set_no_buffer();
	while(!q) {

		if (time(0) > timeout1) {//second timer
			//if (s_bytes_in || s_bytes_out)
				//printf("loop timeout : bytes rx = %d  tx = %d\n", s_bytes_in, s_bytes_out);

			s_bytes_in = 0;
			s_bytes_out = 0;
			timeout1 = time(0);
		}

		sp_poll();
		aun_poll();

		c = 0;
		int count = charsWaiting(fileno(stdin));
		if (count != 0) {
			c = tolower(getchar());
			
			if (mode) {
				switch (mode) {
					case 'r'://Baud rate
						if (c >= '1' && c <= '8') {
							serial_rate((int) c - '0');
							c = 0;
						}
						break;
				}
				mode = 0;
			}
			
			switch (c) {
				case 'q':
					if (serial_connected())
						printf("STILL CONNECTED!\n");//Close connection using BeebEm first.
					else {
						printf("Quit\n");
						q = 1;
					}
					break;
				case 'r':
					printf("Select baud rate (1 to 8)\n");
					mode = 'r';
					break;
				case 'u':
				case 'o':
					sp_optimistic = (c == 'o');
					printf("SP  : OPTIMISTIC = %d\n", sp_optimistic);
					break;
				case 'z':
					printf("\n");
					break;
				case 'e':
					serial_set_nrts(0);
					break;
				case 'd':
					serial_set_nrts(1);
					break;
				case 'y':
					sp_reset();
					break;
			}
		}
	}

	aun_close();
	serial_close();
}
 
