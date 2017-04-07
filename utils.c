#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "multirole.h"

static int32_t read_config(char *ipaddr, int32_t *portnum);

int32_t create_new_socket(int32_t for_master, struct sockaddr_in *mcast_addr, socklen_t addrlen)
{
	int32_t sock = 0;
	int32_t enable = 1;			/* Socket opt switch */
	struct ip_mreq mreq = {0};				/* Multicast group*/
	char ipaddr[16] = {0};
	int32_t portnum = 0;

	/* Create socket for sending/receiving datagrams */
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		perror("socket() failed");
	}

	if (read_config(ipaddr, &portnum) != 0)
	{
		return -1;
	}

	/* Construct local address structure */
	mcast_addr->sin_family = AF_INET;						/* internet address family */
	mcast_addr->sin_port = htons(portnum);				/* Multicast port */
	mcast_addr->sin_addr.s_addr = inet_addr(ipaddr);	/* Multicast IP address */

	if (!for_master)
	{
		mcast_addr->sin_addr.s_addr = htonl(INADDR_ANY);	/* Receive from any */
		/* Reuse port */
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&enable, sizeof(enable)) < 0)
		{
			perror("setsockopt(SO_REUSEADDR) failed");
			exit(1);
		}

		if (bind(sock, (struct sockaddr *)mcast_addr, addrlen))
		{
			perror("bind() failed");
			return -1;
		}

		/* Registering in multicast group */
		mreq.imr_multiaddr.s_addr = inet_addr(ipaddr);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
		{
			perror("setsockopt(IP_ADD_MEMBERSHIP) failed");
			exit(1);
		}
	}

	return sock;
}

void generate_fake_data(int32_t *temp, int32_t *bright, int32_t pid)
{
	srand(time(NULL) * pid);

	*temp = -TEMP_MAX + rand() % TEMP_MAX*2;

	srand(time(NULL) * pid);

	*bright = rand() % BRIGHT_MAX;
}

void set_display_brightness(int32_t bright)
{
	/* do something with brightness*/
	/*printf("Service info: brightness %d set.\n\n", bright);*/
}

void create_payload(int32_t *buffer, int32_t temp, int32_t bright)
{
	char str_time[80] = {0};
	char str_temp[30] = {0};
	time_t t;

	buffer[0] = PAYLOAD_CMD;
	memset(str_time, 0, sizeof(str_time));
	memset(str_temp, 0, sizeof(str_temp));

	t = time(NULL);
	snprintf(str_time, sizeof(str_time), "Date: %s", ctime(&t));
	str_time[strlen(str_time) - 1] = '\0';
	snprintf(str_temp, sizeof(str_temp),", temperature %3d.%d%c", temp/10, abs(temp%10), '\0');
	strcat(str_time, str_temp);

	buffer[1] = bright;
	memcpy(&buffer[2], str_time, strlen(str_time));
}

int32_t slave_is_unknown(struct slave_struct *slaves, unsigned long ip_address)
{
	int32_t known = -1;

	for (int i = 0; i < MAX_SLAVES; i++)
	{
		if (ip_address == slaves[i].address)
		{
			known = i;
			break;
		}
	}

	return known;
}

int32_t slave_set_rank(int32_t sock, int32_t rank, struct sockaddr_in *address, socklen_t addrlen)
{
	int32_t rank_buf[2] = {SET_RANK, rank};
	int32_t result = 0;

	if (sendto(sock, rank_buf, sizeof(rank_buf), 0, (struct sockaddr *)address, addrlen) < 0)
	{
		printf("smthng wrong with sendto\n");
		result = 1;
	}

	return result;
}

void count_aveage_data(struct average *avg, int32_t *buffer, int32_t count)
{
	static int32_t data_recv = 0;
	static int32_t temper = 0;
	static int32_t bright = 0;

	data_recv++;
	temper += buffer[1];
	bright += buffer[2];

	if (data_recv > count)
	{
		avg->avg_temp = temper/data_recv;
		avg->avg_br = bright/data_recv;
		data_recv = 0;
		temper = 0;
		bright = 0;
	}
}


static int32_t read_config(char *ipaddr, int32_t *portnum)
{
	FILE *config = NULL;
	int32_t error = 0;

	if ((config = fopen("multirole.cfg", "r+")) == NULL)
	{
		perror("read_config() error");
		error = -1;
	} else {
		if (fscanf(config, "%s\n%d", ipaddr, portnum) != 2)
		{
			perror("wrong config file");
			error = -1;
		}
		fclose(config);
	}

	return error;
}
