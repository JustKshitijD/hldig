//
// DB2_db.cc
//
// Implementation of DB2_db
//
// $Log: DB2_db.cc,v $
// Revision 1.1  1998/06/21 23:20:06  turtle
// patches by Esa and Jesse to add BerkeleyDB and Prefix searching
//
// Revision 1.1  1997/11/26  22:18:46  jesse
// Initial revision
//
// Revision 1.1.1.1  1997/02/03 17:11:05  turtle
// Initial CVS
//
//
#if RELEASE
static char RCSid[] = "$Id: DB2_db.cc,v 1.1 1998/06/21 23:20:06 turtle Exp $";
#endif

#include "DB2_db.h"
#include <errno.h>
#include <stdlib.h>
#include <fstream.h>
#include <malloc.h>

// Where do I need this for? I don't know.
int errno;

// Default cache size in kilobytes.
// Maybe this should be an config option, just for easy testing and
//   determination for best system performance
// NOTE: page size is 1KB - do not change!!
#define CACHE_SIZE_IN_KB 64

//*****************************************************************************
// DB2_db::DB2_db()
//
DB2_db::DB2_db()
{
    isOpen = 0;
}


//*****************************************************************************
// DB2_db::~DB2_db()
//
DB2_db::~DB2_db()
{
    if (isOpen)
    {
	Close();
    }
}


//*****************************************************************************
// int DB_db::OpenReadWrite(char *filename, int mode)
//
int
DB2_db::OpenReadWrite(char *filename, int mode)
{
    /* Initialize the database environment. */
    dbenv = db_init(NULL);
    memset(&dbinfo, 0, sizeof(dbinfo));
    dbinfo.db_cachesize = CACHE_SIZE_IN_KB * 1024;	/* Cachesize: 64K. */
    dbinfo.db_pagesize = 1024;      			/* Page size: 1K. */



    /* Create the database. */
    if ((errno = db_open(filename, DB_BTREE, DB_CREATE, mode, dbenv, &dbinfo, &dbp)) == 0) {
        /* Acquire a cursor for the database. */
        if ((seqrc = dbp->cursor(dbp, NULL, &dbcp)) != 0) {
            seqerr = seqrc;
	    isOpen = 0;
            Close();
	    return NOTOK;
        }
	isOpen = 1;
	return OK;
    }
    else
    {
	return NOTOK;
    }
}


//*****************************************************************************
// int DB2_db::OpenRead(char *filename)
//
int
DB2_db::OpenRead(char *filename)
{
    /* Initialize the database environment. */
    dbenv = db_init(NULL);
    memset(&dbinfo, 0, sizeof(dbinfo));
    dbinfo.db_cachesize = CACHE_SIZE_IN_KB * 1024;	/* Cachesize: 64K. */
    dbinfo.db_pagesize = 1024;				/* Page size: 1K. */


    /* Open the database. */
    if ((errno = db_open(filename, DB_BTREE, DB_RDONLY, 0, dbenv, &dbinfo, &dbp)) == 0) {
        /* Acquire a cursor for the database. */
        if ((seqrc = dbp->cursor(dbp, NULL, &dbcp)) != 0) {
            seqerr = seqrc;
	    isOpen = 0;
            Close();
	    return NOTOK;
        }
	isOpen = 1;
	return OK;
    }
    else
    {
	return NOTOK;
    }
}


//*****************************************************************************
// int DB2_db::Close()
//
int
DB2_db::Close()
{
    if (isOpen)
    {
	// Close cursor, database and clean up environment
        (void)(dbcp->c_close)(dbcp);
	(void)(dbp->close)(dbp, 0);
	(void) db_appexit(dbenv);
    }
    isOpen = 0;
    return OK;
}


//*****************************************************************************
// void DB2_db::Start_Get()
//
void
DB2_db::Start_Get()
{
    DBT	nextkey;

    // skey and nextkey are just dummies
    memset(&nextkey, 0, sizeof(DBT));
    memset(&skey, 0, sizeof(DBT));

//    skey.data = "";
//    skey.size = 0;
//    skey.flags = 0;
    if (isOpen && dbp) {
	// Set the cursor to the first position.
        seqrc = dbcp->c_get(dbcp, &skey, &nextkey, DB_FIRST);
	seqerr = seqrc;
    }
}


//*****************************************************************************
// char *DB2_db::Get_Next()
//
char *
DB2_db::Get_Next()
{
    //
    // Looks like get Get_Next() and Get_Next_Seq() are pretty much the same...
    //

    DBT	nextkey;
	
    memset(&nextkey, 0, sizeof(DBT));

    if (isOpen && !seqrc)
    {
	lkey = 0;
	lkey.append((char *)skey.data, skey.size);
	// DON'T forget to set the flags to 0!
	skey.flags = 0;
        seqrc = dbcp->c_get(dbcp, &skey, &nextkey, DB_NEXT);
	seqerr = seqrc;
	return lkey.get();
    }
    else
	return 0;
}

//*****************************************************************************
// void DB2_db::Start_Seq()
//
void
DB2_db::Start_Seq(char *str)
{
    DBT	nextkey;

    memset(&skey, 0, sizeof(DBT));
    memset(&nextkey, 0, sizeof(DBT));

    skey.data = str;
    skey.size = strlen(str);
    if (isOpen && dbp) {
	// Okay, get the first key. Use DB_SET_RANGE for finding
	// partial keys also. If you set it to DB_SET, and the
	// words book, books and bookstore do exists, it will
	// find them if you specify book*. However if you
	// specify boo* if will not find anything. Setting to
	// DB_SET_RANGE will still find the `first' word after boo*
	// (which is book).
        seqrc = dbcp->c_get(dbcp, &skey, &nextkey, DB_SET_RANGE);
	seqerr = seqrc;
    }
}


//*****************************************************************************
// char *DB2_db::Get_Next_Seq()
//
char *
DB2_db::Get_Next_Seq()
{
    DBT	nextkey;
	
    memset(&nextkey, 0, sizeof(DBT));

    if (isOpen && !seqrc)
    {
	lkey = 0;
	lkey.append((char *)skey.data, skey.size);
	skey.flags = 0;
        seqrc = dbcp->c_get(dbcp, &skey, &nextkey, DB_NEXT);
	seqerr = seqrc;
	return lkey.get();
    }
    else
	return 0;
}



//*****************************************************************************
// int DB2_db::Put(String &key, String &data)
//
int
DB2_db::Put(String &key, String &data)
{
    DBT	k, d;

    memset(&k, 0, sizeof(DBT));
    memset(&d, 0, sizeof(DBT));

    if (!isOpen)
	return NOTOK;

    k.data = key.get();
    k.size = key.length();

    d.data = data.get();
    d.size = data.length();

    // A 0 in the flags in put means replace, if you didn't
    // specify DB_DUP somewhere else...
    return (dbp->put)(dbp, NULL, &k, &d, 0) == 0 ? OK : NOTOK;
}


//*****************************************************************************
// int DB2_db::Get(String &key, String &data)
//
int
DB2_db::Get(String &key, String &data)
{
    DBT	k, d;

    memset(&k, 0, sizeof(DBT));
    memset(&d, 0, sizeof(DBT));

    k.data = key.get();
    k.size = key.length();

    int rc = dbp->get(dbp, NULL, &k, &d, 0);
    if (rc)
	return NOTOK;

    data = 0;
    data.append((char *)d.data, d.size);
    return OK;
}


//*****************************************************************************
// int DB2_db::Exists(String &key)
//
int
DB2_db::Exists(String &key)
{
    String data;

    if (!isOpen)
	return 0;

    return Get(key, data);
}


//*****************************************************************************
// int DB2_db::Delete(String &key)
//
int
DB2_db::Delete(String &key)
{
    DBT	k;

    memset(&k, 0, sizeof(DBT));

    if (!isOpen)
	return 0;

    k.data = key.get();
    k.size = key.length();

    return (dbp->del)(dbp, NULL, &k, 0);
}


//*****************************************************************************
// DB2_db *DB2_db::getDatabaseInstance()
//
DB2_db *
DB2_db::getDatabaseInstance()
{
    return new DB2_db();
}


//******************************************************************************

/*
 * db_init --
 *      Initialize the environment. Only returns a pointer
 */
DB_ENV *
DB2_db::db_init(char *home)
{
        DB_ENV *dbenv;
	char *progname = "Somewhere_deep_inside...";

        /* Rely on calloc to initialize the structure. */
        if ((dbenv = (DB_ENV *)calloc(sizeof(DB_ENV), 1)) == NULL) {
                fprintf(stderr, "%s: %s\n", progname, strerror(ENOMEM));
                exit (1);
        }
        dbenv->db_errfile = stderr;
        dbenv->db_errpfx = progname;

        if ((errno = db_appinit(home, NULL, dbenv, DB_CREATE)) != 0) {
                fprintf(stderr,
                    "%s: db_appinit: %s\n", progname, strerror(errno));
                exit (1);
        }
        return (dbenv);
}

