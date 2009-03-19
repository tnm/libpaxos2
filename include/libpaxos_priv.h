#ifndef _LIBPAXOS_PRIV_H_
#define _LIBPAXOS_PRIV_H_
// #include <netinet/in.h>
// #include <arpa/inet.h>
// 
#include "libpaxos.h"
#include "config.h"
// 
/*
    Paxos message types
*/

typedef enum pax_msg_code_e            /* Defines an enumeration type    */
{
    // promise = 1,
    // prepare = 2,
    // learn = 3,
    accept_acks 
    // ...
} paxos_msg_code;

// #define PAXOS_PREPARE   1
// #define PAXOS_ACCEPT    2
// #define PAXOS_LSYNC     3
// #define PAXOS_PROMISE   4
// #define PAXOS_LEARN     5
// #define PAXOS_ANYVAL    6
// 
// 
// /* 
//     Paxos messages 
// */
typedef struct paxos_msg_t {
    size_t data_size; //Size of 'data' in bytes
    paxos_msg_code type;
    char data[0];
} paxos_msg;

typedef unsigned int ballot_t;
typedef long unsigned int iid_t;

typedef struct accept_ack_t {
    iid_t       iid;
    ballot_t    ballot;
    size_t      value_size;
    char        value[0];
} accept_ack;
#define ACCEPT_ACK_SIZE(M) (M->value_size + sizeof(accept_ack))

typedef struct accept_ack_batch_t {
    short int   acceptor_id;
    short int   n_of_acks;
    size_t      data_size;
    char        data[0];
} accept_ack_batch;
#define ACCEPT_ACK_BATCH_SIZE(B) (B->data_size + sizeof(accept_ack_batch))

// #define PAXOS_MSG_SIZE(m) (sizeof(paxos_msg) + m->size)
// 
// typedef struct prepare_msg_t {
//     int iid;
//     int ballot;
// } prepare_msg;
// 
// typedef struct prepare_batch_msg_t {
//     int     count;
//     char    data[0];
// } prepare_batch_msg;
// 
// typedef struct promise_msg_t {
//     int     iid;
//     int     ballot;
//     int     value_ballot;
//     int     value_size;
//     char    value[0];
// } promise_msg;
// 
// typedef struct promise_batch_msg_t {
//     int     acceptor_id;
//     int     count;
//     char    data[0];
// } promise_batch_msg;
// 
// typedef struct accept_msg_t {
//     int     iid;
//     int     ballot;
//     int     value_size;
//     int     proposer_id;//
//     char    value[0];
// } accept_msg;
// 
// typedef struct learn_msg_t {
//     int     acceptor_id;
//     int     iid;
//     int     ballot;
//     int     proposer_id;//
//     int     value_size;
//     char    value[0];
// } learn_msg;


// 
// typedef struct learner_sync_msg_t {
//     int     count;
//     int     ids[0];
// } learner_sync_msg;
// 
// typedef struct anyval_msg_t {
//     int     ballot;
//     int     count;
//     int     ids[0];
// } anyval_msg;
// 
// int leader_init(int prop_id, int send_socket, struct sockaddr_in * acc_addr);
// int proposer_is_leader();
// void leader_handle_proposer_msg(int fd, short event, void *arg);
// void leader_deliver_value(char * value, size_t size, int iid, int ballot, int proposer);
// 
// /* 
//    This is equivalent to n mod ACCEPTOR_ARRAY_SIZE, 
//    works only if ACCEPTOR_ARRAY_SIZE is a power of 2.
// */
// #define GET_ACC_INDEX(n) (n & (ACCEPTOR_ARRAY_SIZE-1))
// 
/* 
   This is equivalent to n mod LEARNER_ARRAY_SIZE, 
   works only if LEARNER_ARRAY_SIZE is a power of 2.
*/
#define GET_LEA_INDEX(n) (n & (LEARNER_ARRAY_SIZE-1))
#define GET_LEA_INSTANCE(I) &learner_state[((I) & (LEARNER_ARRAY_SIZE-1))]

// 
// 
// /* 
//    This is equivalent to n mod PROPOSER_ARRAY_SIZE, 
//    works only if PROPOSER_ARRAY_SIZE is a power of 2.
// */
// #define GET_PRO_INDEX(n) (n & (PROPOSER_ARRAY_SIZE-1))
// 
//

/*** MALLOC DEBUGGING MACROs ***/

#ifdef PAXOS_DEBUG_MALLOC
void * paxos_debug_malloc(size_t size, char* file, int line);
void paxos_debug_free(void* p, char* file, int line);
#define PAX_MALLOC(x) paxos_debug_malloc(x, __FILE__, __LINE__)
#define PAX_FREE(x) paxos_debug_free(x, __FILE__, __LINE__)
#else
void* paxos_normal_malloc(size_t size);
#define PAX_MALLOC(x) paxos_normal_malloc(x);
#define PAX_FREE(x) free(x)
#endif

/*** LOGGING MACROS ***/

#define VRB 1
#define DBG 3

#define LOG(L, S) if(VERBOSITY_LEVEL >= L) {\
   printf("[%s] ", __func__) ;\
   printf S ;\
}


/*** MISC MACROS ***/

// Since this lib compiles with -Wextra, unused parameters throw
// a warning, adding a USELESS_PARAMETER(variable_name) removes the message
#define UNUSED_ARG(V) (V = V);


#endif /* _LIBPAXOS_PRIV_H_ */
// 
