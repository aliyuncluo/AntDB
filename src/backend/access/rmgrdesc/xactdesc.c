/*-------------------------------------------------------------------------
 *
 * xactdesc.c
 *	  rmgr descriptor routines for access/transam/xact.c
 *
 * Portions Copyright (c) 1996-2013, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/rmgrdesc/xactdesc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/xact.h"
#include "catalog/catalog.h"
#include "common/relpath.h"
#include "storage/sinval.h"
#include "utils/timestamp.h"

#ifdef ADB
#include "access/remote_xact.h"
#endif

static void
xact_desc_commit(StringInfo buf, xl_xact_commit *xlrec)
{
	int			i;
	TransactionId *subxacts;

	subxacts = (TransactionId *) &xlrec->xnodes[xlrec->nrels];

	appendStringInfoString(buf, timestamptz_to_str(xlrec->xact_time));

	if (xlrec->nrels > 0)
	{
		appendStringInfo(buf, "; rels:");
		for (i = 0; i < xlrec->nrels; i++)
		{
			char	   *path = relpathperm(xlrec->xnodes[i], MAIN_FORKNUM);

			appendStringInfo(buf, " %s", path);
			pfree(path);
		}
	}
	if (xlrec->nsubxacts > 0)
	{
		appendStringInfo(buf, "; subxacts:");
		for (i = 0; i < xlrec->nsubxacts; i++)
			appendStringInfo(buf, " %u", subxacts[i]);
	}
	if (xlrec->nmsgs > 0)
	{
		SharedInvalidationMessage *msgs;

		msgs = (SharedInvalidationMessage *) &subxacts[xlrec->nsubxacts];

		if (XactCompletionRelcacheInitFileInval(xlrec->xinfo))
			appendStringInfo(buf, "; relcache init file inval dbid %u tsid %u",
							 xlrec->dbId, xlrec->tsId);

		appendStringInfo(buf, "; inval msgs:");
		for (i = 0; i < xlrec->nmsgs; i++)
		{
			SharedInvalidationMessage *msg = &msgs[i];

			if (msg->id >= 0)
				appendStringInfo(buf, " catcache %d", msg->id);
			else if (msg->id == SHAREDINVALCATALOG_ID)
				appendStringInfo(buf, " catalog %u", msg->cat.catId);
			else if (msg->id == SHAREDINVALRELCACHE_ID)
				appendStringInfo(buf, " relcache %u", msg->rc.relId);
			/* remaining cases not expected, but print something anyway */
			else if (msg->id == SHAREDINVALSMGR_ID)
				appendStringInfo(buf, " smgr");
			else if (msg->id == SHAREDINVALRELMAP_ID)
				appendStringInfo(buf, " relmap");
			else
				appendStringInfo(buf, " unknown id %d", msg->id);
		}
	}
}

static void
xact_desc_commit_compact(StringInfo buf, xl_xact_commit_compact *xlrec)
{
	int			i;

	appendStringInfoString(buf, timestamptz_to_str(xlrec->xact_time));

	if (xlrec->nsubxacts > 0)
	{
		appendStringInfo(buf, "; subxacts:");
		for (i = 0; i < xlrec->nsubxacts; i++)
			appendStringInfo(buf, " %u", xlrec->subxacts[i]);
	}
}

static void
xact_desc_abort(StringInfo buf, xl_xact_abort *xlrec)
{
	int			i;

	appendStringInfoString(buf, timestamptz_to_str(xlrec->xact_time));
	if (xlrec->nrels > 0)
	{
		appendStringInfo(buf, "; rels:");
		for (i = 0; i < xlrec->nrels; i++)
		{
			char	   *path = relpathperm(xlrec->xnodes[i], MAIN_FORKNUM);

			appendStringInfo(buf, " %s", path);
			pfree(path);
		}
	}
	if (xlrec->nsubxacts > 0)
	{
		TransactionId *xacts = (TransactionId *)
		&xlrec->xnodes[xlrec->nrels];

		appendStringInfo(buf, "; subxacts:");
		for (i = 0; i < xlrec->nsubxacts; i++)
			appendStringInfo(buf, " %u", xacts[i]);
	}
}

static void
xact_desc_assignment(StringInfo buf, xl_xact_assignment *xlrec)
{
	int			i;

	appendStringInfo(buf, "subxacts:");

	for (i = 0; i < xlrec->nsubxacts; i++)
		appendStringInfo(buf, " %u", xlrec->xsub[i]);
}

#ifdef ADB
static void
remote_xact_desc(StringInfo buf, uint8 xl_info, xl_remote_xact *xlrec)
{
	switch (xl_info)
	{
		case XLOG_RXACT_PREPARE:
			appendStringInfo(buf, "remote prepare %u:", xlrec->xid);
			appendStringInfo(buf, "; prepared gid: '%s'", xlrec->gid);
			break;
		case XLOG_RXACT_PREPARE_SUCCESS:
			appendStringInfo(buf, "remote prepare success %u:", xlrec->xid);
			appendStringInfo(buf, "; prepared gid: '%s'", xlrec->gid);
			break;
		case XLOG_RXACT_COMMIT:
			appendStringInfo(buf, "remote commit %u:", xlrec->xid);
			break;
		case XLOG_RXACT_COMMIT_PREPARED:
			appendStringInfo(buf, "remote commit prepared %u:", xlrec->xid);
			appendStringInfo(buf, "; prepared gid: '%s'", xlrec->gid);
			break;
		case XLOG_RXACT_COMMIT_PREPARED_SUCCESS:
			appendStringInfo(buf, "remote commit prepared success %u:", xlrec->xid);
			appendStringInfo(buf, "; prepared gid: '%s'", xlrec->gid);
			break;
		case XLOG_RXACT_ABORT:
			appendStringInfo(buf, "remote abort %u:", xlrec->xid);
			break;
		case XLOG_RXACT_ABORT_PREPARED:
			appendStringInfo(buf, "remote abort prepared %u:", xlrec->xid);
			appendStringInfo(buf, "; prepared gid: '%s'", xlrec->gid);
			break;
		case XLOG_RXACT_ABORT_PREPARED_SUCCESS:
			appendStringInfo(buf, "remote abort prepared success %u:", xlrec->xid);
			appendStringInfo(buf, "; prepared gid: '%s'", xlrec->gid);
			break;
		default:
			Assert(0);
			break;
	}
	appendStringInfo(buf, "; database: %s", xlrec->dbname);
	appendStringInfo(buf, "; xact time: %s", timestamptz_to_str(xlrec->xact_time));
	appendStringInfo(buf, "; implicit: %s", xlrec->implicit ? "yes" : "no");
	appendStringInfo(buf, "; involved remote nodes: %d", xlrec->nnodes);	
}
#endif

void
xact_desc(StringInfo buf, uint8 xl_info, char *rec)
{
	uint8		info = xl_info & ~XLR_INFO_MASK;

	if (info == XLOG_XACT_COMMIT_COMPACT)
	{
		xl_xact_commit_compact *xlrec = (xl_xact_commit_compact *) rec;

		appendStringInfo(buf, "commit: ");
		xact_desc_commit_compact(buf, xlrec);
	}
	else if (info == XLOG_XACT_COMMIT)
	{
		xl_xact_commit *xlrec = (xl_xact_commit *) rec;

		appendStringInfo(buf, "commit: ");
		xact_desc_commit(buf, xlrec);
	}
	else if (info == XLOG_XACT_ABORT)
	{
		xl_xact_abort *xlrec = (xl_xact_abort *) rec;

		appendStringInfo(buf, "abort: ");
		xact_desc_abort(buf, xlrec);
	}
	else if (info == XLOG_XACT_PREPARE)
	{
		appendStringInfo(buf, "prepare");
	}
	else if (info == XLOG_XACT_COMMIT_PREPARED)
	{
		xl_xact_commit_prepared *xlrec = (xl_xact_commit_prepared *) rec;

		appendStringInfo(buf, "commit prepared %u: ", xlrec->xid);
		xact_desc_commit(buf, &xlrec->crec);
	}
	else if (info == XLOG_XACT_ABORT_PREPARED)
	{
		xl_xact_abort_prepared *xlrec = (xl_xact_abort_prepared *) rec;

		appendStringInfo(buf, "abort prepared %u: ", xlrec->xid);
		xact_desc_abort(buf, &xlrec->arec);
	}
	else if (info == XLOG_XACT_ASSIGNMENT)
	{
		xl_xact_assignment *xlrec = (xl_xact_assignment *) rec;

		/*
		 * Note that we ignore the WAL record's xid, since we're more
		 * interested in the top-level xid that issued the record and which
		 * xids are being reported here.
		 */
		appendStringInfo(buf, "xid assignment xtop %u: ", xlrec->xtop);
		xact_desc_assignment(buf, xlrec);
	}
#ifdef ADB
	else if (info == XLOG_RXACT_PREPARE ||
			 info == XLOG_RXACT_PREPARE_SUCCESS ||
			 info == XLOG_RXACT_COMMIT ||
			 info == XLOG_RXACT_COMMIT_PREPARED ||
			 info == XLOG_RXACT_COMMIT_PREPARED_SUCCESS ||
			 info == XLOG_RXACT_ABORT ||
			 info == XLOG_RXACT_ABORT_PREPARED ||
			 info == XLOG_RXACT_ABORT_PREPARED_SUCCESS)
	{
		xl_remote_xact *xlrec = (xl_remote_xact *) rec;
		remote_xact_desc(buf, info, xlrec);
	}
#endif
	else
		appendStringInfo(buf, "UNKNOWN");
}
