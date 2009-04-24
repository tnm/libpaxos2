#ifndef _LIBPAXOS_H_
#define _LIBPAXOS_H_
#include <sys/types.h>
#include "paxos_config.h"



/* 
    TODO comment
    Max packet size minus largest header possible
    (should be accept_ack_batch+accept_ack, around 30 bytes)
*/
#define PAXOS_MAX_VALUE_SIZE (MAX_UDP_MSG_SIZE - 40)
/* 
    TODO comment
*/
typedef unsigned int ballot_t;
typedef long unsigned int iid_t;

// Type for function passed to the learner
// Example void my_deliver_fun(char * value, size_t size, iid_t iid, ballot_t ballot, int proposer)
typedef void (* deliver_function)(char*, size_t, iid_t, ballot_t, int);

// Type for function passed to the learner, 
// called by learner thread after it's normal initialization
// Example int my_custom_init()
typedef int (* custom_init_function)(void);

/*
    Starts a learner and returns when the initialization is complete.
    Return value is 0 if successful
    f -> A deliver_function invoked when a value is delivered.
         This argument cannot be NULL
         It's called by an internal thread therefore:
         i)  it must be quick
         ii) you must synchronize/lock externally if this function touches data
             shared with some other thread (i.e. the one that calls learner init)
    cif -> A custom_init_function invoked by the internal libevent thread, 
           invoked when the normal learner initialization is completed
           Can be used to add other events to the existing event loop.
           It's ok to pass NULL if you don't need it.
           cif has to return -1 for error and 0 for success
*/
int learner_init(deliver_function f, custom_init_function cif);

/*
    Starts an acceptor and returns when the initialization is complete.
    Return value is 0 if successful
    acceptor_id -> Must be in the range [0...(N_OF_ACCEPTORS-1)]
*/
int acceptor_init(int acceptor_id);

/*
    Starts an acceptor that instead of starting clean
    tries to recover from an existing db.
    Return value is 0 if successful
*/
int acceptor_init_recover(int acceptor_id);

//TODO should delegate close to libevent thread
int acceptor_exit();

/*
    TODO comment
    Return value is 0 if successful
*/
int proposer_init(int proposer_id);

/*
    TODO comment
*/
typedef struct paxos_submit_handle_t {
    void * sendbuf;
}paxos_submit_handle;

paxos_submit_handle * pax_submit_handle_init();

/*
    TODO comment
*/
int pax_submit_nonblock(paxos_submit_handle * h, char * value, size_t val_size);

// /*
//     (MTU) - 8 (multicast header) - 32 (biggest paxos header)
// */
// #define MAX_UDP_MSG_SIZE 9000
// #define PAXOS_MAX_VALUE_SIZE 8960

// //Starts a learner and does not return unless an error occours
// int learner_init(deliver_function f);
// 
// //Starts a learner in a thread and returns
// int learner_init_threaded(deliver_function f);
// 
// //Starts a proposer and returns
// int proposer_init(int proposer_id, int is_leader);
// 
// //Starts a proposer which also delivers values from it's internal learner.
// //IMPORTANT: 
// // This function starts the libevent loop in a new thread. The function F is invoked by this thread.
// // Therefore if the callback F accessess data shared with other threads (i.e. the one that calls proposer_submit()), F must be made thread-safe!!!
// // At the moment it is thread-safe only if the other thread is blocked-in or trying to call proposer_submit.
// // F must be as fast as possible since the proposer is blocked in the meanwhile.
// // The contents of the value buffer passed as first argument to F should not be modified!
// int proposer_init_and_deliver(int prop_id, int is_leader, deliver_function f);
// 
// //Submit a value paxos, returns when the value is delivered
// void proposer_submit_value(char * value, size_t val_size);
// 
// //Starts an acceptor and does not return unless an error occours
// int acceptor_start(int acceptor_id);
// 
// //// TO DO - Not Implemented! ///
// 
// int proposer_queue_size();
// 
// void proposer_print_event_counters();
// 
// 
// int learner_get_next_value(char** valuep);
// int learner_get_next_value_nonblock(char** valuep);
// 
// void learner_print_event_counters();
// 
// 
#endif /* _LIBPAXOS_H_ */
