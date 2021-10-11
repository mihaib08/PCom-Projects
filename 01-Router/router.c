#include "skel.h"
#include "queue.h"
#include "list.h"
#include <netinet/if_ether.h>

#define MAXLEN 102
#define NMAX 100002

#define IP_OFF (sizeof(struct ether_header))
#define ICMP_OFF (IP_OFF + sizeof(struct iphdr))

struct route_table_entry {
	uint32_t prefix;
	uint32_t next_hop;
	uint32_t mask;
	int interface;
} __attribute__((packed));

struct arp_entry {
	uint32_t ip;
	uint8_t mac[6];
};

struct route_table_entry *rtable;
int rtable_size;

struct arp_entry *arp_table;
int arp_table_len;

void parse_route_table(char *fname) {
	FILE *fin = fopen(fname, "r");

	char prefix[MAXLEN], next_hop[MAXLEN], mask[MAXLEN], in[MAXLEN];
	int i = 0;

	while (fscanf(fin, "%s%s%s%s", prefix, next_hop, mask, in) > 0) {
		inet_aton(prefix, (struct in_addr *)&rtable[i].prefix);
		inet_aton(next_hop, (struct in_addr *)&rtable[i].next_hop);
		inet_aton(mask, (struct in_addr *)&rtable[i].mask);
		rtable[i].interface = atoi(in);

		i++;
	}

	rtable_size = i;
}

int cmp(const void *p1, const void *p2) {
    const struct route_table_entry r1 = *(struct route_table_entry *)p1;
    const struct route_table_entry r2 = *(struct route_table_entry *)p2;

    if (r1.prefix == r2.prefix) {
        return (r1.mask - r2.mask);
    }

    return (r1.prefix - r2.prefix);
}

struct route_table_entry *get_best_route(uint32_t dest_ip) {
	int l = 0;
	int r = rtable_size - 1;
	int m;

	int ind = -1;

	while (l <= r) {
		m = l + ((r - l) / 2);

		if ((dest_ip & rtable[m].mask) >= rtable[m].prefix) {
			ind = m;
			// search for a more _specific_ entry
			l = m + 1;
		} else {
			r = m - 1;
		}
	}

    if (ind >= 0) {
		return &rtable[ind];
	}

	return NULL;
}

struct arp_entry *get_arp_entry(__u32 ip) {
	int i;

	for (i = 0; i < arp_table_len; ++i) {
		if (arp_table[i].ip == ip) {
			return &arp_table[i];
		}
	}

    return NULL;
}

void record_arp_entry(uint32_t ip, uint8_t *mac) {
	int pos = arp_table_len;

	arp_table[pos].ip = ip;
	memcpy(arp_table[pos].mac, mac, 6);

	arp_table_len++;
}

uint16_t check_1624(struct iphdr* ip_hdr, uint16_t old_check, uint16_t m_prev) {
	// get the current (changed) 16-bit field from ttl
	uint16_t m_curr = (ntohs(*(uint16_t *)&ip_hdr->ttl) & 0xFFFF);

	uint32_t check = (ntohs(~old_check) & 0xFFFF);
	check -= m_prev;
	check += m_curr;

	while (check >> 16) {
		check = (check >> 16) + (check & 0xFFFF);
	}

	return htons(~check);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

	packet m;
	int rc;

	init(argc - 2, argv + 2);

	rtable = (struct route_table_entry*) malloc(NMAX * sizeof(struct route_table_entry));
	DIE(rtable == NULL, "memory");

	parse_route_table(argv[1]);

    // sort the rtable entries in ascending order of their prefixes
	qsort(rtable, rtable_size, sizeof(struct route_table_entry), cmp);

	arp_table = malloc(NMAX * sizeof(struct arp_entry));

    // broadcast
	uint8_t brd_addr[6];
	hwaddr_aton("ff:ff:ff:ff:ff:ff", brd_addr);

    queue q_pack, q_int, q_next;

	q_pack = queue_create(); // packet queue
	q_int = queue_create(); // interface queue
	q_next = queue_create(); // next_hop queue

    int ct = 0;
	while (1) {
		rc = get_packet(&m);
		DIE(rc < 0, "get_message");

		struct ether_header *eth_hdr = (struct ether_header *)m.payload;

		uint16_t eth_type = ntohs(eth_hdr->ether_type);
		if (eth_type == ETHERTYPE_IP) {
			struct iphdr *ip_hdr = (struct iphdr*)(m.payload + IP_OFF);

			char *r_ip = get_interface_ip(m.interface); // router's ip
			
			if (ip_hdr->daddr == inet_addr(r_ip)) {
				struct icmphdr *icmp_hdr = parse_icmp(m.payload);

				if (icmp_hdr != NULL && icmp_hdr->type == ICMP_ECHO) {
					uint16_t id = htons(getpid() & 0xFFFF);

					send_icmp(ip_hdr->saddr, ip_hdr->daddr,
					          eth_hdr->ether_dhost, eth_hdr->ether_shost,
					          ICMP_ECHOREPLY, 0, m.interface, id, htons(ct));
					
					ct++;
				}

				continue;
			}

			if (ip_hdr->ttl <= 1) {
				// time exceeded
				send_icmp_error(ip_hdr->saddr, ip_hdr->daddr,
				                eth_hdr->ether_dhost, eth_hdr->ether_shost,
				                ICMP_TIME_EXCEEDED, 0, m.interface);
				
				continue;
			}

            /**
			 * checksum is calculated in one's complement
			 *    --> ip_checksum(ip_hdr) == 0
			 */
			if (ip_checksum(ip_hdr, sizeof(struct iphdr))) {
				continue;
			}

			struct route_table_entry *best_route = get_best_route(ip_hdr->daddr);

			if (best_route == NULL) {
				send_icmp_error(ip_hdr->saddr, ip_hdr->daddr,
				                eth_hdr->ether_dhost, eth_hdr->ether_shost,
				                ICMP_DEST_UNREACH, 0, m.interface);
				
				continue;
			}

            // get the current 16-bit field from _ttl_
            uint16_t m_curr = ntohs(*(uint16_t *)&ip_hdr->ttl);

			// update ttl
			ip_hdr->ttl--;

			uint16_t curr_check = ip_hdr->check;
            ip_hdr->check = 0;
			ip_hdr->check = check_1624(ip_hdr, curr_check, m_curr);

            // update sender MAC
			get_interface_mac(best_route->interface, eth_hdr->ether_shost);

            // search for next_hop in arp_table
			struct arp_entry *curr_entry = get_arp_entry(best_route->next_hop);

            // next_hop wasn't found
			if (curr_entry == NULL) {
				struct ether_header *req_eth = (struct ether_header *)
				                                malloc(sizeof(struct ether_header));

				req_eth->ether_type = htons(ETHERTYPE_ARP);
				memcpy(req_eth->ether_dhost, brd_addr, 6);
				memcpy(req_eth->ether_shost, eth_hdr->ether_shost, 6);

				r_ip = get_interface_ip(best_route->interface);
				send_arp(best_route->next_hop, inet_addr(r_ip), req_eth,
				         best_route->interface, htons(ARPOP_REQUEST));

                // make a _copy_ of the packet and push it into queue
                packet *p = (packet *)malloc(sizeof(packet));
				p->interface = m.interface;
				p->len = m.len;
				memcpy(p->payload, m.payload, sizeof(m.payload));

                // push the necessary info in the queue
                queue_enq(q_pack, p);
				queue_enq(q_int, &best_route->interface);
				queue_enq(q_next, &best_route->next_hop);
			} else {
				// get the destination MAC
				memcpy(eth_hdr->ether_dhost, curr_entry->mac, 6);
				send_packet(best_route->interface, &m);
			}
		} else if (eth_type == ETHERTYPE_ARP) {
			struct arp_header *arp_hdr = parse_arp(m.payload);

			if (arp_hdr != NULL) {
				uint16_t op = ntohs(arp_hdr->op);

				char *r_ip = get_interface_ip(m.interface);
				uint32_t r_addr = inet_addr(r_ip); // router ip

				if (op == ARPOP_REQUEST && arp_hdr->tpa == r_addr) {
					struct ether_header *reply_eth = (struct ether_header *)
					                                  malloc(sizeof(struct ether_header));
					
					reply_eth->ether_type = eth_hdr->ether_type;
					memcpy(reply_eth->ether_dhost, eth_hdr->ether_shost, 6);
					get_interface_mac(m.interface, reply_eth->ether_shost);

					send_arp(arp_hdr->spa, arp_hdr->tpa, reply_eth,
					         m.interface, htons(ARPOP_REPLY));
				} else if (op == ARPOP_REPLY) {
					// update arp_table
					record_arp_entry(arp_hdr->spa, arp_hdr->sha);

					while (1) {
						if (queue_empty(q_pack)) {
							break;
						}

						packet p = *(packet *)queue_front(q_pack);
						int interface = *(int *)queue_front(q_int);
						uint32_t next_hop = *(uint32_t *)queue_front(q_next);

                        // check if next_hop((packet)p) is now in arp_table
						struct arp_entry *curr_entry = get_arp_entry(next_hop);

						if (curr_entry != NULL) {
							struct ether_header *p_eth_hdr = (struct ether_header *)p.payload;
							memcpy(p_eth_hdr->ether_dhost, curr_entry->mac, 6);

							queue_deq(q_pack);
							queue_deq(q_int);
							queue_deq(q_next);

							send_packet(interface, &p);
						} else {
							break;
						}
					}
				}
			}
		}
	}
}
