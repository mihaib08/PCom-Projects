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

#define BUFLEN 1552

#define MAX_CLIENTS	12

#endif
