#ifndef PAXOS_UDP_H_X98E254H
#define PAXOS_UDP_H_X98E254H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

typedef struct udp_send_buffer_t {
    int sock;
    struct sockaddr_in addr;    
    char buffer[1];    
} udp_send_buffer;

typedef struct udp_receiver_t {
    int sock;
    struct sockaddr_in addr;
    char recv_buffer[1];
} udp_receiver;


udp_send_buffer * udp_sendbuf_new(char* address_string, int port);

udp_receiver * udp_receiver_new(char* address_string, int port);

int udp_read_next_message(udp_receiver * recv_info);


#endif /* end of include guard: PAXOS_UDP_H_X98E254H */
