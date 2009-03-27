#include <stdio.h>
#include <assert.h>

#include "event.h"
#include "evutil.h"

#include "libpaxos.h"
#include "libpaxos_priv.h"
#include "paxos_udp.h"
#include "acceptor_stable_storage.h"

#define ACCEPTOR_ERROR (-1)

int this_acceptor_id = -1;

static udp_send_buffer * to_proposers;
static udp_send_buffer * to_learners;
static udp_receiver * for_acceptor;
static struct event acceptor_msg_event;
static struct event repeat_accept_event;
static struct timeval periodic_repeat_interval;

static iid_t highest_accepted_iid = 0;

/*-------------------------------------------------------------------------*/
// Helpers
/*-------------------------------------------------------------------------*/
static void
acc_retransmit_latest_accept() {
//TODO        
}
    
static void
acc_periodic_repeater(int fd, short event, void *arg)
{
    UNUSED_ARG(fd);
    UNUSED_ARG(event);
    UNUSED_ARG(arg);
    
    //If some value has been accepted,
    //Rebroadcast most recent (so that learners stay up-to-date)
    if (highest_accepted_iid > 0) {
        LOG(DBG, ("re-seding most recent accept, iid:%lu", highest_accepted_iid));
        acc_retransmit_latest_accept();
    }
    
    //Set the next timeout for calling this function
    if(event_add(&repeat_accept_event, &periodic_repeat_interval) != 0) {
	   printf("Error while adding next repeater periodic event\n");
	}
}

/*-------------------------------------------------------------------------*/
// Event handlers
/*-------------------------------------------------------------------------*/

void handle_prepare_req_batch(prepare_req_batch* prb) {
//TODO
}
void handle_accept_req_batch(accept_req_batch* pr) {
//TODO            
}


static void acc_handle_newmsg(int sock, short event, void *arg) {
    //Make the compiler happy!
    UNUSED_ARG(sock);
    UNUSED_ARG(event);
    UNUSED_ARG(arg);
    printf("handle acceptor event!\n");
    
    assert(sock == for_acceptor->sock);
    
    int valid = udp_read_next_message(for_acceptor);
    
    if (valid < 0) {
        printf("Dropping invalid acceptor message\n");
        return;
    }
    
    paxos_msg * msg = (paxos_msg*) &for_acceptor->recv_buffer;

    switch(msg->type) {
        case prepare_reqs: {
            handle_prepare_req_batch((prepare_req_batch*) msg->data);
        }
        break;

        case accept_reqs: {
            handle_accept_req_batch((accept_req_batch*) msg->data);
        }
        break;


        default: {
            printf("Unknow msg type %d received by acceptor\n", msg->type);
        }
    }
}

void acc_deliver_callback(char * value, size_t size, int iid, int ballot, int proposer) {
    UNUSED_ARG(value);
    UNUSED_ARG(size);
    UNUSED_ARG(iid);
    UNUSED_ARG(ballot);
    UNUSED_ARG(proposer);
    #ifndef ACCEPTOR_UPDATE_ON_DELIVER
    //ACCEPTOR_UPDATE_ON_DELIVER not defined, 
    //this callback should not even be called!
    printf("Warning:%s invoked when ACCEPTOR_UPDATE_ON_DELIVER is undefined!\n", __func__);
    return;

    #else
    printf("Acceptor update not implemented yet!\n");
    //TODO
    #endif
}

/*-------------------------------------------------------------------------*/
// Initialization
/*-------------------------------------------------------------------------*/

static int init_acc_network() {
    
    // Send buffer for talking to proposers
    to_proposers = udp_sendbuf_new(PAXOS_PROPOSERS_NET);
    if(to_proposers == NULL) {
        printf("Error creating acceptor->proposers network sender\n");
        return ACCEPTOR_ERROR;
    }

    // Send buffer for talking to learners
    to_learners = udp_sendbuf_new(PAXOS_LEARNERS_NET);
    if(to_learners == NULL) {
        printf("Error creating acceptor->learners network sender\n");
        return ACCEPTOR_ERROR;
    }
    
    // Message receive event
    for_acceptor = udp_receiver_new(PAXOS_ACCEPTORS_NET);
    if (for_acceptor == NULL) {
        printf("Error creating acceptor network receiver\n");
        return ACCEPTOR_ERROR;
    }
    event_set(&acceptor_msg_event, for_acceptor->sock, EV_READ|EV_PERSIST, acc_handle_newmsg, NULL);
    event_add(&acceptor_msg_event, NULL);
    
    return 0;
}

static int 
init_acc_timers() {
    evtimer_set(&repeat_accept_event, acc_periodic_repeater, NULL);
	evutil_timerclear(&periodic_repeat_interval);
	periodic_repeat_interval.tv_sec = ACCEPTOR_REPEAT_INTERVAL;
    periodic_repeat_interval.tv_usec = 0;
	if(event_add(&repeat_accept_event, &periodic_repeat_interval) != 0) {
	   printf("Error while adding first periodic repeater event\n");
       return -1;
	}
    
    return 0;
}

static int
init_acc_stable_storage() {
    return stablestorage_init(this_acceptor_id);
}

static int init_acceptor() {
    //This is invoked by the learner thread
    //after it's normal initialization

#ifdef ACCEPTOR_UPDATE_ON_DELIVER
    //Keep the learnern running as normal
    //Will deliver values when decided
    LOG(VRB, ("Acceptor will update stored values as they are delivered\n"));
#else
    //Shut down the underlying learner,
    //(but keep using it's event loop)
    LOG(VRB, ("Acceptor shutting down learner events\n"));
    learner_suspend();
#endif
    
    //Add network events and prepare send buffer
    if(init_acc_network() != 0) {
        printf("Acceptor network init failed\n");
        return -1;
    }

    //Add additional timers to libevent loop
    if(init_acc_timers() != 0){
        printf("Acceptor timers init failed\n");
        return -1;
    }
    
    //Initialize BDB 
    if(init_acc_stable_storage() != 0) {
        printf("Acceptor stable storage init failed\n");
        return -1;
    }
    return 0;
}

int acceptor_init(int acceptor_id) {
    
    //Check id validity
    if(acceptor_id < 0 || acceptor_id >= N_OF_ACCEPTORS) {
        printf("Invalid acceptor id:%d\n", acceptor_id);
        return -1;
    }    
    this_acceptor_id = acceptor_id;
    LOG(VRB, ("Acceptor %d starting...\n", this_acceptor_id));
    
    //Starts a learner with a custom init function
    //Learner's functionalities can be shut down
    if (learner_init(acc_deliver_callback, init_acceptor) != 0) {
        printf("Could not start the learner!\n");
        return -1;
    }
    
    LOG(VRB, ("Acceptor is ready\n"));
    return 0;
}

int acceptor_init_recover(int acceptor_id) {
    //Set recovery mode then start normally
    stablestorage_do_recovery();
    return acceptor_init(acceptor_id);
}

int acceptor_exit() {
    if (stablestorage_shutdown() != 0) {
        printf("stablestorage shutdown failed!\n");
    }
    return 0;
}