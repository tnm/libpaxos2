#include <stdlib.h>
#include <memory.h>
#include <stdio.h>

#include "libpaxos_priv.h"
#include "paxos_udp.h"
#include "config.h"

static int validate_paxos_msg() {
    // paxos_msg * pmsg = (paxos_msg*) lear_recv_buffer;
    // if (pmsg->size != (msg_size - sizeof(paxos_msg))) {
    //     printf("Invalid paxos packet, size %d does not match packet size %lu\n", pmsg->size, (long unsigned)(msg_size - sizeof(paxos_msg)));
    // }
    return -1;
}

udp_receiver * udp_receiver_new(char* address_string, int port) {
    udp_receiver * rec = PAX_MALLOC(sizeof(udp_receiver));

    // struct sockaddr_in * addr;
    struct ip_mreq mreq;
    // int sock;
    
    /*Clear structures*/
    memset(&mreq, '\0', sizeof(struct ip_mreq));
    /* Set up socket */
    rec->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (rec->sock < 0) {
        perror("receiver socket");
        return NULL;
    }

    /* Set to reuse address */	
    int activate = 1;
    if (setsockopt(rec->sock, SOL_SOCKET, SO_REUSEADDR, &activate, sizeof(int)) != 0) {
        perror("setsockopt, setting SO_REUSEADDR");
        return NULL;
    }

    /* Set up membership to group */
    mreq.imr_multiaddr.s_addr = inet_addr(address_string);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(rec->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        perror("setsockopt, setting IP_ADD_MEMBERSHIP");
        return NULL;
    }

    /* Set up address */
    struct sockaddr_in * addr_p = &rec->addr;
    addr_p->sin_addr.s_addr = inet_addr(address_string);
    if (addr_p->sin_addr.s_addr == INADDR_NONE) {
        printf("Error setting receiver->addr\n");
        return NULL;
    }
    addr_p->sin_family = AF_INET;
    addr_p->sin_port = htons((uint16_t)port);   

    /* Bind the socket */
    if (bind(rec->sock, (struct sockaddr *) &rec->addr, sizeof(struct sockaddr_in)) != 0) {
        perror("bind");
        return NULL;
    }

    /* Set non-blocking */
    int flag = fcntl(rec->sock, F_GETFL);
    if(flag < 0) {
        perror("fcntl1");
        return NULL;
    }

    flag |= O_NONBLOCK;
    if(fcntl(rec->sock, F_SETFL, flag) < 0) {
        perror("fcntl2");
        return NULL;
    }

    return rec;
}


int udp_read_next_message(udp_receiver * recv_info) {
    socklen_t addrlen = sizeof(struct sockaddr);
    int msg_size = recvfrom(recv_info->sock, recv_info->recv_buffer, MAX_UDP_MSG_SIZE, MSG_WAITALL, (struct sockaddr *)&recv_info->addr, &addrlen);

    if (msg_size < 0) {
        perror("recvfrom");
        return -1;
    }
    return validate_paxos_msg(recv_info->recv_buffer, msg_size);
}
