#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libpaxos_priv.h"

static int vh_list_size = 0;
static vh_value_wrapper * vh_list_head = NULL;
static vh_value_wrapper * vh_list_tail = NULL;

static void 
vhtest() {
    unsigned int n_vals = 3;
    int valsize = 30;
    char c = '!';
    char * value;
    unsigned int i;
    for(i = 0; i < n_vals; i++) {
        value = malloc(valsize);
        memset(value, c, valsize);
        vh_enqueue_value(value, valsize);
        c++;
        valsize += 10;
    }
}

int 
vh_init() {
    //Create the emtpy values list
    vh_list_size = 0;
    vh_list_head = NULL;
    vh_list_tail = NULL;
    
    vhtest();
    return 0;
}

void 
vh_shutdown() {
    
}


int vh_pending_list_size() {
    return vh_list_size;
}


void vh_enqueue_value(char * value, size_t value_size) {
    vh_value_wrapper * new_vw = malloc(sizeof(vh_value_wrapper));
    new_vw->value = value;
    new_vw->value_size = value_size;
    new_vw->next = NULL;
    
    /* List is empty*/
	if (vh_list_head == NULL && vh_list_tail == NULL) {
		vh_list_head = new_vw;
		vh_list_tail = new_vw;
        vh_list_size = 1;

	/* List is not empty*/
	} else {
		vh_list_tail->next = new_vw;
		vh_list_tail = new_vw;
        vh_list_size += 1;
	}
    LOG(DBG, ("Value of size %lu enqueued\n", value_size));
}

vh_value_wrapper * 
vh_get_next_pending() {
    /* List is empty*/
	if (vh_list_head == NULL && vh_list_tail == NULL) {
        return NULL;
    }
    
    /* Pop */
    vh_value_wrapper * first_vw = vh_list_head;
    vh_list_head = first_vw->next;

    /* Also last element */
    if(vh_list_tail == first_vw) {
        vh_list_tail = NULL;
    }
    vh_list_size -= 1;
    LOG(DBG, ("Popping value of size %lu\n", first_vw->value_size));
    return first_vw;
}

void 
vh_push_back_value(vh_value_wrapper * vw) {
    /* Adds as list head*/
    vw->next = vh_list_head;
    
    /* List is empty*/
	if (vh_list_head == NULL && vh_list_tail == NULL) {
		vh_list_head = vw;
		vh_list_tail = vw;
        vh_list_size = 1;

	/* List is not empty*/
	} else {
		vh_list_head = vw;
        vh_list_size += 1;
	}
}

void vh_notify_client(unsigned int result, vh_value_wrapper * vw) {
    // TODO
    printf("NOTIFY CLIENT!\n");
}