#include "net_include.h"

int main() {
    struct sockaddr_in send_addr;
    unsigned char      ttl_val;
    int                s;
    char               mess_buf[10] = "start!";

    s = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (s<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, 
        sizeof(ttl_val)) < 0) 
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    sendto( s, mess_buf, strlen(mess_buf), 0, 
            (struct sockaddr *)&send_addr, sizeof(send_addr) );

    return 0;
}
