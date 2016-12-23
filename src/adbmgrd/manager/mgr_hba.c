/*
 * commands of hba
 */


#include "postgres.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/htup.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/mgr_host.h"
#include "catalog/mgr_cndnnode.h"
#include "catalog/mgr_updateparm.h"
#include "catalog/pg_type.h"
#include "catalog/pg_collation.h"
#include "commands/defrem.h"
#include "fmgr.h"
#include "mgr/mgr_cmds.h"
#include "mgr/mgr_agent.h"
#include "mgr/mgr_msg_type.h"
#include "miscadmin.h"
#include "nodes/parsenodes.h"
#include "parser/mgr_node.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/memutils.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/tqual.h"
#include "funcapi.h"
#include "utils/lsyscache.h"
#include "../../interfaces/libpq/libpq-fe.h"
#include "../../bin/agent/hba_scan.h"
 
#include "catalog/mgr_hba.h"
#include "utils/formatting.h"
 
#define SPACE          				" "
#define INVALID_ID					0x7fffffff
#define HBA_RESULT_COLUMN  			3
 

typedef enum OperateHbaType
{
	HANDLE_NO = 0,	/*don't do any handle for drop table hba*/
	HBA_ALL=1,		/*drop table hba all*/
	HBA_NODENAME_ALL,/*drop table hba all base on nodename*/
	HBA_NODENAME_ID  /*drop table hba all base on row_id and nodename*/
}OperateHbaType;

typedef struct TableInfo
{
	Relation rel;
	HeapScanDesc rel_scan;
	ListCell  **lcp;
}TableInfo;

#if (Natts_mgr_hba != 3)
#error "need change hba code"
#endif
static TupleDesc common_command_tuple_desc = NULL;

/*--------------------------------------------------------------------*/
	
extern void mgr_reload_conf(Oid hostoid, char *nodepath);
/*--------------------------------------------------------------------*/
static void mgr_add_hba_all(List *args_list, GetAgentCmdRst *getAgentCmdRst);
static void mgr_add_hba_one(char *coord_name, List *args_list, GetAgentCmdRst *getAgentCmdRst);
static bool drop_hba_all(GetAgentCmdRst *getAgentCmdRst);
static bool drop_hba_nodename_all(char *coord_name, GetAgentCmdRst *getAgentCmdRst);
static bool drop_hba_nodename_id(char *coord_name, uint32 row_id, GetAgentCmdRst *getAgentCmdRst);

static uint32 get_hba_max_id(void);
static Oid tuple_insert_table_hba(Datum *values, bool *isnull);
static HbaType get_connect_type(char *str_type);
static TupleDesc get_tuple_desc_for_hba(void);
static HeapTuple tuple_form_table_hba(const uint32 row_id, const Name node_name, const char * values);
static void delete_table_hba(uint32 row_id, char *coord_name);
static bool check_hba_tuple_exist(uint32 row_id, char *coord_name, char *values);
static bool IDIsValid(uint32 row_id);
/*--------------------------------------------------------------------*/

Datum mgr_alter_hba(PG_FUNCTION_ARGS)
{
	HeapTuple tup_result;
	char *coord_name = "coord1";
	tup_result = tuple_form_table_hba(21, (Name)coord_name, "success");
	return HeapTupleGetDatum(tup_result);
}
Datum mgr_list_hba_by_name(PG_FUNCTION_ARGS)
{
	List *nodenamelist;
	HeapTuple tup_result;
	HeapTuple tuple;
	Form_mgr_hba mgr_hba;
	ScanKeyData key[1];
	FuncCallContext *funcctx;
	TableInfo *info;
	char *nodestrname;
	
	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		nodenamelist = NIL;
		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();
		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		/* allocate memory for user context */
		info = palloc(sizeof(*info));
		info->lcp = (ListCell **) palloc(sizeof(ListCell *));
		if(!PG_ARGISNULL(0))
		{
			#ifdef ADB
				nodenamelist = get_fcinfo_namelist("", 0, fcinfo, NULL);
			#else
				nodenamelist = get_fcinfo_namelist("", 0, fcinfo);
			#endif
		}
		*(info->lcp) = list_head(nodenamelist);
		nodestrname = (char *) lfirst(*(info->lcp));
		ScanKeyInit(&key[0]
				,Anum_mgr_hba_nodename
				,BTEqualStrategyNumber
				,F_NAMEEQ
				,CStringGetDatum(nodestrname));
		info->rel = heap_open(HbaRelationId, AccessShareLock);
		info->rel_scan = heap_beginscan(info->rel, SnapshotNow, 1, key);
		
		funcctx->user_fctx = info;
		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	Assert(funcctx);
	info = funcctx->user_fctx;
	Assert(info);
	tuple = heap_getnext(info->rel_scan, ForwardScanDirection);
	if(!PointerIsValid(tuple))
	{
		heap_endscan(info->rel_scan);
		heap_close(info->rel, AccessShareLock);
		pfree(info);
		SRF_RETURN_DONE(funcctx);
	}
	else
	{
		mgr_hba = (Form_mgr_hba)GETSTRUCT(tuple);
		Assert(mgr_hba);
		tup_result = tuple_form_table_hba(mgr_hba->row_id, &(mgr_hba->nodename), TextDatumGetCString(&(mgr_hba->hbavalue)));
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tup_result));
	}
}
Datum mgr_add_hba(PG_FUNCTION_ARGS)
{
	GetAgentCmdRst getAgentCmdRst;
	OperateHbaType handle_type = HANDLE_NO;
	HeapTuple tup_result;
	List *args_list;
	char *coord_name;
	getAgentCmdRst.ret = true;
	initStringInfo(&getAgentCmdRst.description);
	/*step 1: parase args,and get nodename,hba values;
	if nodename equal '*', add to all the coordinator*/
	if(PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("args is null")));
	}
	#ifdef ADB
		args_list = get_fcinfo_namelist("", 0, fcinfo, NULL);
	#else
		args_list = get_fcinfo_namelist("", 0, fcinfo);
	#endif
	if(args_list->length < 2)
	{
		ereport(ERROR, (errmsg("args is not enough")));
	}	
	coord_name = llast(args_list);
	if(strcmp(coord_name,"*") == 0) 
		handle_type = HBA_ALL;         
	else
		handle_type = HBA_NODENAME_ALL;
	/*step 2: operate add hba comamnd*/	
	args_list = list_delete(args_list, llast(args_list)); /*remove nodename from list*/
	if(HBA_ALL == handle_type)/*add to all coordinator*/
	{
		mgr_add_hba_all(args_list, &getAgentCmdRst);
	}	
	else if(HBA_NODENAME_ALL == handle_type)/*add to specified coordinator*/
	{
		mgr_add_hba_one(coord_name, args_list, &getAgentCmdRst);
	}
	
	/*step 4: show the state of operating drop hba commands */
	tup_result = tuple_form_table_hba(INVALID_ID
									,(Name)coord_name
									,true == getAgentCmdRst.ret ? "success" : getAgentCmdRst.description.data);
	pfree(getAgentCmdRst.description.data);
	return HeapTupleGetDatum(tup_result);
}
static void mgr_add_hba_all(List *args_list, GetAgentCmdRst *getAgentCmdRst)
{
	Relation rel;
	HeapScanDesc rel_scan;
	Form_mgr_node mgr_node;
	HeapTuple tuple;
	ScanKeyData key[1];
	char *coord_name;
	
	ScanKeyInit(&key[0],
			Anum_mgr_node_nodetype
			,BTEqualStrategyNumber
			,F_CHAREQ
			,CharGetDatum(CNDN_TYPE_COORDINATOR_MASTER));
		
	rel = heap_open(NodeRelationId, RowExclusiveLock);
	rel_scan = heap_beginscan(rel, SnapshotNow, 1, key);
	while((tuple = heap_getnext(rel_scan, ForwardScanDirection)) != NULL)
	{
		mgr_node = (Form_mgr_node)GETSTRUCT(tuple);
		Assert(mgr_node);
		coord_name = NameStr(mgr_node->nodename);
		mgr_add_hba_one(coord_name, args_list, getAgentCmdRst);
	}
	
	heap_endscan(rel_scan);
	heap_close(rel, RowExclusiveLock);
}
static void mgr_add_hba_one(char *coord_name, List *args_list, GetAgentCmdRst *getAgentCmdRst)
{
	Relation rel;
	HeapTuple tuple;
	Form_mgr_node mgr_node;
	char *host_address;
	char * node_path;
	Datum datumPath;
	Oid hostoid;
	ListCell *lc;
	List *list_elem = NIL;
	char *str_elem, *str, *str_remain;
	StringInfoData infosendmsg;
	StringInfoData hbainfomsg;	
	bool isNull = false;
	uint32 hba_max_id = 0;
	HbaType conntype = HBA_TYPE_EMPTY;
	bool is_exist = false;
	Datum datum[Natts_mgr_node];
	bool isnull[Natts_mgr_node];
	memset(datum, 0, sizeof(datum));
	memset(isnull, 0, sizeof(isnull));
	
	Assert(coord_name);
	Assert(args_list);
	
	/*step1: check the nodename is exist in the mgr_node table and make sure it has been initialized*/
	rel = heap_open(NodeRelationId, AccessShareLock);
	tuple = mgr_get_tuple_node_from_name_type(rel, coord_name, CNDN_TYPE_COORDINATOR_MASTER);
	if(!(HeapTupleIsValid(tuple)))
	{
		 ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
				 ,errmsg("coordinator\"%s\" does not exist", coord_name)));
	}
    mgr_node = (Form_mgr_node)GETSTRUCT(tuple);
	Assert(mgr_node);	
	if(false == mgr_node->nodeinited)	
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
				 ,errmsg("coordinator\"%s\" does not init", coord_name)));
	}
	hostoid = mgr_node->nodehost;
	host_address = get_hostaddress_from_hostoid(hostoid);
	datumPath = heap_getattr(tuple, Anum_mgr_node_nodepath, RelationGetDescr(rel), &isNull);
	if(isNull)
	{
		 ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
				 ,errmsg("coordinator\"%s\" does not exist nodepath", coord_name)));
	}
	node_path = TextDatumGetCString(datumPath);
	heap_close(rel, AccessShareLock);
	heap_freetuple(tuple);
	
	/*step2: parser the hba values and check whether it's valid*/
	initStringInfo(&infosendmsg);/*send to agent*/
	initStringInfo(&hbainfomsg);/*add to hba table*/
	foreach(lc, args_list)
	{
		resetStringInfo(&hbainfomsg);
		str = lfirst(lc);
		str_elem = strtok_r(str, SPACE, &str_remain);
		conntype = get_connect_type(str_elem);
		if(conntype == HBA_TYPE_EMPTY)
		{
			ereport(ERROR, (errmsg("%s", "the type of hba is wrong")));
		}
		appendStringInfo(&infosendmsg, "%c%c", conntype, '\0');	
		appendStringInfo(&hbainfomsg, "%s  ",str_elem);
		while(str_elem != NULL)
		{
			str_elem = strtok_r(NULL, SPACE, &str_remain);
			if(PointerIsValid(str_elem))
			{
				appendStringInfo(&infosendmsg,"%s%c", str_elem, '\0');	
				appendStringInfo(&hbainfomsg, "%s  ",str_elem);
			}						
		}	
		/*check the value whether exist in the hba table*/
		is_exist = check_hba_tuple_exist(INVALID_ID, coord_name, hbainfomsg.data);
		if(is_exist)
		{
			appendStringInfo(&getAgentCmdRst->description, "nodename %s with values \"%s\" has been existed.\n", coord_name, hbainfomsg.data);
			continue;
		}
		str_elem = palloc0(hbainfomsg.len + 1);
		memcpy(str_elem, hbainfomsg.data, hbainfomsg.len);
		list_elem = lappend(list_elem, str_elem);/*add to list and preprer to add to hbatable*/
	}
     
	/*step3: send msg to the specified coordinator to update datanode master's pg_hba.conf*/	
	mgr_send_conf_parameters(AGT_CMD_CNDN_REFRESH_PGHBACONF
							,node_path
							,&infosendmsg
							,hostoid
							,getAgentCmdRst);
	if (!getAgentCmdRst->ret)
		ereport(ERROR, (errmsg("%s\n", getAgentCmdRst->description.data)));		

	/*step4: execute pgxc_ctl reload to the specified host, 
	it's to take effect for the new value in the pg_hba.conf  */
	mgr_reload_conf(hostoid, node_path);	
	
	/*step5: add a new tuple to hba table */
	hba_max_id = get_hba_max_id();
	foreach(lc, list_elem)
	{
		str_elem = lfirst(lc);
		hba_max_id = hba_max_id + 1;
		datum[Anum_mgr_hba_id - 1] = Int32GetDatum(hba_max_id);
		datum[Anum_mgr_hba_nodename - 1] = NameGetDatum(coord_name);
		datum[Anum_mgr_hba_value - 1] = CStringGetTextDatum(str_elem);
		tuple_insert_table_hba(datum, isnull);
	}
	
	/*step7: Release an allocated chunk*/
	foreach(lc, list_elem)
	{
		pfree(lfirst(lc));
	}
	list_free(list_elem);
	pfree(hbainfomsg.data);
	pfree(infosendmsg.data);		
}

static uint32 get_hba_max_id(void)
{
	Relation rel;
	HeapTuple tuple;
	HeapScanDesc rel_scan;
	Form_mgr_hba mgr_hba;
	uint32 max_id = 0;
	uint32 temp_num = 0;
	rel = heap_open(HbaRelationId, AccessShareLock);
	rel_scan = heap_beginscan(rel, SnapshotNow, 0, NULL);
	while((tuple = heap_getnext(rel_scan, ForwardScanDirection)) != NULL)
	{
		mgr_hba = (Form_mgr_hba)GETSTRUCT(tuple);
		Assert(mgr_hba);
		temp_num = mgr_hba->row_id;
		if(max_id < temp_num)
			max_id = temp_num;
	}
	heap_endscan(rel_scan);
	heap_close(rel, AccessShareLock);
	if(max_id >= INVALID_ID)
		ereport(ERROR, (errmsg("the row_id of hba has been used up")));
	
	return max_id;
}

static Oid tuple_insert_table_hba(Datum *values, bool *isnull)
{
	Relation rel;
	HeapTuple newtuple;
	Oid hba_oid;
	Assert(values && isnull);
	/* now, we can insert record */
	rel = heap_open(HbaRelationId, RowExclusiveLock);	

	newtuple = heap_form_tuple(RelationGetDescr(rel), values, isnull);
	hba_oid = simple_heap_insert(rel, newtuple);
	CatalogUpdateIndexes(rel, newtuple);
	heap_close(rel, RowExclusiveLock);
	heap_freetuple(newtuple);
	return hba_oid;
}

static TupleDesc get_tuple_desc_for_hba(void)
{
    if(common_command_tuple_desc == NULL)
    {
        MemoryContext volatile old_context = MemoryContextSwitchTo(TopMemoryContext);
        TupleDesc volatile desc = NULL;
        PG_TRY();
        {
            desc = CreateTemplateTupleDesc(HBA_RESULT_COLUMN, false);
            TupleDescInitEntry(desc, (AttrNumber) 1, "row_id",
                               INT4OID, -1, 0);
            TupleDescInitEntry(desc, (AttrNumber) 2, "nodename",
                               NAMEOID, -1, 0);
            TupleDescInitEntry(desc, (AttrNumber) 3, "values",
                               TEXTOID, -1, 0);
            common_command_tuple_desc = BlessTupleDesc(desc);
        }PG_CATCH();
        {
            if(desc)
                FreeTupleDesc(desc);
            PG_RE_THROW();
        }PG_END_TRY();
        (void)MemoryContextSwitchTo(old_context);
    }
    Assert(common_command_tuple_desc);
    return common_command_tuple_desc;
}

static HeapTuple tuple_form_table_hba(const uint32 row_id, const Name node_name, const char * values)
{
    Datum datums[HBA_RESULT_COLUMN];
    bool nulls[HBA_RESULT_COLUMN];
    TupleDesc desc;
    AssertArg(node_name && values);
	memset(nulls, false, HBA_RESULT_COLUMN);
    desc = get_tuple_desc_for_hba();

    AssertArg(desc && desc->natts == HBA_RESULT_COLUMN
        && desc->attrs[0]->atttypid == INT4OID
        && desc->attrs[1]->atttypid == NAMEOID
        && desc->attrs[2]->atttypid == TEXTOID);
	datums[0] = Int32GetDatum(row_id);   
    datums[1] = NameGetDatum(node_name);
    datums[2] = CStringGetTextDatum(values);
    return heap_form_tuple(desc, datums, nulls);
}

Datum mgr_drop_hba(PG_FUNCTION_ARGS)
{
	GetAgentCmdRst getAgentCmdRst;
	OperateHbaType handle_type = HANDLE_NO;
	HeapTuple tup_result;
	uint32 row_id = INVALID_ID;
	List *args_list;
	char *coord_name;
	char *num_str;
	bool ret = true;
	getAgentCmdRst.ret = true;
	/*step 1: parase args,and get nodename, row_id;if nodename equal '*', delete all*/
	if(PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("args is null")));
	}
	#ifdef ADB
		args_list = get_fcinfo_namelist("", 0, fcinfo, NULL);
	#else
		args_list = get_fcinfo_namelist("", 0, fcinfo);
	#endif

	coord_name = linitial(args_list);
	if(args_list->length == 1)
	{
		if(strcmp(coord_name,"*") == 0)
			handle_type = HBA_ALL;
		else
			handle_type = HBA_NODENAME_ALL;
	}
	else
	{
		num_str = lsecond(args_list);
		row_id = pg_atoi(num_str,sizeof(uint32),0);
		handle_type = HBA_NODENAME_ID;
	}
	/*step 2: check the tuple is exist*/
	if(HBA_NODENAME_ALL == handle_type)
	{
		ret = check_hba_tuple_exist(INVALID_ID, coord_name, NULL);
		if(false == ret)
		{
			 ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
					 ,errmsg("nodename\"%s\"does not exist", coord_name))); 
		}
	}	
	else if(HBA_NODENAME_ID == handle_type)
	{
		ret = check_hba_tuple_exist(row_id, coord_name, NULL);
		if(false == ret)
		{
			 ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
					 ,errmsg("nodename \"%s\" with row_id %d does not exist", coord_name, row_id))); 
		}
	}
			
	/*step 3: operating drop table hba  according to the handle_type*/
		/*send drop msg to the agent to delete content of pg_hba.conf */
	initStringInfo(&(getAgentCmdRst.description));
	switch(handle_type)
	{
		case HBA_ALL: ret = drop_hba_all(&getAgentCmdRst);
			break;
		case HBA_NODENAME_ALL: ret = drop_hba_nodename_all(coord_name, &getAgentCmdRst);
			break;
		case HBA_NODENAME_ID:ret = drop_hba_nodename_id(coord_name, row_id, &getAgentCmdRst);
			break;
		default:ereport(ERROR, (errmsg("operating drop table hba")));
			break;		
	}
	/*delete tuple of hba table*/
	if(true == ret && true == getAgentCmdRst.ret)
	{
		switch(handle_type)
		{
			case HBA_ALL: delete_table_hba(INVALID_ID, "*");
				break;
			case HBA_NODENAME_ALL: delete_table_hba(INVALID_ID, coord_name);
				break;
			case HBA_NODENAME_ID: delete_table_hba(row_id, coord_name);
				break;
			default:ereport(ERROR, (errmsg("operating drop table hba")));
			break;
		}		
	}
	/*step 4: show the state of operating drop hba commands */
	tup_result = tuple_form_table_hba(row_id
									,(Name)coord_name
									,ret == true ? "success" : getAgentCmdRst.description.data);
	return HeapTupleGetDatum(tup_result);
}

static bool drop_hba_all(GetAgentCmdRst *getAgentCmdRst)
{	
	Relation rel;
	ScanKeyData key[1];
	HeapTuple tuple;
	HeapScanDesc  rel_scan;
	Form_mgr_node mgr_node;	
	char *coord_name;
	bool ret = true;
	/*Traverse all the coordinator in the node table*/
	ScanKeyInit(&key[0]
				,Anum_mgr_node_nodetype
				,BTEqualStrategyNumber
				,F_CHAREQ
				,CharGetDatum(CNDN_TYPE_COORDINATOR_MASTER));
	
	rel = heap_open(NodeRelationId, AccessShareLock);
	rel_scan = heap_beginscan(rel, SnapshotNow, 1, key);
	while((tuple = heap_getnext(rel_scan, ForwardScanDirection)) != NULL)
	{
		mgr_node = (Form_mgr_node)GETSTRUCT(tuple);
		Assert(mgr_node);
		coord_name = NameStr(mgr_node->nodename);
		ret = drop_hba_nodename_all(coord_name, getAgentCmdRst);	
		if(ret == false)
		{
			break;
		}
	}
	heap_endscan(rel_scan);
	heap_close(rel, AccessShareLock);

	return ret;
}
/*
	delete one row form hba talbe base on nodename,
	to delete the nodename which you want.
*/	
static bool drop_hba_nodename_all(char *coord_name, GetAgentCmdRst *getAgentCmdRst)
{	
	Relation rel;
	ScanKeyData key[1];
	HeapTuple tuple;
	HeapScanDesc  rel_scan;
	Form_mgr_hba mgr_hba;	
	uint32 row_id;
	bool ret = true;
	Assert(coord_name);
	
	ScanKeyInit(&key[0]
				,Anum_mgr_hba_nodename
				,BTEqualStrategyNumber
				,F_NAMEEQ
				,CStringGetDatum(coord_name));
	
	rel = heap_open(HbaRelationId, AccessShareLock);
	rel_scan = heap_beginscan(rel, SnapshotNow, 1, key);
	while((tuple = heap_getnext(rel_scan, ForwardScanDirection)) != NULL)
	{
		mgr_hba = (Form_mgr_hba)GETSTRUCT(tuple);
		Assert(mgr_hba);
		row_id = mgr_hba->row_id;
		ret = drop_hba_nodename_id(coord_name, row_id, getAgentCmdRst);
		if(ret == false)
		{
			break;
		}
	}
	heap_endscan(rel_scan);
	heap_close(rel, AccessShareLock);
	
	return ret;
}
/*
	delete one row form hba talbe base on nodename and row_id,
	if success return true,else ruturn false;
*/	
static bool drop_hba_nodename_id(char *coord_name, uint32 row_id, GetAgentCmdRst *getAgentCmdRst)
{	
	Relation rel;
	HeapScanDesc rel_scan;
	HeapTuple tuple;
	Form_mgr_node mgr_node;
	Form_mgr_hba mgr_hba;
	char *host_address;
	char * coord_path;
	char * hba_values = NULL;
	Datum datumPath;
	Oid hostoid;
	char *str_elem, *str_remain;
	StringInfoData infosendmsg;
	bool isNull = false;
	HbaType conntype = HBA_TYPE_EMPTY;
	ScanKeyData key[2];
	bool ret = true;
	Assert(coord_name);
	
	/*step1: get hba value to delete*/
	ScanKeyInit(&key[0]
				,Anum_mgr_hba_id
				,BTEqualStrategyNumber
				,F_INT4EQ
				,Int32GetDatum(row_id));
	ScanKeyInit(&key[1]
				,Anum_mgr_hba_nodename
				,BTEqualStrategyNumber
				,F_NAMEEQ
				,CStringGetDatum(coord_name));
	rel = heap_open(HbaRelationId, AccessShareLock);
	rel_scan = heap_beginscan(rel, SnapshotNow, 2, key);		
	while((tuple = heap_getnext(rel_scan, ForwardScanDirection)) != NULL)
	{
		mgr_hba = (Form_mgr_hba)GETSTRUCT(tuple);
		Assert(mgr_hba);
		hba_values = TextDatumGetCString(&(mgr_hba->hbavalue));
		break;
	}
	if(!PointerIsValid(hba_values))
	{
		appendStringInfo(&getAgentCmdRst->description, "nodename \"%s\" with row_id %d does not exist", coord_name, row_id);
		getAgentCmdRst->ret = false;
	}	
	heap_endscan(rel_scan);
	heap_close(rel, AccessShareLock);	
	
	/*step2: check the nodename is exist in the mgr_node and make sure it has been initialized*/
	rel = heap_open(NodeRelationId, AccessShareLock);
	tuple = mgr_get_tuple_node_from_name_type(rel, coord_name, CNDN_TYPE_COORDINATOR_MASTER);
	if(!(HeapTupleIsValid(tuple)))
	{
		 ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
				 ,errmsg("coordinator\"%s\" does not exist", coord_name)));
	}
    mgr_node = (Form_mgr_node)GETSTRUCT(tuple);
	Assert(mgr_node);	
	if(false == mgr_node->nodeinited)	
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
				 ,errmsg("coordinator\"%s\" does not init", coord_name)));
	}
	hostoid = mgr_node->nodehost;
	host_address = get_hostaddress_from_hostoid(hostoid);
	datumPath = heap_getattr(tuple, Anum_mgr_node_nodepath, RelationGetDescr(rel), &isNull);
	if(isNull)
	{
		 ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT)
				 ,errmsg("coordinator\"%s\" does not exist nodepath", coord_name)));
	}
	coord_path = TextDatumGetCString(datumPath);
	heap_close(rel, AccessShareLock);
	heap_freetuple(tuple);
	
	/*step3: parase the discovered hba values and encode it*/
	initStringInfo(&infosendmsg);
	str_elem = strtok_r(hba_values, SPACE, &str_remain);
	conntype = get_connect_type(str_elem);
	if(conntype == HBA_TYPE_EMPTY)
	{
		ereport(ERROR, (errmsg("%s", "the type of hba is wrong")));
	}
	appendStringInfo(&infosendmsg, "%c%c", conntype, '\0');	
	while(str_elem != NULL)
	{
		str_elem = strtok_r(NULL, SPACE, &str_remain);
		if(PointerIsValid(str_elem))
		{
			appendStringInfo(&infosendmsg,"%s%c", str_elem, '\0');	
		}						
	}	
	/*step4: send msg to the specified coordinator to update datanode master's pg_hba.conf*/	
	mgr_send_conf_parameters(AGT_CMD_CNDN_DELETE_PGHBACONF,
							coord_path,
							&infosendmsg,
							hostoid,
							getAgentCmdRst);
	if (!getAgentCmdRst->ret)
		ereport(ERROR, (errmsg("%s", getAgentCmdRst->description.data)));
   		
	/*step4: execute pgxc_ctl reload to the specified host */
	mgr_reload_conf(hostoid, coord_path);	

	pfree(infosendmsg.data);
	return ret;
}

/*
if row_id is 0 or 0x7fffffff ,the param will be as invalid,
we will to check coord_name,
coord_name = "*" we delete all the hba table;
else if coord_name is valid ,we will delete the table according to it;
*/
static void delete_table_hba(uint32 row_id, char *coord_name)
{
	Relation rel;
	HeapScanDesc rel_scan;
	HeapTuple tuple;
	ScanKeyData scankey[2];	
	Assert(coord_name);
	
	ScanKeyInit(&scankey[0]
				,Anum_mgr_hba_id
				,BTEqualStrategyNumber
				,F_INT4EQ
				,Int32GetDatum(row_id));
	ScanKeyInit(&scankey[1]
				,Anum_mgr_hba_nodename
				,BTEqualStrategyNumber
				,F_NAMEEQ
				,CStringGetDatum(coord_name));
			
	rel = heap_open(HbaRelationId, RowExclusiveLock);
	if(strcmp(coord_name, "*") == 0)
		rel_scan = heap_beginscan(rel, SnapshotNow, 0, NULL);
	else /*if(PointerIsValid(coord_name))*/
	{
		if(IDIsValid(row_id))
			rel_scan = heap_beginscan(rel, SnapshotNow, 2, scankey);
		else
			rel_scan = heap_beginscan(rel, SnapshotNow, 1, &scankey[1]);
	}	
	while((tuple = heap_getnext(rel_scan, ForwardScanDirection)) != NULL)
	{
		simple_heap_delete(rel, &tuple->t_self);
		CatalogUpdateIndexes(rel, tuple);
	}
	
	heap_endscan(rel_scan);
	heap_close(rel, RowExclusiveLock);
}
/*
	1,if row_id == INVALID_ID ,row_id won't be query;
	2,if coord_name == "*",it will check all the table,
	else only select the nodname is coord_name in the table;
	3,if values is NULL,it won't be query
*/
static bool check_hba_tuple_exist(uint32 row_id, char *coord_name, char *values)
{
	Relation rel;		
	HeapScanDesc rel_scan;
	HeapTuple tuple;
	ScanKeyData scankey[2];	
	Form_mgr_hba mgr_hba;
	bool ret = false;
	bool is_check_value = true;
	char *node_name;
	Assert(coord_name);
	
	if(values == NULL)
		is_check_value = false;
	ScanKeyInit(&scankey[0]
			,Anum_mgr_hba_id
			,BTEqualStrategyNumber
			,F_INT4EQ
			,Int32GetDatum(row_id));
	ScanKeyInit(&scankey[1]
			,Anum_mgr_hba_nodename
			,BTEqualStrategyNumber
			,F_NAMEEQ
			,CStringGetDatum(coord_name));
		
	rel = heap_open(HbaRelationId, RowExclusiveLock);
	if(strcmp(coord_name, "*") == 0)
		rel_scan = heap_beginscan(rel, SnapshotNow, 0, NULL);
	else
	{
		if(IDIsValid(row_id))
			rel_scan = heap_beginscan(rel, SnapshotNow, 2, scankey);
		else
			rel_scan = heap_beginscan(rel, SnapshotNow, 1, &scankey[1]);
	}	
	
	while((tuple = heap_getnext(rel_scan, ForwardScanDirection)) != NULL)
	{
		mgr_hba = (Form_mgr_hba)GETSTRUCT(tuple);
		Assert(mgr_hba);
		if(true == is_check_value)
		{
			node_name = NameStr(mgr_hba->nodename);
			if(strcmp(node_name, coord_name) == 0)
			{
				ret = true;
				break;
			}		
		}
		else
		{
			ret = true;
			break;
		}
	}
	heap_endscan(rel_scan);
	heap_close(rel, RowExclusiveLock);
	return ret;
}

static HbaType get_connect_type(char *str_type)
{
	HbaType conntype = HBA_TYPE_EMPTY;
	char *str_lwr ;
	Assert(str_type);
	str_lwr = str_tolower(str_type, strlen(str_type), DEFAULT_COLLATION_OID);
	if(strcmp(str_lwr, "local") == 0)
		conntype = HBA_TYPE_LOCAL;
	else if(strcmp(str_lwr, "host") == 0)
		conntype = HBA_TYPE_HOST;
	else if(strcmp(str_lwr, "hostssl") == 0)
		conntype = HBA_TYPE_HOSTSSL;
	else if(strcmp(str_lwr, "hostnossl") == 0)
		conntype = HBA_TYPE_HOSTNOSSL;
	return conntype;
}

static bool IDIsValid(uint32 row_id)
{
	if(0 < row_id && row_id < INVALID_ID)
		return true;
	else
		return false;
}
