#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "libpaxos.h"

static char as_char(char c) {
    if(c < 33 || c > 126) {
        return '!';        
    } else {
        return c;
    }
}

void my_deliver_fun(char* value, size_t value_size, int iid, int ballot, int proposer) {
    printf("Paxos instance %d closed by ballot %d\n", iid, ballot);
    printf("Value (by proposer:%d, size: %d) ->", proposer, (int)value_size);
    printf("[%c][%c][%c][...]\n", as_char(value[0]), as_char(value[1]), as_char(value[2]));
}

int my_custom_init() {
    printf(">>> This is invoked by another thread in libevent\n");
    printf(">>> After the normal learner initialization\n");
    return 0;
}

int main (int argc, char const *argv[]) {
    if (learner_init(my_deliver_fun, my_custom_init) != 0) {
        printf("Could not start the learner!\n");
        exit(1);
    }
    
    while(1) {
        //This thread does nothing...
        //But it can't terminate!
        sleep(1);
    }
    


    //Makes the compiler happy....
    argc = argc;
    argv = argv;
    return 0;
}