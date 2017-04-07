#ifndef _MULTIROLE_H_
#define _MULTIROLE_H_

/* Protocol commands */
#define DATA_REQ_COMMAND	(0xAA)
#define SET_RANK			(0xAB)
#define PAYLOAD_CMD			(0xBB)
#define HAVE_NO_RANK		(0x00)
#define MASTER_DETECT		(0xCC)

#define TIMEOUT_SEC				(7) /* "Connection lost" timeout */
#define DATA_REQ_INTERV_SEC		(5) /* Request data from slaves interval */
#define DATA_SEND_INTERV_SEC	(2) /* Send payload to slaves interval */
#define TEMP_MAX				(450) /* Fake temp between -45 and 45 Celsium degrees */
#define BRIGHT_MAX				(255) /* Fake brightness maximum */

#define MAX_SLAVES				(100) /* Maximum of slaves. Better to change this to dynamic memory aloocation */
#define MAX_WAITING_TIME		(600) /* 10 minutes */

struct slave_struct
{
	int32_t rank;
	unsigned long address;
};

struct average
{
	int32_t avg_temp;
	int32_t avg_br;
};

struct fake_str
{
	int32_t fsock;
	struct sockaddr_in faddr;
};

int32_t create_new_socket(int32_t for_master, struct sockaddr_in *mcast_addr, socklen_t addrlen);
int32_t slave_is_unknown(struct slave_struct *slaves, unsigned long ip_address);
int32_t slave_set_rank(int32_t sock, int32_t rank, struct sockaddr_in *address, socklen_t addrlen);
void count_aveage_data(struct average *avg, int32_t *buffer, int32_t count);
void generate_fake_data(int32_t *temp, int32_t *bright, int32_t pid);
void set_display_brightness(int32_t bright);
void create_payload(int32_t *buffer, int32_t temp, int32_t bright);

#endif /*_MULTIROLE_H_*/
