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

void usage(char *file) {
    fprintf(stderr, "Usage : %s <IP_Server> <PORT_Server> nickname\n", file);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in serv_addr;

    char buf[BUFLEN];

    if (argc < 4) {
        usage(argv[0]);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    int ret = inet_aton(argv[1], &serv_addr.sin_addr);
    DIE(ret < 0, "[Server] Address conversion failed");

    ret = connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    DIE(ret < 0, "[Server] Connection failed");

    // send the nickname to server
    int len_nick = strlen(argv[3]);
    int s = send(sockfd, argv[3], len_nick + 1, 0);
    DIE(s < 0, "[Nickname] Send failed");

    int f = 1;
    int na = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&f, sizeof(int));
    DIE(na < 0, "[Neagle] Deactivation failed");

    /**
     * I/O Multiplexing Utils
     */
    fd_set read_fds, tmp_fds;

    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(sockfd, &read_fds);

    int fdmax = sockfd;

    while (1) {
        tmp_fds = read_fds;

        ret = select(fdmax + 1, &tmp_fds, nullptr, nullptr, nullptr);
        DIE(ret < 0, "File_descriptor select error");

        if (FD_ISSET(sockfd, &tmp_fds)) {
            memset(buf, 0, BUFLEN);

            ret = recv(sockfd, buf, BUFLEN, 0);
            DIE(ret < 0, "receive");

            // no message 
            //    --> the server is closed
            if (ret == 0) {
                break;
            }

            printf("%s", buf);
        }

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            memset(buf, 0, BUFLEN);

            fgets(buf, BUFLEN - 1, stdin);

			if (strncmp(buf, "exit", 4) == 0) {
				break;
			}

            int len = strlen(buf);
            // ignore the '\n'
            buf[len - 1] = 0;

			// se trimite mesaj la server
			int n = send(sockfd, buf, strlen(buf), 0);
			DIE(n < 0, "send");
        }
    }

    close(sockfd);

    return 0;
}
