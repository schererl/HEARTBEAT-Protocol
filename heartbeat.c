#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <unistd.h>

#include "raw.h"
#include "pthread.h"
#include <time.h>
uint8_t this_mac[6];
char this_hostname[16];

char bcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
char ifName[IFNAMSIZ];

struct host_info
{
	char hostname[16];
	uint8_t mac_addr[6];
	time_t last_beat;
};

struct host_info arr_hosts[100];
uint8_t len_hosts = 0;

void addNewHost(char *hostName, char *mac)
{
	memcpy(arr_hosts[len_hosts].hostname, hostName, sizeof(arr_hosts[len_hosts].hostname));
	time(&arr_hosts[len_hosts].last_beat);
	memcpy(arr_hosts[len_hosts].mac_addr, mac, sizeof(arr_hosts[len_hosts].mac_addr));

	len_hosts++;
}


int sendRaw(char type, char *data, char *dst)
{
	struct ifreq if_idx, if_mac, ifopts;
	struct sockaddr_ll socket_address;
	int sockfd, numbytes;

	uint8_t raw_buffer[ETH_LEN];
	struct eth_frame_s *raw = (struct eth_frame_s *)&raw_buffer;

	/* Open RAW socket */
	if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
		perror("socket");

	/* Set interface to promiscuous mode */
	strncpy(ifopts.ifr_name, ifName, IFNAMSIZ - 1);
	ioctl(sockfd, SIOCGIFFLAGS, &ifopts);
	ifopts.ifr_flags |= IFF_PROMISC;
	ioctl(sockfd, SIOCSIFFLAGS, &ifopts);

	/* Get the index of the interface */
	memset(&if_idx, 0, sizeof(struct ifreq));
	strncpy(if_idx.ifr_name, ifName, IFNAMSIZ - 1);
	if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
		perror("SIOCGIFINDEX");
	socket_address.sll_ifindex = if_idx.ifr_ifindex;
	socket_address.sll_halen = ETH_ALEN;

	/* Get the MAC address of the interface */
	memset(&if_mac, 0, sizeof(struct ifreq));
	strncpy(if_mac.ifr_name, ifName, IFNAMSIZ - 1);
	if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0)
		perror("SIOCGIFHWADDR");
	memcpy(this_mac, if_mac.ifr_hwaddr.sa_data, 6);

	/* fill the Ethernet frame header */
	memcpy(raw->ethernet.dst_addr, dst, 6);
	memcpy(raw->ethernet.src_addr, this_mac, 6);
	raw->ethernet.eth_type = htons(ETHER_TYPE);

	/* fill heartbeat data */
	raw->heartbeat.type = type;

	strncpy(raw->heartbeat.hostname, this_hostname, sizeof(this_hostname));
	if (data != NULL)
	{
		memcpy(raw->heartbeat.talk_msg, data, sizeof(raw->heartbeat.talk_msg));
	}

	/* Send it.. */
	memcpy(socket_address.sll_addr, dst, 6);
	if (sendto(sockfd, raw_buffer, sizeof(struct eth_hdr_s) + sizeof(struct heartbeat_hdr_s), 0,
			   (struct sockaddr *)&socket_address, sizeof(struct sockaddr_ll)) < 0)
		printf("Send failed\n");

	return 0;
}


void *recvRaw(void *param)
{
	struct ifreq ifopts;
	int sockfd;

	uint8_t raw_buffer[ETH_LEN];
	struct eth_frame_s *raw = (struct eth_frame_s *)&raw_buffer;

	/* Open RAW socket */
	if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
		perror("socket");

	/* Set interface to promiscuous mode */
	strncpy(ifopts.ifr_name, ifName, IFNAMSIZ - 1);
	ioctl(sockfd, SIOCGIFFLAGS, &ifopts);
	ifopts.ifr_flags |= IFF_PROMISC;
	ioctl(sockfd, SIOCSIFFLAGS, &ifopts);

	/* End of configuration. Now we can receive data using raw sockets. */
	while (1)
	{
		recvfrom(sockfd, raw_buffer, ETH_LEN, 0, NULL, NULL);

		// Analisa o pacote se for do tipo do nosso protocolo e tiver vindo de outro host (nao processa o prorio pacote)
		if (raw->ethernet.eth_type == ntohs(ETHER_TYPE) && memcmp(raw->ethernet.src_addr, this_mac, sizeof(this_mac)) != 0)
		{
			if (raw->heartbeat.type == TYPE_TALK)
			{
				printf("Talk from %s: %s\n", raw->heartbeat.hostname, raw->heartbeat.talk_msg);
			}
			else if (raw->heartbeat.type == TYPE_HEARTBEAT)
			{
				int h_index = -1;
				for (int i = 0; i < len_hosts; i++)
				{
					if (strncmp(arr_hosts[i].hostname, raw->heartbeat.hostname, sizeof(raw->heartbeat.hostname)) == 0)
					{
						time(&arr_hosts[i].last_beat);
						h_index = i;
						break;
					}
				}

				// Se recebeu um heartbeat e nao achou na tabela atual, significa que o host atual foi iniciado depois da msg
				// de start do host que enviou este heartbeat, entao devemos adiciona-lo a tabela de hosts.
				if (h_index < 0)
					addNewHost(raw->heartbeat.hostname, raw->ethernet.src_addr);
			}
			else // adiciona o novo host no array (START)
			{
				addNewHost(raw->heartbeat.hostname, raw->ethernet.src_addr);
				sendRaw(TYPE_HEARTBEAT, NULL, raw->ethernet.src_addr);
			} 
		}
	}
}

int sendStart()
{
	return sendRaw(TYPE_START, NULL, bcast_mac);
}

int sendHB()
{
	return sendRaw(TYPE_HEARTBEAT, NULL, bcast_mac);
}

int sendTalk(char *data, char *dst_mac)
{
	return sendRaw(TYPE_TALK, data, dst_mac);
}

void startHeartbeat()
{
	while (1)
	{
		sleep(5);
		sendHB();
	}
}

void getHostsList()
{
	time_t c_time;
	printf("------ LIST DE HOSTS ATIVOS ------\n");
	for (int i = 0; i < len_hosts; i++)
	{
		time(&c_time);
		if (difftime(c_time, arr_hosts[i].last_beat) <= 15)
		{
			printf("%s | %s\n", arr_hosts[i].hostname, ctime(&arr_hosts[i].last_beat));
		}
	}
	printf("------ FIM ------\n");
}

char *searchDestAddr(char *nome)
{
	time_t c_time;
	for (int i = 0; i < len_hosts; i++)
	{
		time(&c_time);
		if (difftime(c_time, arr_hosts[i].last_beat) <= 15 && strncmp(arr_hosts[i].hostname, nome, sizeof(arr_hosts[i].hostname)) == 0)
		{
			return arr_hosts[i].mac_addr;
		}
	}

	return NULL;
}

void waitingInput()
{
	char opt[4];
	while (1)
	{
		printf("Digite 'talk' para enviar uma mensagem, ou 'list' para listar hosts.\n");
		scanf("%s", opt);
		if (strcmp(opt, "talk") == 0)
		{
			char dst[16];
			printf("Digite o nome do destino.\n");
			scanf("%s", dst);

			char *addrDest = searchDestAddr(dst);

			if (addrDest == NULL)
			{
				printf("Este destino nao existe ou esta valido.\n");
				continue;
			}

			char buff[32];
			printf("Digite sua mensagem.\n");
			scanf("%s", buff);

			sendTalk(buff, addrDest);
		}
		else if (strcmp(opt, "list") == 0)
			getHostsList();
	}
}

void getHostname()
{
	gethostname(this_hostname, sizeof(this_hostname) - 1);
}

int main(int argc, char *argv[])
{
	pthread_t th_recv, th_waitInput, th_hearBeat;

	/* Get interface name */
	if (argc > 1)
		strcpy(ifName, argv[1]);
	else
		strcpy(ifName, DEFAULT_IF);

	getHostname();

	sendStart();

	pthread_create(&th_recv, NULL, recvRaw, NULL);
	pthread_create(&th_waitInput, NULL, waitingInput, NULL);
	pthread_create(&th_hearBeat, NULL, startHeartbeat, NULL);

	pthread_join(th_waitInput, NULL);

	return 0;
}
