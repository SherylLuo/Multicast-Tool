#include <stdio.h>

#include <stdlib.h>
#include <unistd.h>

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <sys/time.h>

#include <errno.h>

#include "linkedlist.h"
#include "recv_dbg.h"

#define PORT	     10350

#define MAX_MESS_LEN 	1400
#define PAYLOAD_LEN 	1300
#define WINDOW_LEN 		344
#define NAME_LEN 		80
#define MESS_PER_ROUND	20

#define PACKET_TYPE 2
#define TOKEN_TYPE 4
#define INIT_TYPE 8
#define TERM_TYPE 16

#define mcast_addr 225 << 24 | 1 << 16 | 2 << 8 | 125 /* (225.1.2.125) */

typedef struct 
{
	int 	type;
	int 	machine_index;
	int 	packet_index;
	int 	random_num;
	char 	payload[PAYLOAD_LEN];
} packet;

typedef struct 
{
	int 	type;
	int 	seq;
	int 	aru;
	int 	random;
	int 	max_remain_pack;
	int 	last_change_machine;
	int 	rtr[WINDOW_LEN];
} token;

typedef struct 
{
	int 	type;
	int 	sender_index;
	char    sender_host[NAME_LEN];
	int 	receivedNext;
	int 	receivedByLast;
} init;

