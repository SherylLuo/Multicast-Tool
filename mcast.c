#include "net_include.h"

int gethostname(char*,size_t);
void start(int sr, int my_index, fd_set mask, fd_set dummy_mask);
void initRing(int my_index, int machine_num, int loss_rate, int sr, int ss, fd_set mask, fd_set dummy_mask);
double diffTime(struct timeval left, struct timeval right);

char               my_name[NAME_LEN] = {'\0'}; /* my host name */
char               my_next_machine[NAME_LEN] = {'\0'}; /* the next machine's host name in the ring, used for sending token by unicast */
struct sockaddr_in send_addr;

/* use for unicast */
struct hostent     h_ent;
struct hostent     *p_h_ent;
int                host_num;

int main(int argc, char *argv[])
{
    struct sockaddr_in name;

    struct ip_mreq     mreq;
    unsigned char      ttl_val;
    unsigned char      loop;
    int                ss,sr;
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    int                packet_num;
    int                loss_rate;
    int                my_index; /* my machine index */
    int                machine_num; /* number of machines */
    FILE               *fw;
    char               fileName[NAME_LEN] = {'\0'};
    packet             *window_buf[WINDOW_LEN];
    token              *token_buf;
    int                local_aru = -1;
    token              *temp_buf;
    int                local_seq = -1;
    int                last_token_aru = -1; /* token aru in last round */
    int                transmit = 0; /* start transfer packets or not */
    struct timeval     timeout;
    int                last_rand = 0; /* the random number in last token received, used for distinguish duplicate tokens */
    int                deliver_seq = 0; /* First packet to be delivered */

    struct timeval     start_time;
    struct timeval     end_time;

    if(argc != 5) {
        perror("mcast ussage: mcast <number_of_packets> <machine_index> <number of machines> <loss rate>\n");
        exit(1);
    }

    packet_num = (int) strtol(argv[1], (char **)NULL, 10);
    my_index = (int)strtol(argv[2], (char **)NULL, 10);
    machine_num = (int)strtol(argv[3], (char **)NULL, 10);
    loss_rate = (int)strtol(argv[4], (char **)NULL, 10);

    sprintf(fileName, "%d.out", my_index);

    if((fw = fopen(fileName, "w")) == NULL) {
        perror("Mcast: open file error");
    }

    sr = socket(AF_INET, SOCK_DGRAM, 0); /* socket for receiving */
    if (sr<0) {
        perror("Mcast: socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    if ( bind( sr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = htonl( mcast_addr );

    /* the interface could be changed to a specific interface if needed */
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)) < 0) 
    {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, sizeof(ttl_val)) < 0) 
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val );
    }

    /*set the IP_MULTICAST_LOOP*/
    loop = 0;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)))
    {
      printf("Mcast: problem in setsockopt of multicast loop\n");
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );

    recv_dbg_init(loss_rate, my_index);

    start(sr, my_index, mask, dummy_mask);
    initRing(my_index, machine_num, loss_rate, sr, ss, mask, dummy_mask);

    token_buf = malloc(sizeof(token));
    if(token_buf == NULL) {
        perror("mcast: malloc error");
        exit(1);
    }
    token_buf->type = TOKEN_TYPE;
    token_buf->aru = -1;
    token_buf->last_change_machine = 0;

    for(int i = 0; i < WINDOW_LEN; i++) {
        window_buf[i] = malloc(sizeof(packet));
        if(window_buf[i] == NULL) {
            perror("mcast: malloc error");
            exit(1);
        }
        window_buf[i]->type = PACKET_TYPE;
        window_buf[i]->packet_index = -1;
    }

    temp_buf = malloc(sizeof(token));
    if(temp_buf == NULL) {
        perror("mcast: malloc error");
        exit(1);
    }

    gettimeofday(&start_time, NULL);

    for(;;)
    {
        temp_mask = mask;
        timeout.tv_sec = 0;
        timeout.tv_usec = 1800;

        /* First machine sends first message */
        if(transmit == 0 && my_index == 1) {
            transmit = 1;
            int i = 0;
            for(; i < MESS_PER_ROUND && packet_num > 0; i++) {
                window_buf[i]->packet_index = i;
                window_buf[i]->random_num = rand() % 1000000 + 1;
                window_buf[i]->machine_index = my_index;
                sendto(ss, window_buf[i], sizeof(packet), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                packet_num--;
            }
            local_aru = i - 1;
            token_buf->aru = local_aru;
            token_buf->seq = local_aru;
            token_buf->max_remain_pack = packet_num;
            token_buf->random = rand() % 1000000 + 1;
            token_buf->last_change_machine = 1;
            for(int j = 0; j < WINDOW_LEN; j++) {
                token_buf->rtr[j] = -2;
            }

            send_addr.sin_family = AF_INET;
            send_addr.sin_addr.s_addr = host_num;  /* unicast address */
            send_addr.sin_port = htons(PORT);

            sendto(ss, token_buf, sizeof(token), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
        }

        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
        if (num > 0) {

            if ( FD_ISSET( sr, &temp_mask) ) {
                /* when receving, actually we don't know it's a packet or a token, so receive it as a token first 
                since token size is largerand, then analysis */
                bytes = recv_dbg( sr, (char *)temp_buf, sizeof(token), 0 );
                if(bytes <= 0) {
                    continue;
                }

                if(temp_buf->type == PACKET_TYPE) {
                    packet *temp = (packet *)temp_buf;
                    transmit = 1;
                    /* If the packet locates within current window range, that should be a rtr of certain program */
                    if(temp->packet_index >= deliver_seq && temp->packet_index <= local_seq) {
                        memcpy(window_buf[temp->packet_index % WINDOW_LEN], temp_buf, sizeof(packet));
                        /* update local aru */
                        if(temp->packet_index == local_aru + 1) {
                            local_aru++;
                            while(local_aru <= local_seq && window_buf[(local_aru + 1) % WINDOW_LEN]->packet_index == local_aru + 1) {
                                local_aru++;
                            }
                        }
                        continue;
                    }
                    /* If window buffer is full, or if the packet index lower than first index pending deliver, 
                    or if the packet index is higher than or equal to first index pending deliver plus window length 
                    (means the packet comes too early), drop the packet */
                    if(local_seq == deliver_seq + WINDOW_LEN - 1 || temp->packet_index < deliver_seq || 
                        temp->packet_index >= deliver_seq + WINDOW_LEN) {
                        continue;
                    }

                    memcpy(window_buf[temp->packet_index % WINDOW_LEN], temp, sizeof(packet));

                    local_seq = temp->packet_index;
                    /* If received the packet next to local_aru, renew loacl_aru */
                    if(temp->packet_index == local_aru + 1) {
                        local_aru++;
                    }

                } else if(temp_buf->type == TOKEN_TYPE) {
                    /* If token seq less than current token_buf seq, that is a duplicate token */
                    if(temp_buf->seq < token_buf->seq)
                        continue;

                    /* If token seq equal to current token_buf seq, it may be duplicate or transmisstion is about to terminate */
                    if(temp_buf->seq == token_buf->seq) {
                        /* Use random number of a token and its aru together to confirm whether a token is duplicate or not */
                        if(temp_buf->random == last_rand && temp_buf->aru == last_token_aru) 
                            continue;

                        if(temp_buf->aru == local_aru && local_aru == temp_buf->seq && 
                            local_aru == last_token_aru && temp_buf->max_remain_pack == 0) {

                            send_addr.sin_family = AF_INET;
                            send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
                            send_addr.sin_port = htons(PORT);

                            token_buf->type = TERM_TYPE;
                            /* If a process make sure it's able to terminate according to above conditions, it multicasts 
                            a token with TERM_TYPE to let any other machine terminate as well */
                            sendto(ss, token_buf, sizeof(token), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                            for(int i = deliver_seq; i <= local_aru; i++) {
                                packet *temp = window_buf[i % WINDOW_LEN];
                                fprintf(fw, "%2d, %8d, %8d\n", temp->machine_index, temp->packet_index,  temp->random_num);
                            }
                            fclose(fw);
                            free(temp_buf);
                            free(token_buf);
                            for(int j = 0; j < WINDOW_LEN; j++) {
                                free(window_buf[j]);
                            }
                            printf("Machine %d finished transfer!\n", my_index);
                            break;
                        }
                    }

                    memcpy(token_buf, temp_buf, sizeof(token));
                    int before_change_aru = token_buf->aru;

                    if(packet_num > token_buf->max_remain_pack) {
                        token_buf->max_remain_pack = packet_num;
                    }

                    if(token_buf->aru < local_aru) {
                        if(token_buf->last_change_machine == my_index && local_aru > token_buf->aru) {
                            token_buf->aru = local_aru;
                            token_buf->last_change_machine = my_index;
                        }
                    } else if(token_buf->aru > local_aru) {
                        token_buf->aru = local_aru;
                        token_buf->last_change_machine = my_index;
                    } 

                    send_addr.sin_family = AF_INET;
                    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
                    send_addr.sin_port = htons(PORT);

                    /* Sending out rtr if have */
                    int k = 0;
                    while(k < WINDOW_LEN && token_buf->rtr[k] != -2) {
                        if(token_buf->rtr[k] == -1) {
                            k++;
                            continue;
                        }
                        int index = token_buf->rtr[k] % WINDOW_LEN;
                        if(window_buf[index]->packet_index == token_buf->rtr[k]) {
                            sendto(ss, window_buf[index], sizeof(packet), 0,
                                (struct sockaddr *)&send_addr, sizeof(send_addr) );
                            token_buf->rtr[k] = -1;
                        }
                        k++;
                    }

                    /* Adding self rtrs to token */
                    for(int i = local_aru + 1; i <= token_buf->seq; i++) {
                        int k = 0;
                        if(window_buf[i % WINDOW_LEN]->packet_index != i) {
                            while(token_buf->rtr[k] != -1 && token_buf->rtr[k] != -2) {
                                if(token_buf->rtr[k] == i) {
                                    break;
                                }
                                k++;
                            }
                            token_buf->rtr[k] = i;
                        }
                    }

                    for(int i = 0; i < MESS_PER_ROUND && packet_num > 0; i++) {
                        if(token_buf->seq < deliver_seq + WINDOW_LEN - 1) {
                            if(token_buf->seq == token_buf->aru && token_buf->aru == local_aru) {
                                local_aru++;
                                token_buf->aru++;
                            } else if(token_buf->seq == local_aru) {
                                local_aru++;
                            }
                            int packet_index = ++token_buf->seq;
                            window_buf[packet_index % WINDOW_LEN]->packet_index = packet_index;
                            window_buf[packet_index % WINDOW_LEN]->random_num = rand() % 1000000 + 1;
                            window_buf[packet_index % WINDOW_LEN]->machine_index = my_index;
                            local_seq = token_buf->seq;
                            sendto(ss, window_buf[packet_index % WINDOW_LEN], sizeof(packet), 0,
                                (struct sockaddr *)&send_addr, sizeof(send_addr));
                            if(token_buf->max_remain_pack == packet_num)
                                token_buf->max_remain_pack--;
                            packet_num--;
                        } else {
                            break;
                        }
                    }

                    /* deliver packets */
                    int deliver_pending;
                    deliver_pending = (last_token_aru > before_change_aru) ? before_change_aru : last_token_aru;
                    
                    for(int i = deliver_seq; i <= deliver_pending; i++) {
                        packet *temp = window_buf[i % WINDOW_LEN];
                        fprintf(fw, "%2d, %8d, %8d\n", temp->machine_index, temp->packet_index,  temp->random_num);
                    }

                    if(deliver_pending >= deliver_seq) {
                        deliver_seq = deliver_pending + 1;
                    }  

                    send_addr.sin_family = AF_INET;
                    send_addr.sin_addr.s_addr = host_num;  /* unicast address */
                    send_addr.sin_port = htons(PORT);

                    last_rand = token_buf->random;
                    token_buf->random = rand() % 1000000 + 1;

                    sendto(ss, token_buf, sizeof(token), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                    last_token_aru = before_change_aru;

                } else if(temp_buf->type == TERM_TYPE) {
                    /* After receiveing a TERM_TYPE token, multicast this token as well */
                    send_addr.sin_family = AF_INET;
                    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
                    send_addr.sin_port = htons(PORT);

                    token_buf->type = TERM_TYPE;

                    sendto(ss, token_buf, sizeof(token), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                    for(int i = deliver_seq; i <= local_aru; i++) {
                        packet *temp = window_buf[i % WINDOW_LEN];
                        fprintf(fw, "%2d, %8d, %8d\n", temp->machine_index, temp->packet_index,  temp->random_num);
                    }
                    fclose(fw);
                    free(temp_buf);
                    free(token_buf);
                    for(int j = 0; j < WINDOW_LEN; j++) {
                        free(window_buf[j]);
                    }
                    printf("Machine %d finished transfer!\n", my_index);

                    break;
                }
            }
        } else {
            /* If transmission start and timeout, everymachine unicasts token to its next neighbor, Duplicate token can be judged 
            by the receiver */
            if(transmit == 1) {
                send_addr.sin_family = AF_INET;
                send_addr.sin_addr.s_addr = host_num;  /* unicast address */
                send_addr.sin_port = htons(PORT);
                sendto(ss, token_buf, sizeof(token), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
            }
        }
    }

    gettimeofday(&end_time, NULL);
    double diff = diffTime(start_time, end_time);
    printf("Transfer time: %fs\n", diff);

    return 0;
}

void start(int sr, int my_index, fd_set mask, fd_set dummy_mask) {
    for(;;)
    {
        char mess_buf[NAME_LEN];
        fd_set t_mask = mask;
        int n = select( FD_SETSIZE, &t_mask, &dummy_mask, &dummy_mask, NULL);
        if (n > 0) {
            if ( FD_ISSET( sr, &t_mask) ) {
                int b = recv( sr, mess_buf, sizeof(mess_buf), 0 );
                mess_buf[b] = 0;
                printf( "Machine %d %s\n", my_index, mess_buf );
                break;
            }
        }
    }
}

void initRing(int my_index, int machine_num, int loss_rate, int sr, int ss, fd_set mask, fd_set dummy_mask) {

    struct timeval        timeout;
    init                  *initPacket = NULL;
    init                  *receivedInit = NULL;
    int                   num;
    int                   fromNextMachine = 0;
    int                   fromLastMachine = 0;


    gethostname(my_name, NAME_LEN);

    /*build initPacket*/
    initPacket = malloc(sizeof(init));
    if (initPacket == NULL) {
        printf("Mcast: malloc error.\n");
        exit(1);
    }

    initPacket->type = INIT_TYPE;
    initPacket->sender_index = my_index;
    strcpy(initPacket->sender_host, my_name);
    initPacket->receivedNext = 0;
    initPacket->receivedByLast = 0;


    receivedInit = malloc(sizeof(init));
    if (receivedInit == NULL) {
        printf("Mcast: malloc error.\n");
        exit(1);
    }

    fd_set temp_mask = mask;

    for(;;)
    {

        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        temp_mask = mask;

        /*multicast initPacket before recv*/

        sendto( ss, initPacket, sizeof(init), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
        if (num > 0)
        {
            if ( FD_ISSET( sr, &temp_mask) )  
            {
                recv_dbg( sr, (char*)receivedInit, sizeof(init), 0);

                if (receivedInit->type != INIT_TYPE)
                {
                    // printf("macst:Type error %d is not INIT_TYPE.\n", receivedInit->type);
                    continue;
                }

                /* check if it's from next machine*/

                if ( (my_index == machine_num && receivedInit->sender_index == 1) || receivedInit->sender_index == (my_index + 1) )
                {
                    fromNextMachine = 1 ;                    
                }

                /*get the next machine's ip*/

                if (fromNextMachine == 1)
                {
                    strcpy(my_next_machine, receivedInit->sender_host);
                    initPacket->receivedNext = 1;                    
                    p_h_ent = gethostbyname(my_next_machine);
                    if ( p_h_ent == NULL ) {
                        printf("ncp: gethostbyname error.\n");
                        exit(1);
                    }

                    memcpy( &h_ent, p_h_ent, sizeof(h_ent));
                    memcpy( &host_num, h_ent.h_addr_list[0], sizeof(host_num) );

                    fromNextMachine = 0;
                }

                /*check if the initPacket is from the last machine*/

                if ((my_index == 1 && receivedInit->sender_index == machine_num) || receivedInit->sender_index == (my_index -1) )
                {
                    fromLastMachine = 1;
                }

                /*check if the last machine received my hostname*/

                if (fromLastMachine == 1 && receivedInit->receivedNext == 1 && initPacket->receivedByLast == 0)
                {
                    initPacket->receivedByLast = 1;
                }
                if (fromLastMachine == 1 && receivedInit->receivedNext == 0)
                {
                    initPacket->receivedByLast = 0;
                }

                fromLastMachine = 0;

                /*When received the next machine's ip and received by the last machine, then could break. Otherwise, resend the initPacket*/

                if (my_index == 1)
                {
                   if(initPacket->receivedByLast == 1 && initPacket->receivedNext == 1)
                   {                   
                        continue;

                   }else{
                        sleep(0.01);/*To ensure machine 1 is the last one to break*/
                        sendto( ss, initPacket, sizeof(init), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                   }
                }

                if (my_index != 1)
                {
                    if (initPacket->receivedByLast == 1 && initPacket->receivedNext == 1)
                    {
                        sendto( ss, initPacket, sizeof(init), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                        printf("Machine %d initRing finished!\n", my_index);
                        free(initPacket);
                        free(receivedInit);
                        break;
                    }else
                    {                 
                        sendto( ss, initPacket, sizeof(init), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    }   
                }
         
            }
        }else
        {
            if (my_index != 1)
            {
                if (initPacket->receivedNext == 1)
                {
                    sendto( ss, initPacket, sizeof(init), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    printf("Machine %d initRing finished!\n", my_index);
                    free(initPacket);
                    free(receivedInit);
                    break;
                }
            }

            /*To ensure that machine 1 is the last one to break*/

            if (initPacket->receivedNext == 1 && my_index == 1) 
            {
                printf("Machine 1 initRing finished!\n");
                free(initPacket);
                free(receivedInit);
                break;
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
