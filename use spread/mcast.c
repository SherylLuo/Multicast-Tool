#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>


#define MAX_VSSETS 10
#define MAX_MESS_LEN  1400
#define MAX_MEMBERS   100
#define NORMAL_TYPE 1
#define TOKEN_TYPE 2
#define TERM_TYPE 3
#define MESS_PER_ROUND 100
#define WINDOW_LEN 1000

typedef struct 
{
  int type;
  int process_index;
  int message_index;
  int random_number;
  char payload[1200];
} message;

typedef struct
{
  int process_index;
  int message_index;
  int random_number;
} buf_res;

static	char	User[80];
static  char    Spread_name[80];
static  char    Group_name[80];
static  int     To_exit = 0;
static  mailbox Mbox;
static  char    Private_group[MAX_GROUP_NAME];
int 			num_of_messages;
int				process_index;
int 			num_of_processes;
char            fileName[10] = {'\0'};
FILE            *fw;
buf_res         window[WINDOW_LEN];
int             num_of_term = 0;
struct timeval  start;
struct timeval  end;
int             transfer_start = 0;
int             win_size = 0;
int             msg_sent = 0;
int             finished = 0;

static void Print_help();
static void Usage(int argc, char *argv[]);
static void Read_message();
static void Bye();
double diffTime(struct timeval left, struct timeval right);

int main(int argc, char *argv[])
{
    int     ret;
    int     mver, miver, pver;
    sp_time test_timeout;

    test_timeout.sec = 5;
    test_timeout.usec = 0;

    Usage( argc, argv );
    if (!SP_version( &mver, &miver, &pver)) 
    {
        printf("main: Illegal variables passed to SP_version()\n");
        Bye();
    }
    printf("Spread library version is %d.%d.%d\n", mver, miver, pver);

    ret = SP_connect_timeout( Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout);
    if( ret != ACCEPT_SESSION ) 
    {
        SP_error( ret );
        Bye();
    }
    printf("User: connected to %s with private group %s\n", Spread_name, Private_group );

    ret = SP_join( Mbox, Group_name );
    if( ret < 0 ) {
        SP_error( ret );
        Bye();
    }

    sprintf(fileName, "%d.out", process_index);
    if((fw = fopen(fileName, "w")) == NULL) {
        perror("mcast: open file error");
    }

    /*event interface*/

    E_init();

    E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );

    E_handle_events();

    return( 0 );
}

static  void    Read_message() 
{
    static  char     mess[MAX_MESS_LEN];
    char             sender[MAX_GROUP_NAME];
    char             target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
    membership_info  memb_info;
    vs_set_info      vssets[MAX_VSSETS];
    unsigned int     my_vsset_index;
    int              num_vs_sets;
    char             members[MAX_MEMBERS][MAX_GROUP_NAME];
    int              num_groups;
    int              service_type;
    int16            mess_type;
    int              endian_mismatch;
    int              i,j;
    int              ret;
    message          *msg;


    service_type = 0;

    ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
        &mess_type, &endian_mismatch, sizeof(mess), mess );

    if( ret < 0 ) 
    {
        if ( (ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT) ) {
            service_type = DROP_RECV;
            printf("\n========Buffers or Groups too Short=======\n");
            ret = SP_receive( Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups, 
                &mess_type, &endian_mismatch, sizeof(mess), mess );
        }
    }

    if (ret < 0 )
    {
        if( ! To_exit )
        {
            printf("%d\n", ret );
            SP_error( ret );
            printf("\n============================\n");
            printf("\nBye.\n");

        }
        exit( 0 );
    }

    if( Is_regular_mess( service_type ) )
    {       
        // printf("message from %s, of type %d, (endian %d) to %d groups \n(%d bytes): %s\n",
        //   sender, mess_type, endian_mismatch, num_groups, ret, mess );
    }else if( Is_membership_mess( service_type ) )
    {
        ret = SP_get_memb_info( mess, service_type, &memb_info );
        if (ret < 0) {
            printf("BUG: membership message does not have valid body\n");
            SP_error( ret );
            exit( 1 );
        }

        /* deal with membership message*/

        if( Is_reg_memb_mess( service_type ) )
        {
            printf("Received REGULAR membership for group %s with %d members, where I am member %d:\n",
                sender, num_groups, mess_type );
            for( i=0; i < num_groups; i++ )
                printf("\t%s\n", &target_groups[i][0] );
            printf("grp id is %d %d %d\n",memb_info.gid.id[0], memb_info.gid.id[1], memb_info.gid.id[2] );

            if( Is_caused_join_mess( service_type ) )
            {
                printf("Due to the JOIN of %s\n", memb_info.changed_member );
            }else if( Is_caused_leave_mess( service_type ) ){
                printf("Due to the LEAVE of %s\n", memb_info.changed_member );
            }else if( Is_caused_disconnect_mess( service_type ) ){
                printf("Due to the DISCONNECT of %s\n", memb_info.changed_member );
            }else if( Is_caused_network_mess( service_type ) ){
                printf("Due to NETWORK change with %u VS sets\n", memb_info.num_vs_sets);
                num_vs_sets = SP_get_vs_sets_info( mess, &vssets[0], MAX_VSSETS, &my_vsset_index );
                if (num_vs_sets < 0) {
                    printf("BUG: membership message has more then %d vs sets. Recompile with larger MAX_VSSETS\n", MAX_VSSETS);
                    SP_error( num_vs_sets );
                    exit( 1 );
                }
                for( i = 0; i < num_vs_sets; i++ )
                {
                    printf("%s VS set %d has %u members:\n",
                        (i  == my_vsset_index) ?
                        ("LOCAL") : ("OTHER"), i, vssets[i].num_members );
                    ret = SP_get_vs_set_members(mess, &vssets[i], members, MAX_MEMBERS);
                    if (ret < 0) {
                        printf("VS Set has more then %d members. Recompile with larger MAX_MEMBERS\n", MAX_MEMBERS);
                        SP_error( ret );
                        exit( 1 );
                    }
                    for( j = 0; j < vssets[i].num_members; j++ )
                        printf("\t%s\n", members[j] );
                }
            }
        }else if( Is_transition_mess(   service_type ) ) {
            printf("received TRANSITIONAL membership for group %s\n", sender );
        }else if( Is_caused_leave_mess( service_type ) ){
            printf("received membership message that left group %s\n", sender );
        }else 
          printf("received incorrecty membership message of type 0x%x\n", service_type );
    } else if ( Is_reject_mess( service_type ) )
    {
        printf("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n",
          sender, service_type, mess_type, endian_mismatch, num_groups, ret, mess );
    }else 
        printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);


    if(memb_info.gid.id[2] == num_of_processes) {

        if(transfer_start == 0) {
            transfer_start = 1;
            printf("Transmission start!\n");
            gettimeofday(&start, NULL);
        }

        msg = (message *)mess;
        if(msg->type != TERM_TYPE) {
            if(msg->type == NORMAL_TYPE) {
                window[win_size].process_index = msg->process_index;
                window[win_size].message_index = msg->message_index;
                window[win_size].random_number = msg->random_number;
                win_size++;
            }
            /* Check if we have more messags to send */
            /* A process will send a burst of message only after it receives all the messages it sent last burst */
            if(finished == 0 && (msg_sent == 0 || (msg->message_index == msg_sent - 1 && msg->process_index == process_index))) {
                for(int i = 0; i < MESS_PER_ROUND && msg_sent < num_of_messages; i++) {
                    msg->type = NORMAL_TYPE;
                    msg->process_index = process_index;
                    msg->message_index = msg_sent++;
                    msg->random_number = rand() % 1000000 + 1;
                    ret = SP_multicast(Mbox, AGREED_MESS, Group_name, mess_type, sizeof(message), (const char *)msg);
                    if(ret < 0) {
                        SP_error(ret);
                        Bye();
                    }
                }
            }
            /* If no more messages to send, set state to finished and send out a termination message */
            if(msg_sent == num_of_messages && finished == 0) {
                finished = 1;
                msg->type = TERM_TYPE;
                ret = SP_multicast(Mbox, AGREED_MESS, Group_name, mess_type, sizeof(message), (const char *)msg);
                if(ret < 0) {
                    SP_error(ret);
                    Bye();
                }
            }
            /* If window is full, deliver messages in window */
            if(win_size == WINDOW_LEN) {
                for(int i = 0; i < WINDOW_LEN; i++) {
                    fprintf(fw, "%2d, %8d, %8d\n", window[i].process_index, window[i].message_index, window[i].random_number);
                }
                win_size = 0;
            }

        } else {

            /* If receive enough termination message, deliver all the messages and terminate */

            num_of_term++;
            if(num_of_term == num_of_processes) {
                gettimeofday(&end, NULL);
                double transTime = diffTime(start, end);
                for(int i = 0; i < win_size; i++) {
                    fprintf(fw, "%2d, %8d, %8d\n", window[i].process_index, window[i].message_index, window[i].random_number);
                }
                fclose(fw);
                SP_disconnect(Mbox);
                printf("Transfer time: %fs\n", transTime);
                exit(0);
            }
        }
    }
}

double diffTime(struct timeval left, struct timeval right)
{
    struct timeval diff;
    double result;

    diff.tv_sec  = right.tv_sec - left.tv_sec;
    diff.tv_usec = right.tv_usec - left.tv_usec;

    if (diff.tv_usec < 0) {
        diff.tv_usec += 1000000;
        diff.tv_sec--;
    }

    if (diff.tv_sec < 0) {
        printf("WARNING: diffTime has negative result, returning 0!\n");
        diff.tv_sec = diff.tv_usec = 0;
    }

    result = diff.tv_sec + (double)diff.tv_usec / 1000000;

    return result;
}

static  void    Print_help()
{
    printf( "mcast <num_of_messages> <process_index> <num_of_processes>\n");          
    exit(0);
}

static	void	Usage(int argc, char *argv[])
{
  	sprintf( User, "hbqy" );
  	sprintf( Spread_name, "4803");
    sprintf( Group_name, "hbqy437");
  	if (argc != 4)
  	{
  		  Print_help();
  	}
  	num_of_messages = atoi(argv[1]);
  	process_index = atoi(argv[2]);
  	num_of_processes = atoi(argv[3]);
}

static  void  Bye()
{
    To_exit = 1;

    printf("\nBye.\n");

    SP_disconnect( Mbox );

    exit( 0 );
}
