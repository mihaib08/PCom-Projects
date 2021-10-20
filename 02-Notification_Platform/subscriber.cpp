#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>

#include "helpers.h"

// max size of a message received from the server
#define MAXTCP (sizeof(tcp_msg) + 1)

/**
 * encode the given command - *buf - 
 * to send to the server - *m -
 */
bool encode_sub_msg(char *buf, sub_msg *m) {
    char sep[] = " ";

    // (un)subscribe
    char *p = strtok(buf, sep);
    if (strcmp(p, "subscribe") == 0) {
        m->type = 1;
    } else if (strcmp(p, "unsubscribe") == 0) {
        m->type = 0;
    } else {
        fprintf(stderr, "[Command] Invalid command");
        return false;
    }

    // topic
    p = strtok(nullptr, sep);
    strcpy(m->topic, p);

    if (m->type == 1) {
        // subscribe command -> get sf
        p = strtok(nullptr, sep);

        DIE(p == nullptr, "[Subscribe] Need to add the <sf> specifier");

        m->sf = p[0] - '0';
    }

    return true;
}

void usage(char *file) {
	fprintf(stderr, "Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", file);
	exit(0);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int ret;
    tcp_msg *t_msg;
    sub_msg m;
    char buf[MAXTCP];

    if (argc != 4) {
        usage(argv[0]);
    }

    int len_id = strlen(argv[1]);
    DIE(len_id > 10, "[ID] > 10 chars");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "[Socket] Failed");

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	ret = inet_aton(argv[2], &serv_addr.sin_addr);
    DIE(ret < 0, "[Server] Address conversion failed");

    ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
    DIE(ret < 0, "[Server] Connection failed");

    /**
     * send <ID_CLIENT> to server
     */
    int s = send(sockfd, argv[1], len_id + 1, 0);
    DIE(s < 0, "[ID] Send failed");

    int f = 1;
    int na = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&f, sizeof(int));
    DIE(na < 0, "[Neagle] Deactivation failed");

    fd_set read_fds;
    fd_set tmp_fds;

    FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

    FD_SET(STDIN_FILENO, &read_fds);
	FD_SET(sockfd, &read_fds);

    while (1) {
        tmp_fds = read_fds;

        ret = select(sockfd + 1, &tmp_fds, nullptr, nullptr, nullptr);
        DIE(ret < 0, "Select file_descriptor error");

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            // parse the command from stdin
            memset(buf, 0, MAXTCP);
            fgets(buf, BUFLEN - 1, stdin);

			if (strncmp(buf, "exit", 4) == 0) {
				break;
			}

            int len = strlen(buf);
            // ignore the '\n'
            buf[len - 1] = 0;

            bool ok = encode_sub_msg(buf, &m);

            if (!ok) {
                continue;
            }

            s = send(sockfd, &m, sizeof(m), 0);
            DIE(s < 0, "[Command] Send failed");

            // print the feedback message
            if (m.type == 1) {
                printf("Subscribed to topic.\n");
            } else {
                printf("Unsubscribed from topic.\n");
            }
        }
        
        if (FD_ISSET(sockfd, &tmp_fds)) {
            memset(buf, 0, MAXTCP);
            ret = recv(sockfd, buf, sizeof(tcp_msg), 0);

            // no message 
            //    --> the server is closed
            if (ret == 0) {
                break;
            }
            
            // parse the message in tcp_msg format
            t_msg = (tcp_msg *)buf;

            printf("%s:%hu - %s - %s - %s\n", t_msg->udp_ip, t_msg->prt, t_msg->topic, t_msg->d_type, t_msg->payload);
        }
    }

    close(sockfd);

    return 0;
}
