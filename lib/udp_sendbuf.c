#include <stdlib.h>
#include <memory.h>
#include <stdio.h>

#include "libpaxos_priv.h"
#include "paxos_udp.h"
#include "acceptor_stable_storage.h"

void sendbuf_clear(udp_send_buffer * sb, paxos_msg_code type) {

    sb->dirty = 0;

    paxos_msg * m = (paxos_msg *) &sb->buffer;
    m->type = type;
    m->data_size = sizeof(paxos_msg);
    
    switch(type) {

        //Acceptor
        case prepare_acks: {
            m->data_size += sizeof(prepare_ack_batch);
            prepare_ack_batch * pab = (prepare_ack_batch *)&m->data;
            pab->count = 0;    
        } break;
        
        case accept_acks: {
            m->data_size += sizeof(accept_ack_batch);
            accept_ack_batch * aab = (accept_ack_batch *)&m->data;
            aab->count = 0;
        } break;
        
        //Learner
        case repeat_reqs: {
            m->data_size += sizeof(repeat_req_batch);
            repeat_req_batch * rrb = (repeat_req_batch *)&m->data;
            rrb->count = 0;    
        } break;
            
        default: {
            
            printf("Invalid message type %d for sendbuf_clear!\n", 
                type);
            
        }
    }
}

void sendbuf_add_prepare_ack(udp_send_buffer * sb, acceptor_record * rec) {
    paxos_msg * m = (paxos_msg *) &sb->buffer;
    prepare_ack_batch * pab = (prepare_ack_batch *)&m->data;
    
    size_t pa_size = (sizeof(prepare_ack) + rec->value_size);

    if(PAXOS_MSG_SIZE(m) + pa_size >= MAX_UDP_MSG_SIZE) {
        // Next propose_ack to add does not fit, flush the current 
        // message before adding it
        stablestorage_tx_end();
        sendbuf_flush(sb);
        sendbuf_clear(sb, prepare_acks);
        stablestorage_tx_begin();
    }
    
    prepare_ack * pa = (prepare_ack *)&pab->data[m->data_size];
    pa->ballot = rec->ballot;
    pa->value_ballot = rec->value_ballot;
    pa->value_size = rec->value_size;
    
    //If there's no value this copies 0 bytes!
    memcpy(pa->value, rec->value, rec->value_size);
    
    sb->dirty = 1;
    m->data_size += pa_size;
    pab->count += 1;
}


void sendbuf_add_accept_ack(udp_send_buffer * sb, acceptor_record * rec) {
    paxos_msg * m = (paxos_msg *) &sb->buffer;
    accept_ack_batch * aab = (accept_ack_batch *)&m->data;
    
    size_t aa_size = ACCEPT_ACK_SIZE(rec);
    
    if(PAXOS_MSG_SIZE(m) + aa_size >= MAX_UDP_MSG_SIZE) {
        // Next accept to add does not fit, flush the current 
        // message before adding it
        stablestorage_tx_end();
        sendbuf_flush(sb);
        sendbuf_clear(sb, accept_acks);
        stablestorage_tx_begin();
    }
    

    accept_ack * aa = (accept_ack *)&aab->data[m->data_size];
    memcpy(aa, rec, aa_size);
    
    sb->dirty = 1;
    m->data_size += aa_size;
    aab->count += 1;
    
}

void sendbuf_add_repeat_req(udp_send_buffer * sb, iid_t iid) {
    paxos_msg * m = (paxos_msg *) &sb->buffer;

    if(PAXOS_MSG_SIZE(m) + sizeof(iid_t) >= MAX_UDP_MSG_SIZE) {
        // Next iid to add does not fit, flush the current 
        // message before adding it
        sendbuf_flush(sb);
        sendbuf_clear(sb, prepare_reqs);
    }
    
    sb->dirty = 1;
    m->data_size += sizeof(iid_t);
    
    repeat_req_batch * rrb = (repeat_req_batch *)&m->data;
    rrb->requests[rrb->count] = iid;
    rrb->count += 1;
}

void sendbuf_flush(udp_send_buffer * sb) {
    int cnt;
    
    //The dirty field is used to determine if something 
    // is in the buffer waiting to be sent
    if(!sb->dirty) {
        return;
    }
    
    //Send the current message in buffer
    paxos_msg * m = (paxos_msg *) &sb->buffer;
    cnt = sendto(sb->sock,              //Sock
        sb->buffer,                     //Data
        PAXOS_MSG_SIZE(m),              //Data size
        0,                              //Flags
        (struct sockaddr *)&sb->addr,   //Addr
        sizeof(struct sockaddr_in));    //Addr size
        
    if (cnt != (int)PAXOS_MSG_SIZE(m) || cnt == -1) {
        perror("failed to send message");
    }
}

udp_send_buffer * udp_sendbuf_new(char* address_string, int port) {
    udp_send_buffer * sb  = PAX_MALLOC(sizeof(udp_send_buffer));
    memset(sb, 0, sizeof(udp_send_buffer));

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
    
    LOG(DBG, ("Socket %d created for address %s:%d (send mode)\n", sb->sock, address_string, port));


    return sb;
}
    
