struct event p1_check_event;
struct timeval p1_check_interval;

struct event p2_check_event;
struct timeval p2_check_interval;


#ifndef LEADER_EVENTS_UPDATE_INTERVAL
//Leader events display is disabled
static void empty_fun() {};
#define COUNT_EVENT(E) empty_fun()
#else
//Leader events display is enabled
struct leader_event_counters {
    long unsigned int p1_timeout;
    long unsigned int p2_timeout;
};
struct leader_event_counters lead_counters;
struct event print_events_event;
struct timeval print_events_interval;
#define COUNT_EVENT(E) (lead_counters.E += 1)
static void clear_event_counters() {
    lead_counters.p1_timeout = 0;    
    lead_counters.p2_timeout = 0;
}

static void 
leader_print_event_counters(int fd, short event, void *arg) {
    UNUSED_ARG(fd);
    UNUSED_ARG(event);
    UNUSED_ARG(arg);
    printf("-----------------------------------------------\n");
    printf("current_iid:%lu\n", current_iid);
    printf("p1_timeout:%lu\n", lead_counters.p1_timeout);
    printf("p2_timeout:%lu\n", lead_counters.p2_timeout);
    printf("p1_info.pending_count:%u\n", p1_info.pending_count);
    printf("p1_info.ready_count:%u\n", p1_info.ready_count);
    printf("p1_info.highest_open:%lu\n", p1_info.highest_open);
    printf("p2_info.next_unused_iid:%lu\n", p2_info.next_unused_iid);
    printf("-----------------------------------------------\n");
    
    int ret;
    ret = event_add(&print_events_event, &print_events_interval);
    assert(ret == 0);

}
#endif

/*-------------------------------------------------------------------------*/
// Phase 1 routines
/*-------------------------------------------------------------------------*/

static void
leader_check_p1_pending() {
    iid_t iid_iterator;
    p_inst_info * ii;

    //Create an empty prepare batch in send buffer
    sendbuf_clear(to_acceptors, prepare_reqs, this_proposer_id);
    LOG(DBG, ("Checking pending phase 1 from %lu to %lu\n",
        current_iid, p1_info.highest_open));
    for(iid_iterator = current_iid; 
        iid_iterator <= p1_info.highest_open; iid_iterator++) {

        //Get instance from state array
        ii = GET_PRO_INSTANCE(iid_iterator);
        assert(ii->iid == iid_iterator);
        
        //Still pending -> it's expired
        if(ii->status == p1_pending) {
            LOG(DBG, ("Phase 1 of instance %ld expired!\n", ii->iid));

            ii->my_ballot = NEXT_BALLOT(ii->my_ballot);
            //Send prepare to acceptors
            sendbuf_add_prepare_req(to_acceptors, ii->iid, ii->my_ballot);        

            COUNT_EVENT(p1_timeout);
        }
    }    

    //Send if something is still there
    sendbuf_flush(to_acceptors);
}

//Opens instances at the "end" of the proposer state array 
//Those instances were not opened before
static void
leader_open_instances_p1() {
    int active_count = p1_info.pending_count + p1_info.ready_count;
    
    assert(active_count >= 0);
    
    if(active_count >= (PROPOSER_PREEXEC_WIN_SIZE/2)) {
        //More than half are active/pending
        // Wait before opening more
        return;
    }
    
    //Create an empty prepare batch in send buffer
    sendbuf_clear(to_acceptors, prepare_reqs, this_proposer_id);
    
    //How many new instances to open now
    unsigned int to_open = PROPOSER_PREEXEC_WIN_SIZE - active_count;
    assert(to_open >= (PROPOSER_PREEXEC_WIN_SIZE/2));

    iid_t i, curr_iid;
    p_inst_info * ii;
    for(i = 1; i <= to_open; i++) {
        //Get instance from state array
        curr_iid = i + p1_info.highest_open; 
        ii = GET_PRO_INSTANCE(curr_iid);
        assert(ii->status == empty);
        
        //Create initial record
        ii->iid = curr_iid;
        ii->status = p1_pending;
        ii->my_ballot = FIRST_BALLOT;
        //Send prepare to acceptors
        sendbuf_add_prepare_req(to_acceptors, ii->iid, ii->my_ballot);        
    }

    //Send if something is still there
    sendbuf_flush(to_acceptors);
    
    //Keep track of pending count
    p1_info.pending_count += to_open;

    //Set new higher bound for checking
    p1_info.highest_open += to_open; 
    
    LOG(DBG, ("Opened %d new instances\n", to_open));

    
}

static void leader_set_p1_timeout() {
    int ret;
    ret = event_add(&p1_check_event, &p1_check_interval);
    assert(ret == 0);
}

//This function is invoked periodically
// (periodic_repeat_interval) to retrasmit the most 
// recent accept
static void
leader_periodic_p1_check(int fd, short event, void *arg)
{
    UNUSED_ARG(fd);
    UNUSED_ARG(event);
    UNUSED_ARG(arg);
    
    //All instances in status p1_pending are expired
    // increment ballot and re-send prepare_req
    leader_check_p1_pending();
    
    //Try to open new instances if some were used
    leader_open_instances_p1();
    
    //Set next check timeout for calling this function
    leader_set_p1_timeout();
    
}

/*-------------------------------------------------------------------------*/
// Phase 2 routines
/*-------------------------------------------------------------------------*/
static void leader_set_p2_timeout() {
    int ret;
    ret = event_add(&p2_check_event, &p2_check_interval);
    assert(ret == 0);
}

static void
ldr_exec_reserved_p2(p_inst_info * ii) {
    //Phase 1 completed with a value
    //Send an accept with that value
    // this is a strict requirement!
    
    vh_value_wrapper * vw = ii->assigned_value;

    //No value assigned by us to this instance
    if(vw == NULL) {
        //Send the accept with the reserved value
        sendbuf_add_accept_req(to_acceptors, ii);
        LOG(VRB, ("Sending accept reserved instance %lu\n", ii->iid));
        return;
    }
    
    //Found a value and we assigned one
    
    int same_value = ((vw->value_size == ii->value_size) &&
        (memcmp(vw->value, ii->value, vw->value_size) == 0));

    //The P1 value is not ours, 
    // send back ours to list and do P2 with the one received
    if(!same_value) {
        vh_push_back_value(vw);
        ii->assigned_value = NULL;
        LOG(VRB, ("Pushing back assigned value for reserved instance %lu\n", ii->iid));
    }
    
    //Send the accept with the reserved value
    sendbuf_add_accept_req(to_acceptors, ii);

}

static void
ldr_exec_fresh_p2(p_inst_info * ii) {
    //Phase 1 completed without a value
    //We can send ours (assigned value)

    vh_value_wrapper * vw = ii->assigned_value;
    assert(vw != NULL);
    
    ii->value = vw->value;
    ii->value_size = vw->value_size;
    
    //Send the accept with our value
    sendbuf_add_accept_req(to_acceptors, ii);
    LOG(VRB, ("Sending accept for clear instance %lu\n", ii->iid));

}

static void
set_p2_expiration_timestamp(p_inst_info * ii, struct timeval * current_time) {
    struct timeval * deadline = &ii->p2_timeout;
    const unsigned int a_second = 1000000; 

    //Set seconds
    deadline->tv_sec = current_time->tv_usec + (P2_TIMEOUT_INTERVAL / a_second);
    
    //Sum microsecs
    unsigned int usec_sum;
    usec_sum = current_time->tv_usec + (P2_TIMEOUT_INTERVAL % a_second);
    
    //If sum of mircosecs exceeds 10d6, add another second
    if(usec_sum > a_second) {
        deadline->tv_sec += 1; 
    }

    //Set microseconds
    deadline->tv_usec = (usec_sum % a_second);
}

static void
leader_execute_p2(p_inst_info * ii, struct timeval * current_time) {
    if(ii->value != NULL) {
        //A value was received during phase 1
        //We MUST execute p2 with that value
        //But there may be an assigned value too
        ldr_exec_reserved_p2(ii);
    } else if (ii->assigned_value != NULL) {
        //Phase 1 completed with no value, 
        // but a client value was already assigned
        ldr_exec_fresh_p2(ii);
    } else {
        //Phase 1 completed with no value, 
        // assign a value from pending list
        ii->assigned_value = vh_get_next_pending();
        assert(ii->assigned_value != NULL);
        //And do normal phase 2
        ldr_exec_fresh_p2(ii);
    }
    
    set_p2_expiration_timestamp(ii, current_time);
    ii->status = p2_pending;
}

// Scan trough p1_ready that have a value
// assigned or found, execute phase2
static void
leader_open_instances_p2_expired() {
    unsigned int count = 0;
    
    //Create a batch of accept requests
    sendbuf_clear(to_acceptors, accept_reqs, this_proposer_id);
    
    //Save the time at which phase2 started
    struct timeval time_now;
    gettimeofday(&time_now, NULL);
    
    //Start new phase 2 for all instances found in status p1_ready
    // if they are in the range below, phase2 timed-out and 
    // we went successfully trough phase1 again
    iid_t iid;
    p_inst_info * ii;
    
    for(iid = current_iid; iid < p2_info.next_unused_iid; iid++) {
    
        ii = GET_PRO_INSTANCE(iid);
        assert(ii->iid == iid);
        
        if(ii->status == p1_ready) {
            assert(ii->value != NULL || ii->assigned_value != NULL);
            //Executes phase2, sending an accept request
            //Using the found value or getting the next from list
            leader_execute_p2(ii, &time_now);

            //Count opened
            count += 1;
        }
    }
    
    //Count p1_ready that were consumed
    p1_info.ready_count -= count;
    
    //Flush last accept_req batch
    sendbuf_flush(to_acceptors);
    
    LOG(DBG, ("Opened %u old (timed-out) instances\n", count));
    
}


static void
leader_open_instances_p2_new() {
    unsigned int count = 0;
    p_inst_info * ii;

    //For better batching, opening new instances at the end
    // is preferred when more than 1 can be opened together
    unsigned int max_active_p2 = p2_info.next_unused_iid - current_iid;
    if ((max_active_p2) > (PROPOSER_P2_CONCURRENCY/2)) {
        LOG(DBG, ("Skipping Phase2 open, %u may be still active\n", 
            max_active_p2));
        return;
    }

    //Create a batch of accept requests
    sendbuf_clear(to_acceptors, accept_reqs, this_proposer_id);
    
    //Save the time at which phase2 started
    struct timeval time_now;
    gettimeofday(&time_now, NULL);
    
    //Start new phase 2 while there is some value from 
    // client to send and we can open more concurrent instances
    while((p2_info.next_unused_iid - current_iid) 
                        <= PROPOSER_P2_CONCURRENCY) {

        ii = GET_PRO_INSTANCE(p2_info.next_unused_iid);
        assert(ii->assigned_value == NULL);
        
        //Next unused is not ready, stop
        if(ii->status != p1_ready || ii->iid != p2_info.next_unused_iid) {
            LOG(DBG, ("Next instance to use for P2 (iid:%lu) is not ready yet\n", p2_info.next_unused_iid));
            break;
        }

        //No value to send for next unused, stop
        if(ii->value == NULL &&
            ii->assigned_value == NULL &&
            vh_pending_list_size() == 0) {
                LOG(DBG, ("No value to use for next instance\n"));
                break;
        }

        //Executes phase2, sending an accept request
        //Using the found value or getting the next from list
        leader_execute_p2(ii, &time_now);
        
        //Count opened
        count += 1;
        //Update next to use
        p2_info.next_unused_iid += 1;
    }
    
    //Count p1_ready that were consumed
    p1_info.ready_count -= count;
    
    //Flush last accept_req batch
    sendbuf_flush(to_acceptors);
    
    LOG(DBG, ("Opened %u new instances\n", count));
}

static int
ldr_is_expired(struct timeval * deadline, struct timeval * time_now) {
    return (deadline->tv_sec < time_now->tv_sec ||
            (deadline->tv_sec == time_now->tv_sec &&
            deadline->tv_usec < time_now->tv_usec));
}

static void
leader_periodic_p2_check(int fd, short event, void *arg) {
    UNUSED_ARG(fd);
    UNUSED_ARG(event);
    UNUSED_ARG(arg);
    
    // create a prepare batch for expired instances
    sendbuf_clear(to_acceptors, prepare_reqs, this_proposer_id);
    
    struct timeval now;
    gettimeofday(&now, NULL);
    // from current to highest open, check deadline
    // if instances in status p2_pending
    iid_t i;
    p_inst_info * ii;
    for(i = current_iid; i < p2_info.next_unused_iid; i++) {
        ii = GET_PRO_INSTANCE(i);
        
        //Not p2_pending, skip
        if(ii->status != p2_pending) {
            continue;
        }
        
        //Not expired yet, skip
        if(!ldr_is_expired(&ii->p2_timeout, &now)) {
            continue;
        }
        
        //Timer is expired!
        
        //Check if it was closed in the meanwhile 
        // (but not delivered yet)
        if(learner_is_closed(i)) {
            ii->status = p2_completed;
            //The rest (i.e. answering client)
            // is done when the value is actually delivered
            LOG(VRB, ("Instance %lu closed, waiting for deliver\n", i));
            continue;
        }
        
        //Expired and not closed: must restart from phase 1
        ii->status = p1_pending;
        p1_info.pending_count += 1;
        ii->my_ballot = NEXT_BALLOT(ii->my_ballot);
        //Send prepare to acceptors
        sendbuf_add_prepare_req(to_acceptors, ii->iid, ii->my_ballot);  
        LOG(VRB, ("Instance %lu restarts from phase 1\n", i));
        COUNT_EVENT(p2_timeout);

    }
    
    //Flush last message if any
    sendbuf_flush(to_acceptors);
    
    //Open new instances
    leader_open_instances_p2_new();
    
    //Set next invokation of this function
    leader_set_p2_timeout();

}

/*-------------------------------------------------------------------------*/
// Deliver callback
/*-------------------------------------------------------------------------*/
static void 
leader_deliver(char * value, size_t size, iid_t iid, ballot_t ballot, int proposer) {
    UNUSED_ARG(ballot);
    UNUSED_ARG(proposer);
    LOG(DBG, ("Instance %lu delivered to Leader\n", iid));

    //Verify that the value is the one found or associated
    p_inst_info * ii = GET_PRO_INSTANCE(iid);
    //Instance not even initialized, skip
    if(ii->iid != iid) {
        return;
    }
    
    if(ii->status == p1_pending) {
        p1_info.pending_count -= 1;
    }
    
    if(p2_info.next_unused_iid == iid) {
        p2_info.next_unused_iid += 1;
    }
    int same_val = (ii->value != NULL) && 
        (ii->value_size == size) &&
        (memcmp(value, ii->value, size) == 0);
    
    vh_value_wrapper * vw = ii->assigned_value;
    int assigned_val = same_val &&
        (ii->assigned_value != NULL) && 
        (vw->value_size == size) &&
        ((vw->value == ii->value) || 
            memcmp(vw->value, ii->value, size) == 0);

    if(assigned_val) {
        vh_notify_client(0, vw);
    } else if(vw != NULL) {
        vh_push_back_value(vw);
        //If ref is the same
        if(ii->value == vw->value) {
            //Delete pointer otherwise freed in clear_ii below
            ii->value = NULL;
        }
    }

    //Clear current instance
    pro_clear_instance_info(ii);
    
    //If enough instances are ready to 
    // be opened, start phase2 for them
    leader_open_instances_p2_new();
}


/*-------------------------------------------------------------------------*/
// Initialization/shutdown
/*-------------------------------------------------------------------------*/

static int
leader_init() {
    LOG(VRB, ("Proposer %d promoted to leader\n", this_proposer_id));
    
#ifdef LEADER_EVENTS_UPDATE_INTERVAL
    clear_event_counters();
    evtimer_set(&print_events_event, leader_print_event_counters, NULL);
    evutil_timerclear(&print_events_interval);
    print_events_interval.tv_sec = (LEADER_EVENTS_UPDATE_INTERVAL / 1000000);
    print_events_interval.tv_usec = (LEADER_EVENTS_UPDATE_INTERVAL % 1000000);
    leader_print_event_counters(0, 0, NULL);
#endif

    //Initialize values handler
    if(vh_init()!= 0) {
        printf("Values handler initialization failed!\n");
        return -1;
    }

    //TODO check again later...
    p1_info.pending_count = 0;
    p1_info.ready_count = 0;
    p1_info.highest_open = current_iid - 1;
    
    //Initialize timer and corresponding event for
    // checking timeouts of instances, phase 1
    evtimer_set(&p1_check_event, leader_periodic_p1_check, NULL);
    evutil_timerclear(&p1_check_interval);
    p1_check_interval.tv_sec = (P1_TIMEOUT_INTERVAL / 1000000);
    p1_check_interval.tv_usec = (P1_TIMEOUT_INTERVAL % 1000000);
    
    //Check pending, open new, set next timeout
    leader_periodic_p1_check(0, 0, NULL);
    
    //TODO check again later...
    p2_info.next_unused_iid = current_iid;
    
    //Initialize timer and corresponding event for
    // checking timeouts of instances, phase 2
    evtimer_set(&p2_check_event, leader_periodic_p2_check, NULL);
    evutil_timerclear(&p2_check_interval);
    p2_check_interval.tv_sec = ((P2_TIMEOUT_INTERVAL/3) / 1000000);
    p2_check_interval.tv_usec = ((P2_TIMEOUT_INTERVAL/3) % 1000000);
    leader_set_p2_timeout();
    
    LOG(VRB, ("Leader is ready\n"));
    return 0;        
}

static void
leader_shutdown() {
    LOG(VRB, ("Proposer %d dropping leadership\n", this_proposer_id));

    evtimer_del(&p1_check_event);
    evtimer_del(&p2_check_event);
        
    //TODO for all opened answer clients
    //TODO for all pending answer clients
    //TODO Clear inst_info array
    
    vh_shutdown();
}