#include "postgres.h"

#include "parser/scanner.h"
#include "nodes/pg_list.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "mgr_gram.h"

#define PG_KEYWORD(a,b,c) {a,b,c},

const ScanKeyword ManagerKeywords[] = {
PG_KEYWORD("add", ADD_P, UNRESERVED_KEYWORD)
PG_KEYWORD("agent", AGENT, UNRESERVED_KEYWORD)
PG_KEYWORD("all", ALL, UNRESERVED_KEYWORD)
PG_KEYWORD("alter", ALTER, UNRESERVED_KEYWORD)
PG_KEYWORD("append", APPEND, UNRESERVED_KEYWORD)
PG_KEYWORD("config", CONFIG, UNRESERVED_KEYWORD)
PG_KEYWORD("coordinator", COORDINATOR, UNRESERVED_KEYWORD)
PG_KEYWORD("datanode", DATANODE, UNRESERVED_KEYWORD)
PG_KEYWORD("deploy", DEPLOY, UNRESERVED_KEYWORD)
PG_KEYWORD("drop", DROP, UNRESERVED_KEYWORD)
PG_KEYWORD("exists", EXISTS, UNRESERVED_KEYWORD)
PG_KEYWORD("f", F, UNRESERVED_KEYWORD)
PG_KEYWORD("failover", FAILOVER, UNRESERVED_KEYWORD)
PG_KEYWORD("false", FALSE_P, RESERVED_KEYWORD)
PG_KEYWORD("fast", FAST, UNRESERVED_KEYWORD)
PG_KEYWORD("gtm", GTM, UNRESERVED_KEYWORD)
PG_KEYWORD("gtm_proxy", GTM_PROXY, UNRESERVED_KEYWORD)
PG_KEYWORD("host", HOST, UNRESERVED_KEYWORD)
PG_KEYWORD("i", I, UNRESERVED_KEYWORD)
PG_KEYWORD("if", IF_P, UNRESERVED_KEYWORD)
PG_KEYWORD("immediate", IMMEDIATE, UNRESERVED_KEYWORD)
PG_KEYWORD("init", INIT, UNRESERVED_KEYWORD)
PG_KEYWORD("list", LIST, UNRESERVED_KEYWORD)
PG_KEYWORD("master", MASTER, UNRESERVED_KEYWORD)
PG_KEYWORD("mode", MODE, UNRESERVED_KEYWORD)
PG_KEYWORD("monitor", MONITOR, UNRESERVED_KEYWORD)
PG_KEYWORD("node", NODE, UNRESERVED_KEYWORD)
PG_KEYWORD("not", NOT, RESERVED_KEYWORD)
PG_KEYWORD("off", OFF, UNRESERVED_KEYWORD)
PG_KEYWORD("on", ON, UNRESERVED_KEYWORD)
PG_KEYWORD("parm", PARM, UNRESERVED_KEYWORD)
PG_KEYWORD("password", PASSWORD, UNRESERVED_KEYWORD)
PG_KEYWORD("s", S, UNRESERVED_KEYWORD)
PG_KEYWORD("set", SET, UNRESERVED_KEYWORD)
PG_KEYWORD("slave", SLAVE, UNRESERVED_KEYWORD)
PG_KEYWORD("smart", SMART, UNRESERVED_KEYWORD)
PG_KEYWORD("start", START, UNRESERVED_KEYWORD)
PG_KEYWORD("stop", STOP, UNRESERVED_KEYWORD)
PG_KEYWORD("true", TRUE_P, RESERVED_KEYWORD)
PG_KEYWORD("to", TO, UNRESERVED_KEYWORD)

};

const int NumManagerKeywords = lengthof(ManagerKeywords);
