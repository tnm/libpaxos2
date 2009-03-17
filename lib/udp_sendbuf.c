#include <stdlib.h>
#include <memory.h>
#include <stdio.h>



#include "libpaxos_priv.h"
#include "paxos_udp.h"

udp_send_buffer * udp_sendbuf_new(char* address_string, int port) {
    udp_send_buffer * sb  = PAX_MALLOC(sizeof(udp_send_buffer));

    // int sock;
    // int addrlen;
    struct sockaddr_in * addr_p = &sb->addr;
        
    /* Set up socket */
    sb->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sb->sock < 0) {
        perror("sender socket");
        return NULL;
    }
    
    /* Set up address */
    memset(addr_p, '\0', sizeof(struct sockaddr_in));
    addr_p->sin_addr.s_addr = inet_addr(address_string);
    if (addr_p->sin_addr.s_addr == INADDR_NONE) {
        printf("Error setting addr\n");
        return NULL;
    }
    addr_p->sin_family = AF_INET;
    addr_p->sin_port = htons((uint16_t)port);	
    // addrlen = sizeof(struct sockaddr_in);
    
    /* Set non-blocking */
    int flag = fcntl(sb->sock, F_GETFL);
    if(flag < 0) {
        perror("fcntl1");
        return NULL;
    }
    
    flag |= O_NONBLOCK;
    if(fcntl(sb->sock, F_SETFL, flag) < 0) {
        perror("fcntl2");
        return NULL;
    }

    return sb;
}
    
