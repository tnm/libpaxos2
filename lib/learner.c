#include <stdlib.h>
#include <stdio.h>
#include <pthread.h> 
#include <assert.h>
#include <memory.h>

#include "event.h"
#include "evutil.h"

#include "libpaxos.h"
#include "libpaxos_priv.h"
#include "paxos_udp.h"

#define LEARNER_ERROR (-1)
#define LEARNER_READY (0)
#define LEARNER_STARTING (1)
#define INST_INFO_EMPTY (0)

typedef struct learner_instance_info {
    iid_t           iid;
    ballot_t        last_update_ballot;
    accept_ack*     acks[N_OF_ACCEPTORS];
    // int         proposer_id;
    accept_ack*     final_value;
    // int         final_value_size;
} inst_info;

static iid_t highest_iid_delivered = 0;
static iid_t highest_iid_seen = 1;
static iid_t current_iid = 1;
static inst_info learner_state[LEARNER_ARRAY_SIZE];

static custom_init_function custom_init = NULL;
static deliver_function delfun = NULL;

static int learner_ready = LEARNER_STARTING;
static pthread_mutex_t ready_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ready_cond = PTHREAD_COND_INITIALIZER;
static pthread_t learner_thread = NULL;

static struct event_base * eb;
struct event learner_msg_event;

static udp_send_buffer * to_acceptors;
static udp_receiver * for_learner;

/*-------------------------------------------------------------------------*/
// Helpers
/*-------------------------------------------------------------------------*/

void clear_instance_info(inst_info * ii) {
    ii->iid = INST_INFO_EMPTY;
    ii->last_update_ballot = 0;
    ii->final_value = NULL;
    size_t i;
    for(i = 0; i < N_OF_ACCEPTORS; i++) {
        if(ii->acks[i] != NULL) {
            PAX_FREE(ii->acks[i]);
            ii->acks[i] = NULL;            
        }
    }
}

void store_accept_ack(inst_info * ii, short int acceptor_id, accept_ack * aa) {
    accept_ack * new_ack = PAX_MALLOC(ACCEPT_ACK_SIZE(aa));
    memcpy(new_ack, aa, ACCEPT_ACK_SIZE(aa));
    ii->acks[acceptor_id] = new_ack;

    //Keep track of most recent update
    ii->last_update_ballot = aa->ballot;
}

//Returns 0 if the message was discarded because not relevant
//1 if the state changed, so the quorum check is triggered
int update_state(inst_info * ii, short int acceptor_id, accept_ack * aa) {
    //First message for this iid
    if(ii->iid == INST_INFO_EMPTY) {
        LOG(DBG, ("Received first message for instance:%lu\n", aa->iid));
        ii->iid = aa->iid;
        ii->last_update_ballot = aa->ballot;
    }
    assert(ii->iid == aa->iid);
    
    //Closed already, drop
    if(ii->final_value != NULL) {
        LOG(DBG, ("Dropping accept_ack for iid:%lu, already closed\n", aa->iid));
        return 0;
    }
    
    //No previous message to overwrite for this acceptor
    if(ii->acks[acceptor_id] == NULL) {
        LOG(DBG, ("Got first ack for iid:%lu, acceptor:%d\n", \
            ii->iid, acceptor_id));
        //Save this accept_ack
        store_accept_ack(ii, acceptor_id, aa);
        return 1;
    }
    
    //There is already a message from the same acceptor
    accept_ack * prev_ack = ii->acks[acceptor_id];
    //Already more recent info in the record, accept_ack is old
    if(prev_ack->ballot >= aa->ballot) {
        LOG(DBG, ("Dropping accept_ack for iid:%lu, more stored ballot is newer or equal\n", aa->iid));
        return 0;
    }
    
    //Replace the previous ack since the ballot is older
    LOG(DBG, ("Overwriting previous accept_ack for iid:%lu\n", aa->iid));
    PAX_FREE(prev_ack);
    store_accept_ack(ii, acceptor_id, aa);
    return 1;
}

//Returns 0 if the instance is not closed yet, 1 otherwise
int check_quorum(inst_info * ii) {
    size_t i, a_valid_index=-1, count=0;
    accept_ack * curr_ack;
    
    //Iterates over stored acks
    for(i = 0; i < N_OF_ACCEPTORS; i++) {
        curr_ack = ii->acks[i];
        if(curr_ack->ballot == ii->last_update_ballot){
            a_valid_index = i;
            count++;
        }
    }
    
    //Reached a quorum/majority!
    if(count >= QUORUM) {
        LOG(DBG, ("Reached quorum, iid:%lu is closed!\n", ii->iid));
        ii->final_value = ii->acks[a_valid_index];
        return 1;
    }
    
    //No quorum yet...
    return 0;
    
}

void deliver_next_closed_instances() {
    inst_info * ii = GET_LEA_INSTANCE(current_iid + 1);
    accept_ack * aa;
    while(ii->final_value != NULL) {
        aa = ii->final_value;
        
        //Deliver the value trough callback
        delfun(aa->value, aa->value_size, current_iid, aa->ballot, -1);
        current_iid++;
        
        //Clear the state
        clear_instance_info(ii);
        
        //Go on and try to deliver next
        ii = GET_LEA_INSTANCE(current_iid + 1);
    }

}

/*-------------------------------------------------------------------------*/
// Event handlers
/*-------------------------------------------------------------------------*/

void handle_accept_ack(short int acceptor_id, accept_ack * aa) {
    //Keep track of highest seen instance id
    if(aa->iid > highest_iid_seen) {
        highest_iid_seen = aa->iid;
    }
    
    //Already closed and delivered, drop
    if(aa->iid <= highest_iid_delivered) {
        LOG(DBG, ("Dropping accept_ack for already delivered iid:%lu\n", aa->iid));
        return;
    }
    
    //We are late w.r.t the current iid, drop
    if(aa->iid >= highest_iid_delivered + LEARNER_ARRAY_SIZE) {
        LOG(DBG, ("Dropping accept_ack for iid:%lu, too far in future\n", aa->iid));
        return;
    }

    //Message is within interesting bounds
    //Update the corresponding record
    inst_info * ii = GET_LEA_INSTANCE(aa->iid);
    int relevant = update_state(ii, acceptor_id, aa);
    if(!relevant) {
        LOG(DBG, ("Learner discarding learn for iid:%lu\n", aa->iid));
        return;
    }

    //Check if instance can be declared closed
    int closed = check_quorum(ii);
    if(!closed) {
        LOG(DBG, ("Not yet a quorum for iid:%lu\n", aa->iid));
        return;
    }

    //If the closed instance is last delivered + 1
    //Deliver it (and the following if already closed)
    if (aa->iid == highest_iid_delivered+1) {
        deliver_next_closed_instances(aa->iid);
    }
}

//Iterate over the accept_ack inside an accept_ack_batch
void handle_accept_ack_batch(accept_ack_batch* aab) {
    size_t data_offset;
    accept_ack * aa;
    
    data_offset = 0;
    aa = (accept_ack*) &aab->data[data_offset];

    short int i;
    for(i = 0; i < aab->n_of_acks; i++) {
        handle_accept_ack(aab->acceptor_id, aa);
        data_offset += ACCEPT_ACK_SIZE(aa);
        aa = (accept_ack*) &aab->data[data_offset];
    }
    
    assert(data_offset == aab->data_size);
}

static void handle_event_newmsg(int sock, short event, void *arg) {
    //Make the compiler happy!
    UNUSED_ARG(sock);
    UNUSED_ARG(event);
    UNUSED_ARG(arg);
    
    assert(sock == for_learner->sock);
    
    int valid = udp_read_next_message(for_learner);
    
    if (valid < 0) {
        printf("Dropping invalid learner message\n");
        return;
    }
    
    paxos_msg * msg = (paxos_msg*) &for_learner->recv_buffer;

    switch(msg->type) {
        case accept_acks: {
            handle_accept_ack_batch((accept_ack_batch*) msg->data);
        }
        break;

        default: {
            printf("Unknow msg type %d received by learner\n", msg->type);
        }
    }
}

/*-------------------------------------------------------------------------*/
// Initialization
/*-------------------------------------------------------------------------*/
static int learner_structures_init() {
    // Clear the state array
    memset(learner_state, 0, (sizeof(inst_info) * LEARNER_ARRAY_SIZE));
    size_t i;
    for(i = 0; i < N_OF_ACCEPTORS; i++) {
        clear_instance_info(&learner_state[i]);
    }
    return 0;
}

static int learner_network_init() {
    
    // Send buffer for talking to acceptors
    to_acceptors = udp_sendbuf_new(PAXOS_ACCEPTORS_NET);
    if(to_acceptors == NULL) {
        printf("Error creating learner network sender\n");
        return LEARNER_ERROR;
    }
    
    // Message receive event
    for_learner = udp_receiver_new(PAXOS_LEARNERS_NET);
    if (for_learner == NULL) {
        printf("Error creating learner network receiver\n");
        return LEARNER_ERROR;
    }
    event_set(&learner_msg_event, for_learner->sock, EV_READ|EV_PERSIST, handle_event_newmsg, NULL);
    event_add(&learner_msg_event, NULL);
    
    //...
    
    return 0;
}

static void learner_init_failure(char * msg) {
    printf("Learner init error: %s\n", msg);
    pthread_mutex_lock(&ready_lock);
    learner_ready = LEARNER_ERROR;
    pthread_cond_signal(&ready_cond);
    pthread_mutex_unlock(&ready_lock);
}

static void learner_signal_init_complete() {
    LOG(DBG, ("Learner thread setting status to ready\n")); 
    pthread_mutex_lock(&ready_lock);
    learner_ready = LEARNER_READY;
    pthread_cond_signal(&ready_cond);
    pthread_mutex_unlock(&ready_lock);
}

static void* start_learner_thread(void* arg) {
    delfun = (deliver_function) arg;
    if(delfun == NULL) {
        learner_init_failure("Error in libevent init\n");
        printf("Error NULL callback!\n");
        return NULL;
    }
    
    //Initialization of libevent handle
    if((eb = event_init()) == NULL) {
        learner_init_failure("Error in libevent init\n");
        return NULL;
    }
    
    //Normal learner initialization, private structures
    if(learner_structures_init() != 0) {
        learner_init_failure("Error in learner structures initialization\n");
        return NULL;
    }
    
    //Init sockets and send buffer
    if(learner_network_init() != 0) {
        learner_init_failure("Error in learner network init\n");
        return NULL;
    }
    
    //Call custom init (i.e. to register additional events)
    if(custom_init != NULL && custom_init() != 0) {
        learner_init_failure("Error in custom_init_function\n");
        return NULL;
    }
    
    // Signal client, learner is ready
    learner_signal_init_complete();

    // Start thread that calls event_dispatch()
    // and never returns
    LOG(DBG, ("Learner thread ready, starting libevent loop\n"));
    event_dispatch();
    printf("libeven loop terminated\n");
    return NULL;
}

static int learner_wait_ready() {
    int status;
    
    pthread_mutex_lock(&ready_lock);
    
    while(1) {
        pthread_cond_wait(&ready_cond, &ready_lock);
        status = learner_ready;

        if(status == LEARNER_STARTING) {
            //Not ready yet, keep waiting
            continue;            
        }
        
        //Status changed
        if (status != LEARNER_READY && status != LEARNER_ERROR) {
            //Unknow status
            printf("Unknow learner status: %d\n", status);
            status = LEARNER_ERROR;
        }
        break;
    }
    
    pthread_mutex_unlock(&ready_lock);
    return status;
}

int learner_init(deliver_function f, custom_init_function cif) {
    // Start learner (which starts event_dispatch())
    custom_init = cif;
    if (pthread_create(&learner_thread, NULL, start_learner_thread, (void*) f) != 0) {
        perror("pthread create learner thread");
        return -1;
    }
    LOG(DBG, ("Learner thread started, waiting for ready signal\n"));    
    if (learner_wait_ready() == LEARNER_ERROR) {
        printf("Learner initialization failed!\n");
        return -1;
    }
    LOG(VRB, ("Learner is ready\n"));    
    return 0;
}

