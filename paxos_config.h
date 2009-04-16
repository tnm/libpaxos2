#ifndef _CONFIG_H_
#define _CONFIG_H_

/*** PROTOCOL SETTINGS ***/

/* 
    TODO comment
*/
#define PROPOSER_PREEXEC_WIN_SIZE 50

/* 
    TODO comment
    Unit is microseconds - i.e. 1500000 = 1.5 secs
*/
#define P1_TIMEOUT_INTERVAL 3000000

/* 
    TODO comment
    Unit is microseconds - i.e. 1500000 = 1.5 secs
*/
#define P2_TIMEOUT_INTERVAL 1500000

/* 
    TODO comment
*/
#define N_OF_ACCEPTORS  3

/* 
    TODO comment
*/
#define N_OF_PROPOSERS  3

/* 
    TODO comment
    Must be a power of 10
*/
#define MAX_N_OF_PROPOSERS  10
/* 
    If ACCEPTOR_UPDATE_ON_DELIVER is defined, the acceptor is also a learner.
    Delivered values are written in stable storage (overwriting the status
    for this acceptor if any).
    This makes recovery and catch-up easier in some situations.
    Undefine to disable.
*/
// #define ACCEPTOR_UPDATE_ON_DELIVER


/*
    Periodically (every ACCEPTOR_REPEAT_INTERVAL seconds) the acceptor
    repeats the latest (by instance id) accept received. 
    This is useful to keep the learners updated in very low-traffic 
    situations.
*/
#define ACCEPTOR_REPEAT_INTERVAL 5

/* 
    Rule for calculating whether the number of accept_ack messages
    (phase 2b of the protocol) is sufficient to declare the instance
    closed and deliver the corresponding value.
    Paxos     -> ((int)(N_OF_ACCEPTORS/2))+1;
    FastPaxos -> 1 + (int)((double)(N_OF_ACCEPTORS*2)/3);
*/
#define QUORUM (((int)(N_OF_ACCEPTORS/2))+1)

/*
    Periodically (as specified by the two variables below)
    the learner will check for "holes": instance i is closed but
    it cannot be delivered since i-1 or i-2 was not closed yet.
    If some hole is detected, will ask the acceptors to repeat
    their decision for the not-yet-closed instances.
*/
#define LEARNER_HOLECHECK_INTERVAL_S 3
#define LEARNER_HOLECHECK_INTERVAL_US 0

/*** ACCEPTORS DB SETTINGS ***/

/*
    Setting for how 'strict' the durability of acceptors should be.
    DB_LOG_INMEMORY: Transactional Data Store (write, in-memory logging)
    DB_TXN_NOSYNC: Transactional Data Store (write, no-sync on commit)
    DB_TXN_WRITE_NOSYNC: Transactional Data Store (write, write-no-sync on commit)
    0: Transactional Data Store (write, sync on commit)
*/
#define BDB_TX_MODE 0

/*
    Path for creating acceptors' BDB file
    Must fit in 512 chars
    %d is replaced by 'acceptor_id'
*/
#define ACCEPTOR_DB_PATH "/tmp/acceptor_%d", acceptor_id
#define ACCEPTOR_DB_FNAME "acc_db_%d.bdb", acceptor_id

/*
    Access method for acceptors' DB
    DB_BTREE and DB_RECNO only, other methods
    requires additional configuration and do not fit well
*/
// #define ACCEPTOR_ACCESS_METHOD DB_BTREE
#define ACCEPTOR_ACCESS_METHOD DB_RECNO


/*** NETWORK SETTINGS ***/

/* 
  Set this less than the UDP MTU in your network
  If not set properly, message send will fail
*/
#define MAX_UDP_MSG_SIZE 6700

/* 
  Multicast <address, port> for the respective groups
*/
#define PAXOS_LEARNERS_NET  "239.0.0.1", 6001
#define PAXOS_ACCEPTORS_NET "239.0.0.1", 6002
#define PAXOS_PROPOSERS_NET "239.0.0.1", 6003


/*** STRUCTURES SETTINGS ***/

/* 
   PROPOSER_ARRAY_SIZE needs to be bigger than
   the PROPOSER_PREEXEC_WIN_SIZE
   IMPORTANT: This must be a power of 2
*/
#define PROPOSER_ARRAY_SIZE 512

/* 
   LEARNER_ARRAY_SIZE can be any size, to ensure proper
   buffering it can be set to a multiple of the 
   PROPOSER_PREEXEC_WIN_SIZE
   IMPORTANT: This must be a power of 2
*/
#define LEARNER_ARRAY_SIZE 512


/*** DEBUGGING SETTINGS ***/
/*
  Verbosity of the library
  0 -> off (prints only errors)
  1 -> verbose
  3 -> debug
*/
#define VERBOSITY_LEVEL 4

/*
  If PAXOS_DEBUG_MALLOC is defined, it turns on
  malloc debugging to detect memory leaks.
  The trace filename must be defined anyway

*/
// #define PAXOS_DEBUG_MALLOC
#define MALLOC_TRACE_FILENAME "malloc_debug_trace.txt"


#endif /* _CONFIG_H_ */