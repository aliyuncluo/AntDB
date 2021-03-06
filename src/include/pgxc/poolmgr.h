/*-------------------------------------------------------------------------
 *
 * poolmgr.h
 *
 *	  Definitions for the Datanode connection pool.
 *
 *
 * Portions Copyright (c) 1996-2011, PostgreSQL Global Development Group
 * Portions Copyright (c) 2010-2012, Postgres-XC Development Group
 *
 * src/include/pgxc/poolmgr.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef POOLMGR_H
#define POOLMGR_H

#include "nodes/nodes.h"
#include "pgxcnode.h"
#include "poolcomm.h"
#include "storage/pmsignal.h"
#include "utils/hsearch.h"


/*
 * List of flags related to pooler connection clean up when disconnecting
 * a session or relaeasing handles.
 * When Local SET commands (POOL_CMD_LOCAL_SET) are used, local parameter
 * string is cleaned by the node commit itself.
 * When global SET commands (POOL_CMD_GLOBAL_SET) are used, "RESET ALL"
 * command is sent down to activated nodes to at session end. At the end
 * of a transaction, connections using global SET commands are not sent
 * back to pool.
 * When temporary object commands are used (POOL_CMD_TEMP), "DISCARD ALL"
 * query is sent down to nodes whose connection is activated at the end of
 * a session.
 * At the end of a transaction, a session using either temporary objects
 * or global session parameters has its connections not sent back to pool.
 *
 * Local parameters are used to change within current transaction block.
 * They are sent to remote nodes invloved in the transaction after sending
 * BEGIN TRANSACTION using a special firing protocol.
 * They cannot be sent when connections are obtained, making them having no
 * effect as BEGIN is sent by backend after connections are obtained and
 * obtention confirmation has been sent back to backend.
 * SET CONSTRAINT, SET LOCAL commands are in this category.
 *
 * Global parmeters are used to change the behavior of current session.
 * They are sent to the nodes when the connections are obtained.
 * SET GLOBAL, general SET commands are in this category.
 */
typedef enum
{
	POOL_CMD_TEMP,		/* Temporary object flag */
	POOL_CMD_LOCAL_SET,	/* Local SET flag, current transaction block only */
	POOL_CMD_GLOBAL_SET	/* Global SET flag */
} PoolCommandType;

typedef struct PoolHandle PoolHandle;

extern int	MinPoolSize;
extern int	MaxPoolSize;
extern int	PoolRemoteCmdTimeout;

extern bool PersistentConnections;

/* Status inquiry functions */
extern void PGXCPoolerProcessIam(void);
extern bool IsPGXCPoolerProcess(void);

/* Initialize internal structures */
extern int	PoolManagerInit(void) __attribute__((noreturn));

/* Destroy internal structures */
extern int	PoolManagerDestroy(void);

/*
 * Get handle to pool manager. This function should be called just before
 * forking off new session. It creates PoolHandle, PoolAgent and a pipe between
 * them. PoolAgent is stored within Postmaster's memory context and Session
 * closes it later. PoolHandle is returned and should be store in a local
 * variable. After forking off it can be stored in global memory, so it will
 * only be accessible by the process running the session.
 */
extern PoolHandle *GetPoolManagerHandle(void);

/*
 * Called from Postmaster(Coordinator) after fork. Close one end of the pipe and
 * free memory occupied by PoolHandler
 */
extern void PoolManagerCloseHandle(PoolHandle *handle);

/*
 * Gracefully close connection to the PoolManager
 */
extern void PoolManagerDisconnect(void);

extern char *session_options(void);

/*
 * Called from Session process after fork(). Associate handle with session
 * for subsequent calls. Associate session with specified database and
 * initialize respective connection pool
 */
extern void PoolManagerConnect(PoolHandle *handle,
	                           const char *database, const char *user_name,
	                           const char *pgoptions);

/*
 * Reconnect to pool manager
 * This simply does a disconnection followed by a reconnection.
 */
extern void PoolManagerReconnect(void);

/*
 * Save a SET command in Pooler.
 * This command is run on existent agent connections
 * and stored in pooler agent to be replayed when new connections
 * are requested.
 */
extern int PoolManagerSetCommand(PoolCommandType command_type, const char *set_command);

/* Get pooled connections */
extern pgsocket *PoolManagerGetConnectionsOid(List *oidlist);

/* Clean pool connections */
extern void PoolManagerCleanConnectionOid(List *oidlist, const char *dbname, const char *username);

/* Check consistency of connection information cached in pooler with catalogs */
#define PoolManagerCheckConnectionInfo()	true

/* Reload connection data in pooler and drop all the existing connections of pooler */
#define PoolManagerReloadConnectionInfo()	((void)0)

/* Send Abort signal to transactions being run */
extern int	PoolManagerAbortTransactions(char *dbname, char *username, int **proc_pids);

/* Return connections back to the pool, for both Coordinator and Datanode connections */
extern void PoolManagerReleaseConnections(bool force_close);

/* Check if pool has a handle */
extern bool IsPoolHandle(void);

/* Send commands to alter the behavior of current transaction */
extern int PoolManagerSendLocalCommand(int dn_count, int* dn_list, int co_count, int* co_list);

extern Datum pool_close_idle_conn(PG_FUNCTION_ARGS);

#endif
