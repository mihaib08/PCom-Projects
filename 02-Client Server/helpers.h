#ifndef _HELPERS_H
#define _HELPERS_H 1

#include <stdio.h>
#include <stdlib.h>

/*
* Error checking macro
* Usage: 
* 		int fd = open (file_name , O_RDONLY);	
* 		DIE(fd == -1, "open failed");
*/

#define DIE(assertion, call_description)				\
	do {								\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",			\
					__FILE__, __LINE__);		\
			perror(call_description);			\
			exit(EXIT_FAILURE);				\
		}							\
	} while(0)

/**
 * Message from a TCP subscriber
 */
struct __attribute__((packed)) sub_msg {
	uint8_t type;
	char topic[51];
	uint8_t sf;
};

/**
 * Messages sent from the UDP Clients
 * 							-> TCP Subscribers
 */
struct __attribute__((packed)) tcp_msg {
	char udp_ip[16];
	uint16_t prt;
	char topic[51];
	char d_type[11];
	char payload[1501];
};

/**
 * Format of an UDP message received by the server
 */
struct __attribute__((packed)) udp_msg {
	char topic[50];
	uint8_t d_type;
	char payload[1501]; // (+1) for '\0'
};

#define BUFLEN 1552

#define MAX_CLIENTS	12

#endif
