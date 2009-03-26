/*
    Paxos message types
*/

typedef enum pax_msg_code_e            /* Defines an enumeration type    */
{
    // promise = 1,
    // prepare = 2,
    // learn = 3,
    prepare_reqs,
    prepare_acks,
    accept_reqs,
    accept_acks 
    // ...
} paxos_msg_code;

typedef struct paxos_msg_t {
    size_t data_size; //Size of 'data' in bytes
    paxos_msg_code type;
    char data[0];
} paxos_msg;


/* 
    Paxos messages 
*/

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

typedef struct accept_req_batch_t {
    
} accept_req_batch;

typedef struct prepare_req_batch_t {
    
} prepare_req_batch;

