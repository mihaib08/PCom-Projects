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

#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>

#include "helpers.h"

using namespace std;

void usage(char *file) {
    fprintf(stderr, "Usage : %s <PORT_Server>\n", file);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in serv_addr;

    struct sockaddr_in cli_addr;
    socklen_t cli_len;
    int sock_cli;

    char buf[BUFLEN];

    if (argc < 2) {
		usage(argv[0]);
	}

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "[TCP] Socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    int b = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(sockaddr_in));
    DIE(b < 0, "[TCP] Bind failed");

    int f;
    int na;
    f = 1;
    na = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&f, sizeof(int));
    DIE(na < 0, "[Neagle] Deactivation failed");

    int l = listen(sockfd, MAX_CLIENTS);
    DIE(l < 0, "[TCP] Listen error");

    fd_set read_fds, tmp_fds;

    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(sockfd, &read_fds);

    int fdmax = sockfd;

    // no. clients
    int ct = 0;

    unordered_map<string, int> user_sock;
    unordered_map<string, int> user_id;
    unordered_map<int, string> sock_user;
    
    vector<string> id_user;
    id_user.push_back("0"); // don't use the 0-th entry

    char sep[] = " ";

    int i, ret;
    while (1) {
        tmp_fds = read_fds;

        ret = select(fdmax + 1, &tmp_fds, nullptr, nullptr, nullptr);
        DIE(ret < 0, "File_descriptor select error");

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            memset(buf, 0, BUFLEN);

            // received a command from stdin
            // check for <exit>
            fgets(buf, BUFLEN - 1, stdin);

			if (strncmp(buf, "exit", 4) == 0) {
				break;
			}

            // not <exit>
            fprintf(stderr, "Only <exit> command is accepted\n");
        }

        for (i = 2; i <= fdmax; ++i) {
            if (FD_ISSET(i, &tmp_fds)) {
                memset(buf, 0, BUFLEN);

                if (i == sockfd) {
                    cli_len = sizeof(cli_addr);
                    sock_cli = accept(sockfd, (struct sockaddr *) &cli_addr, &cli_len);
                    DIE(sock_cli < 0, "[Client] Connection to server failed");

                    f = 1;
                    na = setsockopt(sock_cli, IPPROTO_TCP, TCP_NODELAY, (char *)&f, sizeof(int));
                    DIE(na < 0, "[Neagle] Deactivation failed");

                    FD_SET(sock_cli, &read_fds);
                    if (sock_cli > fdmax) {
                        fdmax = sock_cli;
                    }

                    // wait for nickname
                    ret = recv(sock_cli, buf, BUFLEN - 1, 0);
                    DIE(ret < 0, "[Nickname] No nickname received");

                    if (user_sock.find(buf) != user_sock.end()) {
                        user_sock[buf] = sock_cli;
                        sock_user[sock_cli] = buf;
                        printf("S-a reconectat clientul %s, pe socket-ul %d\n", buf, sock_cli);
                    } else {
                        user_sock[buf] = sock_cli;
                        sock_user[sock_cli] = buf;

                        ct++;
                        user_id[buf] = ct;
                        id_user.push_back(buf);

                        printf("S-a conectat un nou client, pe socket-ul %d\n", sock_cli);
                    }
                    
                    // printf("%s\n", buf);
                    // printf("---------------------\n");
                } else {
                    ret = recv(i, buf, BUFLEN - 1, 0);
                    DIE(ret < 0, "[Client] Receive failed");

                    // check for exit
                    if (ret == 0) {
                        printf("Clientul de pe socket-ul %d s-a deconectat.\n", i);

                        string name = sock_user[i];
                        user_sock[name] = -1; // make the entry "inactive"

                        FD_CLR(i, &read_fds);
                        close(i);
                    } else {
                        // printf("%d - %s\n", i, buf);
                        if (strcmp(buf, "LIST") == 0) {
                            string ans;

                            ans = "";

                            for (int l = 1; l < id_user.size(); ++l) {
                                string name = id_user[l];

                                if (user_sock[name] != -1) {
                                    char aux[BUFLEN];

                                    sprintf(aux, "%d - %s\n", l, name.c_str());
                                    ans += aux;
                                }
                            }

                            ret = send(i, ans.c_str(), ans.size(), 0);
                        } else {
                            char rec[BUFLEN];
                            strcpy(rec, buf);

                            char *p = strtok(buf, sep);

                            if (strcmp(p, "MSG") == 0) {
                                p = strtok(nullptr, sep);

                                int id;
                                id = atoi(p);

                                string to_name = id_user[id];
                                int curr_sock = user_sock[to_name];

                                if (curr_sock != -1) {
                                    p = strtok(nullptr, sep);

                                    char ans[BUFLEN];
                                    sprintf(ans, "%s: ", sock_user[i].c_str());

                                    strcat(ans, rec + (p - buf));
                                    strcat(ans, "\n");

                                    // printf("%d - %s\n", curr_sock, ans);
                                    ret = send(curr_sock, ans, strlen(ans), 0);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // close the other sockets
    for (i = 2; i <= fdmax; ++i) {
        if(FD_ISSET(i, &read_fds)) {
            close(i);
        }
    }
    close(sockfd);

    return 0;
}
