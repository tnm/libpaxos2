static void
leader_check_p1_pending() {
    iid_t curr_iid;
    inst_info * ii;

    //Create an empty prepare batch in send buffer
    sendbuf_clear(to_acceptors, prepare_reqs, this_proposer_id);

    for(curr_iid = p1_info.lowest_open; 
        curr_iid <= p1_info.highest_open; curr_iid++) {

        //Get instance from state array
        ii = GET_PRO_INSTANCE(curr_iid);
        assert(ii->iid == curr_iid);
        
        //Still pending -> it's expired
        if(ii->status == p1_pending) {
            ii->my_ballot = NEXT_BALLOT(ii->my_ballot);
            //Send prepare to acceptors
            sendbuf_add_prepare_req(to_acceptors, ii->iid, ii->my_ballot);        
        }
    }    

    //Send if something is still there
    sendbuf_flush(to_acceptors);
}

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
    
}

static void
leader_periodic_p1_check() {
    //All instances in status p1_pending are expired
    // increment ballot and re-send prepare_req
    leader_check_p1_pending();
    
    //Try to open new instances if some were used
    leader_open_instances_p1();
    
    //Set next check timeout
    leader_set_p1_timeout();
}

static int
leader_init() {
    //TODO check again later...
    p1_info.pending_count = 0;
    p1_info.ready_count = 0;
    p1_info.lowest_open = current_iid;
    p1_info.highest_open = current_iid - 1;
    
    leader_periodic_p1_check();
    
    return 0;        
}

static void
leader_shutdown() {
    cancel_phase1_alarm();
    cancel_phase2_alarms();
    
    //TODO Clear inst_info array
    
}