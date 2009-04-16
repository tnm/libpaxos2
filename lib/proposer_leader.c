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
    inst_info * ii;

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
    inst_info * ii;
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
// Phase 1 routines
/*-------------------------------------------------------------------------*/
static void
leader_open_instances_p2() {
    
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
    long unsigned int check_interval_usec = P1_TIMEOUT_INTERVAL/3;
    p1_check_interval.tv_sec = (check_interval_usec / 1000000);
    p1_check_interval.tv_usec = (check_interval_usec % 1000000);
    
    //Check pending, open new, set next timeout
    leader_periodic_p1_check(0, 0, NULL);
    
    
    
    return 0;        
}

static void
leader_shutdown() {
    LOG(VRB, ("Proposer %d dropping leadership\n", this_proposer_id));

    evtimer_del(&p1_check_event);
    evtimer_del(&p2_check_event);
        
    //TODO Clear inst_info array
    
}