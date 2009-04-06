#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>
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

//DB env handle, DB handle, Transaction handle 
static DB_ENV *dbenv;
static DB *dbp;
static DB_TXN *txn;

//Buffer to read/write current record
static char record_buf[MAX_UDP_MSG_SIZE];
static acceptor_record * record_buffer = (acceptor_record*)record_buf;

//Set to 1 if init should do a recovery
static int do_recovery = 0;

//Invoked before stablestorage_init, sets recovery mode on
// the acceptor will try to recover a DB rather than creating a new one
void stablestorage_do_recovery() {
    printf("Acceptor in recovery mode\n");
    do_recovery = 1;
}

//Initializes the underlying stable storage
int stablestorage_init(int acceptor_id) {
    int result;
    int flags = 0;
    
    //Create environment handle
    result = db_env_create(&dbenv, 0);
    if (result != 0) {
        printf("DB_ENV creation failed: %s\n", db_strerror(result));
        return -1;
    }
    dbenv->set_errfile(dbenv, stdout);	

    //defined in paxos_config.h, RECNO or BTREE
    flags |= BDB_TX_MODE; 
    
    result = dbenv->set_flags(dbenv, flags, 1);
    if (result != 0) {
        printf("DB_ENV set_flags failed: %s\n", db_strerror(result));
        return -1;
    }

    //Set in-memory logging (no durability!)
    // result = dbenv->log_set_config(dbenv, DB_LOG_IN_MEMORY, 1);
    // assert(result == 0);
    
    //Set log in-memory size
    // result = dbenv->set_lg_bsize(dbenv, LOG_BUF_SIZE);
    // assert(result == 0);
    
    //Set the size of the memory cache
    // result = dbenv->set_cachesize(dbenv, 0, MEM_CACHE_SIZE, 1);
    // assert(result == 0);

    //Check if the environment dir exists
    char db_env_path[512];
    sprintf(db_env_path, ACCEPTOR_DB_PATH);
    LOG(VRB, ("Opening env in: %s\n", db_env_path));
    
    struct stat sb;
    if (stat(db_env_path, &sb) != 0) {
        //Dir does not exist,
        //create it with rwx for owner
        if (mkdir(db_env_path, S_IRWXU) != 0) {
            printf("Failed to create env dir %s: %s\n", db_env_path, strerror(errno));
            return -1;
        }
    }

    flags = 0;
    //Create and for this process only
    flags |= (DB_CREATE | DB_PRIVATE); 
    //Transactional storage for a single thread
    flags |= (DB_INIT_LOG | DB_INIT_TXN | DB_INIT_MPOOL); 

    //Add this flag if this acceptor is recovering 
    //from a crash rather than starting "fresh"
    if(do_recovery)
        flags |= DB_RECOVER;
    
    //Open the DB environment
    result = dbenv->open(dbenv, 
        db_env_path,            /* Environment directory */
        flags,                  /* Open flags */
        0);                     /* Default file permissions */
    if (result != 0) {
        printf("DB_ENV open failed: %s\n", db_strerror(result));
        return -1;
    }

    //Create the DB file
    result = db_create(&dbp, dbenv, 0);
    if (result != 0) {
        printf("db_create failed: %s\n", db_strerror(result));
        return -1;
    }
    
    //Set page size for this db
    // result = dbp->set_pagesize(dbp, pagesize);
    // assert(result  == 0);

    char db_filename[512];
    sprintf(db_filename, ACCEPTOR_DB_FNAME);
    LOG(VRB, ("Opening db file %s/%s\n", db_env_path, db_filename));    
    
    flags = 0;    
    //Create if does not exist
    flags |= DB_CREATE;
    //Drop current content of file, 
    // unless we are in recovery mode
    if(!do_recovery)
        flags |= DB_TRUNCATE;

    //Open the DB file
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

//Safely closes the underlying stable storage
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

//Begins a new transaction in the stable storage
void 
stablestorage_tx_begin() {
    int result;
    result = dbenv->txn_begin(dbenv, NULL, &txn, 0);
    assert(result == 0);
}

//Commits the transaction to stable storage
void 
stablestorage_tx_end() {
    int result;
    //Since it's either read only or write only
    // and there is no concurrency, should always commit!
    result = txn->commit(txn, 0);
    assert(result == 0);
}

//Retrieves an instance record from stable storage
// returns null if the instance does not exist yet
acceptor_record * 
stablestorage_get_record(iid_t iid) {
    int flags, result;
    DBT dbkey, dbdata;
    
    memset(&dbkey, 0, sizeof(DBT));
    memset(&dbdata, 0, sizeof(DBT));

    //Key is iid
    dbkey.data = &iid;
    dbkey.size = sizeof(iid_t);
    
    //Data is our buffer
    dbdata.data = record_buffer;
    dbdata.ulen = MAX_UDP_MSG_SIZE;
    //Force copy to the specified buffer
    dbdata.flags = DB_DBT_USERMEM;

    //Read the record
    flags = 0;
    result = dbp->get(dbp, 
        txn, 
        &dbkey, 
        &dbdata, 
        flags);
    
    if(result == DB_NOTFOUND || result == DB_KEYEMPTY) {
        //Record does not exist
        LOG(DBG, ("The record for iid:%lu does not exist\n", iid));
        return NULL;
    } else if (result != 0) {
        //Read error!
        printf("Error while reading record for iid:%lu : %s\n",
            iid, db_strerror(result));
        return NULL;
    }
    
    //Record found
    assert(iid == record_buffer->iid);
    return record_buffer;
}

//Save a valid accept request, the instance may be new (no record)
// or old with a smaller ballot, in both cases it creates a new record
acceptor_record * 
stablestorage_save_accept(accept_req * ar) {
    int flags, result;
    DBT dbkey, dbdata;
    
    //Store as acceptor_record (== accept_ack)
    record_buffer->iid = ar->iid;
    record_buffer->ballot = ar->ballot;
    record_buffer->value_ballot = ar->ballot;
    record_buffer->is_final = 0;
    record_buffer->value_size = ar->value_size;
    memcpy(record_buffer->value, ar->value, ar->value_size);
    
    memset(&dbkey, 0, sizeof(DBT));
    memset(&dbdata, 0, sizeof(DBT));

    //Key is iid
    dbkey.data = &ar->iid;
    dbkey.size = sizeof(iid_t);
        
    //Data is our buffer
    dbdata.data = record_buffer;
    dbdata.size = ACCEPT_ACK_SIZE(record_buffer);
    
    //Store permanently
    flags = 0;
    result = dbp->put(dbp, 
        txn, 
        &dbkey, 
        &dbdata, 
        0);

    assert(result == 0);    
    return record_buffer;
}

//Save a valid prepare request, the instance may be new (no record)
// or old with a smaller ballot
acceptor_record * 
stablestorage_save_prepare(prepare_req * pr, acceptor_record * rec) {
    int flags, result;
    DBT dbkey, dbdata;
    
    //No previous record, create a new one
    if (rec == NULL) {
        //Record does not exist yet
        rec = record_buffer;
        rec->iid = pr->iid;
        rec->ballot = pr->ballot;
        rec->value_ballot = 0;
        rec->is_final = 0;
        rec->value_size = 0;
    } else {
    //Record exists, just update the ballot
        rec->ballot = pr->ballot;
    }
    
    memset(&dbkey, 0, sizeof(DBT));
    memset(&dbdata, 0, sizeof(DBT));

    //Key is iid
    dbkey.data = &pr->iid;
    dbkey.size = sizeof(iid_t);
        
    //Data is our buffer
    dbdata.data = record_buffer;
    dbdata.size = ACCEPT_ACK_SIZE(record_buffer);
    
    //Store permanently
    flags = 0;
    result = dbp->put(dbp, 
        txn, 
        &dbkey, 
        &dbdata, 
        0);
        
    assert(result == 0);
    return record_buffer;
    
}
