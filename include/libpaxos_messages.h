/*
    Paxos message types
*/

typedef enum pax_msg_code_e {
    prepare_reqs,   //Phase 1a, P->A
    prepare_acks,   //Phase 1b, A->P
    accept_reqs,    //Phase 2a, P->A
    accept_acks,    //Phase 2b, A->L
    repeat_reqs     //For progress, L -> A
} paxos_msg_code;

typedef struct paxos_msg_t {
    size_t data_size; //Size of 'data' in bytes
    paxos_msg_code type;
    char data[0];
} paxos_msg;
#define PAXOS_MSG_SIZE(M) (M->data_size + sizeof(paxos_msg))

/* 
    Paxos messages 
*/

typedef struct accept_ack_t {
    iid_t       iid;
    ballot_t    ballot;
    short int   is_final;
    size_t      value_size;
    char        value[0];
} accept_ack;
#define ACCEPT_ACK_SIZE(M) (M->value_size + sizeof(accept_ack))

typedef struct accept_ack_batch_t {
    short int   acceptor_id;
    short int   n_of_acks;
    char        data[0];
} accept_ack_batch;
// #define ACCEPT_ACK_BATCH_SIZE(B) (B->data_size + sizeof(accept_ack_batch))

typedef struct accept_req_batch_t {
    
} accept_req_batch;

typedef struct prepare_req_batch_t {
    
} prepare_req_batch;

typedef struct repeat_req_batch_t {
    short int count;
    iid_t requests[0];
} repeat_req_batch;
#define REPEAT_REQ_BATCH_SIZE(B) (sizeof(repeat_req_batch) + (sizeof(iid_t) * B->count))
