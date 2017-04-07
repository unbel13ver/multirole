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
#include <time.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#include "multirole.h"

static void *slave_receive_data_task(void *args);
static void *connection_monitor_task(void *args);
static void *waiting_for_master_task(void *args);
static void *fake_master_monitor_task(void *args);

static void *master_request_data_task(void *args);
static void *master_receive_data_task(void *args);
static void *master_send_payload_task(void *args);

static int32_t multirole_run(int32_t for_master, int32_t fake, struct sockaddr_in *mcast_addr, socklen_t addrlen);
static int32_t start_tasks(int32_t for_master, int32_t fake);
static void master_off(void);

pthread_mutex_t mon_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t recv_cond = PTHREAD_COND_INITIALIZER;

pthread_t slave_receive_data;
pthread_t connection_monitor;
pthread_t waiting_for_master;

pthread_t master_send_payload;
pthread_t master_request_data;
pthread_t master_receive_data;
pthread_t fake_master_monitor;

struct sockaddr_in mcast_addr = {0};	/* Multicast address */
struct fake_str fstr = {0}; 			/* Structure for fake master */

int32_t sock = 0;				/* Socket */
int32_t fake_master = 0;

/* For sa_handler*/
void sigint(int signo) {
	(void)signo;
}

int32_t main(int argc, char **argv)
{
	uint8_t is_master = 0;

	sigset_t sigset, oldset;
	struct sigaction s;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	pthread_sigmask(SIG_BLOCK, &sigset, &oldset);

	s.sa_handler = sigint;
	sigemptyset(&s.sa_mask);
	s.sa_flags = 0;
	sigaction(SIGINT, &s, NULL);


	if (argc > 1) /* May be gpio_get_value() or something else */
	{
		if (strcmp("MASTER", argv[1]) == 0)
		{
			is_master = 1;
			printf("Running as MASTER\n");
		}
	}

	sock = multirole_run(is_master, fake_master, &mcast_addr, sizeof(mcast_addr));

	pthread_sigmask(SIG_SETMASK, &oldset, NULL);

	pause();

	if ((is_master) || (fake_master))
	{
		master_off();
		if (fake_master)
		{
			pthread_cancel(fake_master_monitor);
			pthread_join(fake_master_monitor, NULL);
			close(fstr.fsock);
		}
	} else {
		pthread_cancel(slave_receive_data);
		pthread_join(slave_receive_data, NULL);
	}

	close(sock);

	return 0;
}

/* Service procedures */
static int32_t start_tasks(int32_t for_master, int32_t fake)
{
	int32_t error = 0;
	struct average avg = {0};

	if (for_master)
	{
		if (pthread_create(&master_request_data, NULL, master_request_data_task, NULL) != 0)
		{
			perror("master_request_data() start failed\n");
			error = -1;
		}
		if (pthread_create(&master_receive_data, NULL, master_receive_data_task, (void *)&avg) != 0)
		{
			perror("master_receive_data() start failed\n");
			error = -1;
		}
		if (pthread_create(&master_send_payload, NULL, master_send_payload_task, (void *)&avg) != 0)
		{
			perror("master_send_payload() start failed\n");
			error = -1;
		}
		if (fake) /* Create specific task if we are fake master */
		{
			fstr.fsock = create_new_socket(0, &fstr.faddr, sizeof(fstr.faddr));
			if (fstr.fsock < 0)
			{
				perror("fake_sock failed");
				exit(-1);
			}
			if (pthread_create(&fake_master_monitor, NULL, fake_master_monitor_task, NULL) != 0)
			{
				perror("fake_master_monitor() start failed\n");
				error = -1;
			}
		}
	} else {
		if (pthread_create(&slave_receive_data, NULL, slave_receive_data_task, NULL) != 0)
		{
			perror("slave_receive_data_task() start failed\n");
			error = -1;
		}
	}

	return error;
}

static int32_t multirole_run(int32_t for_master, int32_t fake, struct sockaddr_in *mcast_addr, socklen_t addrlen)
{
	int32_t int_sock = create_new_socket(for_master, mcast_addr, addrlen);
	if (int_sock < 0)
	{
		perror("Socket creation failed");
		exit(-1);
	}

	if (start_tasks(for_master, fake) != 0)
	{
		perror("start_tasks() failed");
		exit(-1);
	}

	return int_sock;
}

static void master_off(void)
{
	pthread_cancel(master_send_payload);
	pthread_cancel(master_request_data);
	pthread_cancel(master_receive_data);
	pthread_join(master_send_payload, NULL);
	pthread_join(master_request_data, NULL);
	pthread_join(master_receive_data, NULL);
}

/* Tasks description */
/* Task requests data from slaves */
static void *master_request_data_task(void *args)
{
	int32_t command = 0;

	/* If we are true master, send DETECT for fake master, if it exists.	*
	 * One send must be enought, but it is probably a problem place			*/
	if (!fake_master)
	{
		command = MASTER_DETECT;
		if (sendto(sock, &command, sizeof(command), 0, (struct sockaddr *)&mcast_addr, sizeof(mcast_addr)) < 0)
		{
			printf("smthng wrong with sendto\n");
		}
	}

	command = DATA_REQ_COMMAND;

	while (1)
	{
		if (sendto(sock, &command, sizeof(command), 0, (struct sockaddr *)&mcast_addr, sizeof(mcast_addr)) < 0)
		{
			printf("smthng wrong with sendto\n");
		}
		sleep(DATA_REQ_INTERV_SEC);
	}

	return 0;
}

/* Task sends payload (string to output and brightness) to slaves */
static void *master_send_payload_task(void *args)
{
	int32_t sendbuf[120] = {0};
	struct average *avg = (struct average *)args;

	while (1)
	{
		if ((avg->avg_temp !=0) && (avg->avg_br != 0))
		{
			/* in utils.c */
			create_payload(sendbuf, avg->avg_temp, avg->avg_br);

			if (sendto(sock, sendbuf, sizeof(sendbuf), 0, (struct sockaddr *)&mcast_addr, sizeof(mcast_addr)) < 0)
			{
				printf("smthng wrong with sendto\n");
			}
		}
		sleep(DATA_SEND_INTERV_SEC);
	}

	return 0;
}

/* Task receives data from slaves and counts slaves */
static void *master_receive_data_task(void *args)
{
	struct sockaddr_in slave_addr = {0};	/* Slave address */
	struct average *avg = (struct average *)args;
	int32_t buffer[10] = {0};
	socklen_t addrlen = sizeof(slave_addr);
	int32_t sl_cnt = 0;
	int32_t rank = 1;
	unsigned long ip_address = 0;
	int32_t known_slave = -1;

	struct slave_struct slaves[MAX_SLAVES];

	while (1)
	{
		if (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&slave_addr, &addrlen) < 0)
		{
			perror("Something wrong with recvfrom");
		}

		ip_address = slave_addr.sin_addr.s_addr;

		/* in utils.c */
		known_slave = slave_is_unknown(slaves, ip_address);		/* in utils.c */
		if (known_slave < 0)
		{
			/* If slave is unknown, add it to the list and send him a rank */
			slaves[sl_cnt].address = ip_address;
			slaves[sl_cnt].rank = rank;
			slave_set_rank(sock, rank, &slave_addr, addrlen);
			sl_cnt++;
			rank++;
			printf("New slave at %s registered\n", inet_ntoa(slave_addr.sin_addr));
		} else {
			if (HAVE_NO_RANK == buffer[3])
			{
				slave_set_rank(sock, slaves[known_slave].rank, &slave_addr, addrlen);
				printf("Slave %s reconnected\n", inet_ntoa(slave_addr.sin_addr));
			}
		}

		/* in utils.c */
		count_aveage_data(avg, buffer, sl_cnt);
	}

	return 0;
}

/* Task, that is waiting while true master appears. 		*
 * When it happens, task turns fake master back to slave.	*/
static void *fake_master_monitor_task(void *args)
{
	int32_t buffer[120] = {0};
	socklen_t addrlen = sizeof(fstr.faddr);
	char str_to_print[80] = {0};

	while(1)
	{
		if (recvfrom(fstr.fsock, buffer, sizeof(buffer), 0, (struct sockaddr *)&fstr.faddr, &addrlen) >= 0)
		{
			/* True master detected */
			if (buffer[0] == MASTER_DETECT)
			{
				/* Close all master tasks */
				master_off();
				/* Close all sockets */
				close(fstr.fsock);
				close(sock);
				/* Run slave */
				sock = multirole_run(0, 0, &mcast_addr, sizeof(mcast_addr));
				break;
			}
			if (buffer[0] == PAYLOAD_CMD)
			{
				bzero(str_to_print, sizeof(str_to_print));
				memcpy(str_to_print, &buffer[2], sizeof(str_to_print));
				printf("%s\n", str_to_print);
				set_display_brightness(buffer[1]);
			}
		} else {
			printf("recv problem\n");
		}
	}
	printf("Fake master OFF...\n");

	return 0;
}

/* Main slave task. Receives requests from master and answers back to it. */
static void *slave_receive_data_task(void *args)
{
	int32_t buffer[100] = {0};
	int32_t answer[10] = {0};
	char str_to_print[80] = {0};
	int32_t temp = 0, bright = 0;
	int32_t rank = HAVE_NO_RANK;

	socklen_t addrlen = sizeof(mcast_addr);

	if (pthread_create(&connection_monitor, NULL, connection_monitor_task, (void *)&rank) == 0)
	{
		printf("connection_monitor_task created\n");
	}

	while(1)
	{
		if (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&mcast_addr, &addrlen) >= 0)
		{
			/* Process received message */
			switch (buffer[0])
			{
				case (DATA_REQ_COMMAND):
				{
					generate_fake_data(&temp, &bright, rank);
					answer[0] = PAYLOAD_CMD;
					answer[1] = temp;
					answer[2] = bright;
					answer[3] = rank;
					if (sendto(sock, answer, sizeof(answer), 0, (struct sockaddr *)&mcast_addr, addrlen) < 0)
					{
						printf("Something wrong with sendto()\n");
					}
					break;
				}
				case (SET_RANK):
				{
					rank = buffer[1];
					printf("Rank %d assigned\n", rank);
					break;
				}
				case (PAYLOAD_CMD):
				{
					bzero(str_to_print, sizeof(str_to_print));
					memcpy(str_to_print, &buffer[2], sizeof(str_to_print));
					printf("%s\n", str_to_print);
					set_display_brightness(buffer[1]);
					break;
				}
				default: break;
			}
		} else {
			printf("recv problem\n");
		}

		/* Every received packet is signalling to "connection monitor" */
		pthread_mutex_lock(&mon_mutex);
			pthread_cond_signal(&recv_cond);
		pthread_mutex_unlock(&mon_mutex);
	}

	return 0;
}

/* Task is waiting for "packet from master received" signal.	*
 * If timeout reached, task runs "role changing" task 			*/
static void *connection_monitor_task(void *args)
{
	struct timespec time_to_wait = {0};
	int32_t err = 0;
	time_t t;

	while(1)
	{
		pthread_mutex_lock(&mon_mutex);
			time_to_wait.tv_sec = time(NULL) + TIMEOUT_SEC;
			err = pthread_cond_timedwait(&recv_cond, &mon_mutex, &time_to_wait);
		pthread_mutex_unlock(&mon_mutex);
		if (err == ETIMEDOUT)
		{
			t = time(NULL);
			printf("\nConnection lost at %s\n", ctime(&t));
			break;
		}
	}

	if (pthread_create(&waiting_for_master, NULL, waiting_for_master_task, args) == 0)
	{
		printf("waiting_for_master_task\n");
	}

	return 0;
}

/* Task waits for new master. If timeout reached, becomes a fake master 	*
 * The timeout value in seconds is corresponding to 2x slave rank. 			*/
static void *waiting_for_master_task(void *args)
{
	int32_t rank = *(int32_t *)args;
	*(int32_t *)args = 0;
	struct timespec time_to_wait = {0};
	int32_t err = 0;
	int32_t i_am_master = 0;
	if (0 == rank)
	{
		/* New client connected while master disconnected */
		rank = MAX_WAITING_TIME;
	} else {
		rank += rank; /* 2sec interval for avoid collision */
	}

	pthread_mutex_lock(&mon_mutex);
		time_to_wait.tv_sec = time(NULL) + rank;
		printf("Waiting %d second(s) for the master...\n", rank);
		err = pthread_cond_timedwait(&recv_cond, &mon_mutex, &time_to_wait);
		if (err == ETIMEDOUT)
		{
			printf("I am the new master now\n");
			i_am_master = 1;
		} else {
			printf("New master detected\n");
		}
	pthread_mutex_unlock(&mon_mutex);

	if (i_am_master)
	{
		/* Cancel slave task, close sock and rerun with "master" and "fake master" flags */
		pthread_cancel(slave_receive_data);
		pthread_join(slave_receive_data, NULL);
		close(sock);
		fake_master = 1;

		sock = multirole_run(i_am_master, fake_master, &mcast_addr, sizeof(mcast_addr));
	} else {
		/* Run connection monitor */
		if (pthread_create(&connection_monitor, NULL, connection_monitor_task, args) == 0)
		{
			printf("connection_monitor_task created\n");
		}
	}

	return 0;
}
