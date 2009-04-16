struct event p1_check_event;
struct timeval p1_check_interval;

struct event p2_check_event;
struct timeval p2_check_interval;

/*-------------------------------------------------------------------------*/
// Phase 1 routines
/*-------------------------------------------------------------------------*/

static void
leader_check_p1_pending() {
    iid_t iid_iterator;
    p_inst_info * ii;

    //Create an empty prepare batch in send buffer
    sendbuf_clear(to_acceptors, prepare_reqs, this_proposer_id);

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
    
    if(active_count >= (PROPOSER_PREEXEC_WIN_SIZE/2)) {
        //More than half are active/pending
        // Wait before opening more
        return;
    }
    
    //Create an empty prepare batch in send buffer
    sendbuf_clear(to_acceptors, prepare_reqs, this_proposer_id);
    
    //How many new instances to open now
    unsigned int to_open = PROPOSER_PREEXEC_WIN_SIZE - active_count;

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
    }
    
    int diff_value = ((vw->value_size == ii->value_size) &&
        (memcmp(vw->value, ii->value, vw->value_size) == 0));

    //The P1 value is not ours, 
    // send back ours to list and do P2 with the one received
    if(diff_value) {
        vh_push_back_value(vw);
        ii->assigned_value = NULL;
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

static void
leader_open_instances_p2() {
    unsigned int count = 0;
    p_inst_info * ii;

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
        assert(ii->iid == p2_info.next_unused_iid);
        
        //Next unused is not ready, stop
        if(ii->status != p1_ready) {
            break;
        }

        //No value to send for next unused, stop
        if(ii->value == NULL &&
            ii->assigned_value == NULL &&
            vh_pending_list_size() == 0) {
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
            continue;
        }
        
        //Expired and not closed: must restart from phase 1
        ii->status = p1_pending;
        ii->my_ballot = NEXT_BALLOT(ii->my_ballot);
        //Send prepare to acceptors
        sendbuf_add_prepare_req(to_acceptors, ii->iid, ii->my_ballot);  
    }
    
    //Flush last message if any
    sendbuf_flush(to_acceptors);
}

/*-------------------------------------------------------------------------*/
// Initialization/shutdown
/*-------------------------------------------------------------------------*/

static int
leader_init() {
    LOG(VRB, ("Proposer %d promoted to leader\n", this_proposer_id));

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
    return 0;        
}

static void
leader_shutdown() {
    LOG(VRB, ("Proposer %d dropping leadership\n", this_proposer_id));

    evtimer_del(&p1_check_event);
    evtimer_del(&p2_check_event);
        
    //TODO Clear inst_info array
    
}