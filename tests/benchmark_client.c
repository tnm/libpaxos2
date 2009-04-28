#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>

#include "event.h"
#include "libpaxos.h"

int start_time;
int end_time;
unsigned int concurrent_values = 30;
struct timeval values_timeout;
int min_val_size = 30;
// int max_val_size = PAXOS_MAX_VALUE_SIZE;
int max_val_size = 300;
int duration = 120;
int print_step = 10;

int delivered_count = 0;
int submitted_count = 0;
int retried_count = 0;

struct event cl_periodic_event;
struct timeval cl_periodic_interval;

typedef struct client_value_record_t {
    struct timeval creation_time;
    struct timeval expire_time;
    size_t value_size;
    char value[PAXOS_MAX_VALUE_SIZE];
} client_value_record;

client_value_record * values_table;

paxos_submit_handle * psh = NULL;

static void 
sum_timevals(struct timeval * dest, struct timeval * t1, struct timeval * t2) {
    dest->tv_sec = t1->tv_sec + t2->tv_sec;
    unsigned int usecs = t1->tv_usec + t2->tv_usec;
    if(usecs > 1000000) {
        dest->tv_usec = (usecs % 1000000);
        dest->tv_sec += 1;
    } else {
        dest->tv_usec = usecs;
    }
}

static void 
submit_old_value(client_value_record * cvr) {
    retried_count += 1;

    //Leave value, value size and creation time unaltered

    //Set expiration as now+timeout
    struct timeval time_now;
    gettimeofday(&time_now, NULL);
    sum_timevals(&cvr->expire_time, &time_now, &values_timeout);
    
    //Send the value to proposers and return immediately
    pax_submit_nonblock(psh, cvr->value, cvr->value_size);
}

size_t random_value_gen(char * buf) {
    long r = random();
    int val_size = (r % ((max_val_size - min_val_size)+1)) + min_val_size;
    size_t passes = val_size / sizeof(long);
    long * ary = (long*) buf;
    size_t i;
    for(i = 0; i < passes; i++) {
        ary[i] = r;
    }
    return i*sizeof(long);
}

static void 
submit_new_value(client_value_record * cvr) {
    
    submitted_count += 1;
    if((submitted_count % print_step) == 0) {
        printf("Submitted %u values\n", submitted_count);
    }
    
    cvr->value_size = random_value_gen(cvr->value);
    
    //Set creation timestamp
    gettimeofday(&cvr->creation_time, NULL);
    
    //Set expiration as creation+timeout
    sum_timevals(&cvr->expire_time, &cvr->creation_time, &values_timeout);
    
    //Send the value to proposers and return immediately
    pax_submit_nonblock(psh, cvr->value, cvr->value_size);
    
}

int is_expired(struct timeval * deadline, struct timeval * time_now) {
    if(time_now->tv_sec > deadline->tv_sec) {
        return 1;
    }

    if(time_now->tv_sec == deadline->tv_sec &&
            time_now->tv_usec >= deadline->tv_usec) {
        return 1;
    }
    return 0;
}

static void
set_timeout_check() {
    event_add(&cl_periodic_event, &cl_periodic_interval);
}

static void
cl_periodic_timeout_check(int fd, short event, void *arg) {
    
    struct timeval time_now;
    gettimeofday(&time_now, NULL);

    // Iterate over the values submitted by this client
    unsigned int i;
    client_value_record * iter;
    for(i = 0; i < concurrent_values; i++) {
        iter = &values_table[i];

        if (is_expired(&iter->expire_time, &time_now)) {
            submit_old_value(iter);
        }    
    }
    
    //And set a timeout for calling this function again
    set_timeout_check();
    
    //Makes the compiler happy
    fd = fd;
    event = event;
    arg = arg;
}

//This is executed by the libevent/learner thread
//Before learner init returns
int cl_init() {
    
    psh = pax_submit_handle_init();
    if (psh == NULL) {
        printf("Client init failed [submit handle]\n");
        return -1;        
    }
    
    //Create table to store values submitted
    values_table = malloc(sizeof(client_value_record) * concurrent_values);
    if(values_table == NULL) {
        printf("Client init failed [malloc]\n");
        return -1;
    }
    
    //Submit N new values
    unsigned int i;
    for(i = 0; i < concurrent_values; i++) {
        submit_new_value(&values_table[i]);
    }
    
    //And set a timeout to check expired ones,
    evtimer_set(&cl_periodic_event, cl_periodic_timeout_check, NULL);    
    set_timeout_check();
    
    return 0;
}

void cl_deliver(char* value, size_t val_size, iid_t iid, ballot_t ballot, int proposer) {

    delivered_count += 1;
    assert((int)iid == delivered_count);
    
    struct timeval time_now;
    gettimeofday(&time_now, NULL);
    
    // Iterate over the values submitted by this client
    unsigned int i;
    client_value_record * iter;
    for(i = 0; i < concurrent_values; i++) {
        iter = &values_table[i];
        if(val_size == iter->value_size && 
                memcmp(value, iter->value, val_size) == 0) {
            //Our value, submit a new one!
            submit_new_value(iter);
        } else if (is_expired(&iter->expire_time, &time_now)) {
            //Value timer expired, resubmit
            submit_old_value(&values_table[i]);
        }    
    }
    
    //Makes the compiler happy
    iid = iid;
    ballot = ballot;
    proposer = proposer;
}

int main (int argc, char const *argv[])
{
    
    start_time = time(NULL);
    end_time = start_time + duration;
    
    //Default timeout for values
    values_timeout.tv_sec = 5;
    values_timeout.tv_usec = 0;
    
    //Default timeout check interval
    cl_periodic_interval.tv_sec = 5;
    cl_periodic_interval.tv_usec = 0;

    
    if(learner_init(cl_deliver, cl_init) != 0) {
        printf("Failed to start the learner!\n");
        return -1;
    }
    
    //Wait until benchmark time expires, then exit
    while(time(NULL) < end_time) {
        sleep(10);
    }    
    
    printf("Total delivered:%u\n", delivered_count);
    printf("\tRate:%f\n", ((float)delivered_count/duration));
    printf("Total submitted:%u\n", submitted_count);
    printf("\tRate:%f\n", ((float)submitted_count/duration));
    printf("Timed-out values:%u\n", retried_count);
    
    return 0;
}