#include <stdlib.h>
#include <stdio.h>
#include <pthread.h> 

#include "event.h"
#include "evutil.h"

#include "libpaxos.h"
#include "libpaxos_priv.h"
#include "paxos_udp.h"

#define LEARNER_ERROR (-1)
#define LEARNER_READY (0)
#define LEARNER_STARTING (1)

static custom_init_function custom_init = NULL;
static deliver_function delfun = NULL;

static int learner_ready = 0;
static pthread_mutex_t ready_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ready_cond = PTHREAD_COND_INITIALIZER;
static pthread_t learner_thread = NULL;

static struct event_base * eb;
struct event learner_msg_event;

static udp_send_buffer * to_acceptors;
static udp_receiver * for_learner;


/*-------------------------------------------------------------------------*/
// Event handlers
/*-------------------------------------------------------------------------*/

static void handle_learner_msg(int sock, short event, void *arg) {
    //Make the compiler happy!
    USELESS_PARAMETER(sock);
    USELESS_PARAMETER(event);
    USELESS_PARAMETER(arg);
    
    int valid = udp_read_next_message(for_learner);
    
    if (valid < 0) {
        printf("Error while reading message in learner socket\n");
        return;
    }
    
    paxos_msg * msg = (paxos_msg*) &for_learner->recv_buffer;

    switch(msg->type) {
        // case learn: {
        //     learner_handle_learn_msg((learn_msg*) msg->data);
        // }
        // break;

        default: {
            printf("Invalid packet type %d received from learners network\n", msg->type);
        }
    }
    
}

/*-------------------------------------------------------------------------*/
// Initialization
/*-------------------------------------------------------------------------*/
static int learner_structures_init() {

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
    event_set(&learner_msg_event, for_learner->sock, EV_READ|EV_PERSIST, handle_learner_msg, NULL);
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
    learner_ready = LEARNER_STARTING;
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

