#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "libpaxos.h"


int main (int argc, char const *argv[]) {
    if (acceptor_init() != 0) {
        printf("Could not start the acceptor!\n");
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
