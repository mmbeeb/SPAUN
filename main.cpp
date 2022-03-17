/* SPAUN                  */
/* main.cpp               */
/* (c) 2022 Martin Mather */


#include <iostream>
#include <string>
#include <fstream>

#include <iterator>

using namespace std;

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

#include "sp.h"
#include "aun.h"
#include "network.h"

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

static int net_config(void) {
	//Configure network and stations
	
	ifstream cfg;
	cfg.open("spaun.cfg");
	if (cfg.is_open()) {
		cout << "Config file found\n";

		string s1, s2, s3, s4;	
		while (cfg >> s1) {
			if (s1.length() == 2) {
				if (s1[0] == '*') {
					switch (s1[1]) {
						case 'R'://Remote AUN
							cfg >> s2 >> s3;
							cout << "AUN Remote: STN=" << s2 << " IP_ADDR=" << s3 << endl;
							mynetwork.add(atoi(s2.c_str()), s3);
							break;
							
						case 'L'://Local AUN
							cfg >> s2;
							cout << "AUN Local : STN=" << s2 << endl;
							mynetwork.add(atoi(s2.c_str()), "127.0.0.1");
							break;						
						
						case 'S'://SP serial or serial over ip
							cfg >> s2 >> s3;
							cout << "SP        : STN=" << s2 << " DEVICE=" << s3;
							if (!s3.compare("IP") || !s3.compare("IPRAW")) {//Get serial over ip port number
								cfg >> s4;
								cout << " SERIAL_IP_PORT=" << s4;
							}
							cout << endl;

							if (!mynetwork.add(atoi(s2.c_str()), "127.0.0.1"));//All SP stations are local
								mysp.open(atoi(s2.c_str()), s3, atoi(s4.c_str()));
							break;
					}
				}
			}
		}
		
		cfg.close();
		return 0;
	}
	
	
	cout << "Config file not found!\n";
	return -1;	
}

int main(void) {
	// Main
	
	if (!net_config()) {
		int q = 0, c;
		set_no_buffer();
		
		while(!q) {
			mysp.poll();

			c = 0;
			int count = charsWaiting(fileno(stdin));
			if (count != 0) {
				c = tolower(getchar());	
		
				switch (c) {
					case 'q':
						if (mysp.connected())
							printf("STILL CONNECTED!\n");//Close connection using BeebEm first.
						else {
							printf("Quit\n");
							q = 1;
						}
						break;
				
					/*case 'u':
					case 'o':
						mysp.optimistic = (c == 'o');
						cout << "OPTIMISTIC = " << mysp.optimistic;
						break;*/
						
					case 'm':
					case 'n':
						mysp.monitor(c == 'm');
						break;
						
					case 'z':
						mysp.test();
						break;
						
					case 'y':
						mysp.reset();
						break;
				}
			}
		}

		mysp.close();
	}
}
 
