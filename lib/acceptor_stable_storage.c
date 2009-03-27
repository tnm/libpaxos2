#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/*
Getting started:
 http://www.oracle.com/technology/documentation/berkeley-db/db/gsg/C/index.html
API:
 http://www.oracle.com/technology/documentation/berkeley-db/db/api_c/frame.html
Reference Guide:
 http://www.oracle.com/technology/documentation/berkeley-db/db/ref/toc.html
BDB Forums @ Oracle
 http://forums.oracle.com/forums/forum.jspa?forumID=271
*/
#include <db.h>

#include "libpaxos_priv.h"

static DB_ENV *dbenv;
static DB *dbp;

static int do_recovery = 0;

void stablestorage_do_recovery() {
    printf("Acceptor in recovery mode\n");
    do_recovery = 1;
}

int stablestorage_init(int acceptor_id) {
    int result;
    int flags = 0;
    
    result = db_env_create(&dbenv, 0);
    if (result != 0) {
        printf("DB_ENV creation failed: %s\n", db_strerror(result));
        return -1;
    }
    dbenv->set_errfile(dbenv, stdout);	

    // flags |= DB_AUTO_COMMIT;
    flags |= BDB_TX_MODE; //defined in paxos_config.h
    
    result = dbenv->set_flags(dbenv, flags, 1);
    if (result != 0) {
        printf("DB_ENV set_flags failed: %s\n", db_strerror(result));
        return -1;
    }

    // result = dbenv->log_set_config(dbenv, DB_LOG_IN_MEMORY, 1);
    // assert(result == 0);
    
    // result = dbenv->set_lg_bsize(dbenv, LOG_BUF_SIZE);
    // assert(result == 0);
    
    // result = dbenv->set_cachesize(dbenv, 0, MEM_CACHE_SIZE, 1);
    // assert(result == 0);

    char db_env_path[512];
    sprintf(db_env_path, ACCEPTOR_DB_PATH);
    LOG(VRB, ("Opening env in: %s\n", db_env_path));
    
    struct stat sb;
    if (stat(db_env_path, &sb) != 0) {
        //Env dir does not exist
        //create it with rwx for owner
        if (mkdir(db_env_path, S_IRWXU) != 0) {
            printf("Failed to create env dir %s: %s\n", db_env_path, strerror(errno));
            return -1;
        }
    }

    flags = 0;
    //Create and for this process only
    flags |= (DB_CREATE | DB_PRIVATE); 
    //Transactional storage
    flags |= (DB_INIT_LOG | DB_INIT_TXN | DB_INIT_MPOOL); 

    if(do_recovery)
        flags |= DB_RECOVER;
    
    result = dbenv->open(dbenv, 
        db_env_path,            /* Environment directory */
        flags,                  /* Open flags */
        0);                     /* Default file permissions */
    if (result != 0) {
        printf("DB_ENV open failed: %s\n", db_strerror(result));
        return -1;
    }


    result = db_create(&dbp, dbenv, 0);
    if (result != 0) {
        printf("db_create failed: %s\n", db_strerror(result));
        return -1;
    }

    // result = dbp->set_pagesize(dbp, pagesize);
    // assert(result  == 0);

    char db_filename[512];
    sprintf(db_filename, ACCEPTOR_DB_FNAME);
    LOG(VRB, ("Opening db file %s/%s\n", db_env_path, db_filename));    
    
    //Open the database
    flags = 0;    
    //Create if does not exist
    flags |= DB_CREATE;
    //Drop current content of file
    if(!do_recovery)
        flags |= DB_TRUNCATE;

    result = dbp->open(dbp,
        NULL,                   /* Transaction pointer */
        db_filename,            /* On-disk file that holds the database. */
        NULL,                   /* Optional logical database name */
        ACCEPTOR_ACCESS_METHOD, /* Database access method */
        flags,                  /* Open flags */
        0);                     /* Default file permissions */

    if(result != 0) {
        printf("DB open failed: %s\n", db_strerror(result));
        return -1;
    }
    
    return 0;
}


int stablestorage_shutdown() {
    int result = 0;
    
    //Close db file
    if(dbp->close(dbp, 0) != 0) {
        printf("DB_ENV close failed\n");
        result = -1;
    }

    //Close handle
    if(dbenv->close(dbenv, 0) != 0) {
        printf("DB close failed\n");
        result = -1;
    }
 
    LOG(VRB, ("DB close completed\n"));
    
    return result;
}