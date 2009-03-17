#ifndef _CONFIG_H_
#define _CONFIG_H_

/*** PROTOCOL SETTINGS ***/

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