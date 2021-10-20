#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "helpers.h"

#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>

using namespace std;

/**
 * Decode the message received from an UDP client
 * depending on the data type - d_type - 
 */
void decode_udp(udp_msg *u_msg, tcp_msg *t_msg) {
    uint8_t t = u_msg->d_type;

    memcpy(t_msg->topic, u_msg->topic, 50);

    if (t == 0) {
        // INT
        long long num = ntohl(*(uint32_t *)(u_msg->payload + 1));

        if (u_msg->payload[0]) {
            num *= (-1);
        }

        sprintf(t_msg->d_type, "INT");
        sprintf(t_msg->payload, "%lld", num);
    } else if (t == 1) {
        // SHORT_REAL
        uint16_t sr = ntohs(*(uint16_t *)u_msg->payload);

        double re = (double) sr / 100;

        sprintf(t_msg->d_type, "SHORT_REAL");
        sprintf(t_msg->payload, "%.2lf", re);
    } else if (t == 2) {
        // FLOAT
        int p = *(uint8_t *)(u_msg->payload + sizeof(uint32_t) + 1); // 10^(-p)

        int32_t fl = ntohl(*(uint32_t *)(u_msg->payload + 1));
        
        if (u_msg->payload[0]) {
            fl *= -1;
        }

        double res = (double) fl;
        while (p) {
            res /= 10;
            p--;
        }

        sprintf(t_msg->d_type, "FLOAT");
        sprintf(t_msg->payload, "%lf", res);
    } else if (t == 3) {
        // STRING
        sprintf(t_msg->d_type, "STRING");
        memcpy(t_msg->payload, u_msg->payload, sizeof(u_msg->payload));
    }
}

/**
 *  Update the topics of the messages to be stored
 *  after a subscriber disconnects
 */
void update_store(string id, unordered_map<string, unordered_set<string>>& saved_topics, 
                             unordered_map<string, unordered_set<string>>& saved_ids) {
    for (auto& topic : saved_ids[id]) {
        saved_topics[topic].insert(id);
    }
}

/**
 * Send the stored messages to the current re-connected client <id>
 */
void send_stored(int sockfd, char *id, unordered_map<string, unordered_set<string>>& saved_topics,
                            vector<pair<int, tcp_msg>>& saved_msgs) {
    for (auto it = saved_msgs.begin(); it != saved_msgs.end();) {
        auto t_msg = (*it).second;

        if (saved_topics[t_msg.topic].find(id) != saved_topics[t_msg.topic].end()) {
            send(sockfd, &t_msg, sizeof(tcp_msg), 0);
            (*it).first--;
        }

        if ((*it).first == 0) {
            // erase the message if it's no longer needed
            it = saved_msgs.erase(it);
        } else {
            it++;
        }
    }
}

/**
 *  Delete the client <id> from the store&forward logic
 */
void delete_from_stored(char *id, unordered_map<string, unordered_set<string>>& saved_topics,
                                  unordered_map<string, unordered_set<string>>& saved_ids) {
    for (auto& topic : saved_ids[id]) {
        // remove <id> from saved_topics
        //    -- the current stored messages need no longer
        //       to be sent to client <id>
        saved_topics[topic].erase(id);

        if (saved_topics[topic].size() == 0) {
            // the messages on <topic>
            // don't need to be stored anymore
            saved_topics.erase(topic);
        }
    }
}

void usage(char *file) {
	fprintf(stderr, "Usage: %s <PORT_SERVER>\n", file);
	exit(0);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    
    // check the no. parameters
    if (argc != 2) {
		usage(argv[0]);
	}

    int ret;
    int i;

    // Config server addresses
    struct sockaddr_in serv_addr_udp, serv_addr_tcp;
    int portno;
    socklen_t socklen;

    portno = atoi(argv[1]);

    // UDP Socket
    int sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(sock_udp < 0, "[UDP] Socket");

	serv_addr_udp.sin_family = AF_INET;
	serv_addr_udp.sin_port = htons(portno);
	serv_addr_udp.sin_addr.s_addr = INADDR_ANY;

    int b = bind(sock_udp, (struct sockaddr *) &serv_addr_udp, sizeof(sockaddr_in));
    DIE(b < 0, "[UDP] Bind failed");

    // TCP Socket
    int sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sock_tcp < 0, "[TCP] Socket");

	serv_addr_tcp.sin_family = AF_INET;
	serv_addr_tcp.sin_port = htons(portno);
	serv_addr_tcp.sin_addr.s_addr = INADDR_ANY;

    b = bind(sock_tcp, (struct sockaddr *) &serv_addr_tcp, sizeof(sockaddr_in));
    DIE(b < 0, "[UDP] Bind failed");

    int f;
    int na;
    f = 1;
    na = setsockopt(sock_tcp, IPPROTO_TCP, TCP_NODELAY, (char *)&f, sizeof(int));
    DIE(na < 0, "[Neagle] Deactivation failed");

    int l;
    l = listen(sock_tcp, MAX_CLIENTS);
    DIE(l < 0, "[TCP] Listen error");

    /**
     * I/O Multiplexing utils
     */
    fd_set read_fds;
	fd_set tmp_fds;
	int fdmax;

    FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

    // add the initial sockets
    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(sock_udp, &read_fds);
    FD_SET(sock_tcp, &read_fds);

    fdmax = max(sock_udp, sock_tcp);

    char buf[BUFLEN];

    struct sockaddr_in cli_addr;
    socklen_t cli_len;
    int sock_cli;

    udp_msg *u_msg;
    tcp_msg t_msg;
    sub_msg *s_msg;

    /**
     * <ID_CLIENT, sock_cli>
     *   - client_id disconnected
     *           -> sock_cli[client_id] = -1
     */
    unordered_map<string, int> client_sockfds;

    /**
     * <sock_cli, ID_CLIENT>
     */
    unordered_map<int, string> client_ids;

    /**
     * <topic, {subs}>
     *    - {subs} -> subscribers that are disconnected
     *                and have sf = 1 for <topic>
     */
    unordered_map<string, unordered_set<string>> saved_topics;

    /**
     * <id, {topic(s)}>
     *      -> topics with sf = 1 for client <id>
     */
    unordered_map<string, unordered_set<string>> saved_ids;

    /**
     * <no_subs, TCP_msg>
     *    - messages to be sent after clients reconnect
     *    - no_subs - no. subscribers to sent the message <TCP_msg> to
     */
    vector<pair<int, tcp_msg>> saved_msgs;

    /**
     * <ID, {topic(s)}>
     */
    unordered_map<string, unordered_set<string>> client_topics;

    while (1) {
        tmp_fds = read_fds;

        ret = select(fdmax + 1, &tmp_fds, nullptr, nullptr, nullptr);
        DIE(ret < 0, "Select file_descriptor error");

        memset(buf, 0, BUFLEN);

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
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
                if (i == sock_tcp) {
                    // connection request from a new client
                    cli_len = sizeof(cli_addr);
                    sock_cli = accept(sock_tcp, (struct sockaddr *)&cli_addr, &cli_len);
                    DIE(sock_cli < 0, "[Client] Connection to server failed");

                    f = 1;
                    na = setsockopt(sock_cli, IPPROTO_TCP, TCP_NODELAY, (char *)&f, sizeof(int));
                    DIE(na < 0, "[Neagle] Deactivation failed");

                    FD_SET(sock_cli, &read_fds);
                    if (sock_cli > fdmax) {
                        fdmax = sock_cli;
                    }

                    // wait for ID_CLIENT
                    ret = recv(sock_cli, buf, BUFLEN - 1, 0);
                    DIE(ret < 0, "[ID_CLIENT] No ID received");

                    if (client_sockfds.find(buf) == client_sockfds.end()) {
                        // add the new client
                        client_sockfds[buf] = sock_cli;
                        client_ids[sock_cli] = buf;

                        printf("New client %s connected from %s:%hu.\n", buf, inet_ntoa(cli_addr.sin_addr),
                                                                              ntohs(cli_addr.sin_port));
                    } else {
                        if (client_sockfds[buf] == -1) {
                            // client has re-connected
                            client_sockfds[buf] = sock_cli;
                            client_ids[sock_cli] = buf;

                            printf("New client %s connected from %s:%hu.\n", buf, inet_ntoa(cli_addr.sin_addr),
                                                                             ntohs(cli_addr.sin_port));

                            // send the stored messages
                            send_stored(sock_cli, buf, saved_topics, saved_msgs);
                            delete_from_stored(buf, saved_topics, saved_ids);
                        } else {
                            // two clients with the same id
                            //   --> exit the current client
                            printf("Client %s already connected.\n", buf);

                            close(sock_cli);
                            FD_CLR(sock_cli, &read_fds);
                        }
                    }
                } else if (i == sock_udp) {
                    ret = recvfrom(sock_udp, buf, BUFLEN, 0, (struct sockaddr *)&serv_addr_udp, &socklen);
                    DIE(ret < 0, "[UDP] Receive failed");

                    // cast the data to udp_msg
                    u_msg = (udp_msg *)buf;

                    t_msg.prt = ntohs(serv_addr_udp.sin_port);
                    strcpy(t_msg.udp_ip, inet_ntoa(serv_addr_udp.sin_addr));
                    decode_udp(u_msg, &t_msg);

                    for (auto& [id, topics] : client_topics) {
                        /** 
                         * check for the client to still be connected
                         * and be subscribed to the current topic
                         */
                        if (client_sockfds[id] != -1 && topics.find(t_msg.topic) != topics.end()) {
                            send(client_sockfds[id], &t_msg, sizeof(tcp_msg), 0);
                        }
                    }

                    // check if the message has to be stored
                    if (saved_topics.find(t_msg.topic) != saved_topics.end()) {
                        // store the message
                        saved_msgs.push_back({saved_topics[t_msg.topic].size(), t_msg});
                    }
                } else {
                    // receive a command from a client
                    ret = recv(i, buf, BUFLEN - 1, 0);
                    DIE(ret < 0, "[Client] Receive failed");

                    string id = client_ids[i];

                    // check for exit
                    if (ret == 0) {
                        printf("Client %s disconnected.\n", id.c_str());

                        client_sockfds[id] = -1;

                        if (saved_ids.find(id) != saved_ids.end()) {
                            update_store(id, saved_topics, saved_ids);
                        }

                        FD_CLR(i, &read_fds);
                        close(i);
                    } else {
                        // parse the command
                        s_msg = (sub_msg *)buf;

                        if (s_msg->type == 1) {
                            // subscribe
                            client_topics[id].insert(s_msg->topic);

                            if (s_msg->sf == 1) {
                                saved_ids[id].insert(s_msg->topic);
                            } else {
                                // if the topic was previously set with sf = 1
                                // then erase it from saved_ids
                                saved_ids[id].erase(s_msg->topic);
                            }
                        } else {
                            // unsubscribe
                            client_topics[id].erase(s_msg->topic);
                            saved_ids[id].erase(s_msg->topic);
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

    return 0;
}
