// Major libraries first

#include <limits.h>

// These are almost always necessary for an extension

#include "postgres.h"
#include "fmgr.h"			// Extension function macros
#include "miscadmin.h"		// work_mem, hash_mem_multiplier
#include "utils/builtins.h"	// text_to_cstring
#include "utils/guc.h"		// GUC registration, types, signals

// We'll be doing a _lot_ of parsing

#include "parser/parser.h"
#include "nodes/print.h"			// pprint
#include "nodes/parsenodes.h"		// PlannedStmt
#include "optimizer/optimizer.h"	// planner
#include "commands/prepare.h"		// ParseState
#include "parser/analyze.h"			// transformTopLevelStmt

// Executor hooks

#include "executor/execdesc.h"
#include "executor/executor.h"

// Our own include comes last.

#include "querymem.h"

// Helper utilities needed by Postgres C functions

/**
 * Traverse the nodes of a query plan to estimate work_mem maximum
 * 
 * The logic for this function comes from the Postgres documentation:
 * 
 * https://www.postgresql.org/docs/current/runtime-config-resource.html
 * 
 * Normal query plan nodes count for one work_mem instance, while hash types
 * count for hash_mem_multiplier instances. This function simply traverses
 * the tree and takes left/right paths until none remain.
 */
static void
walk_plan(Plan *plan, WalkContext *ctx)
{
	if (plan == NULL)
		return;

	// Every node type allocates work_mem; some more than others.
	// See: src/backend/nodes/nodetags.h for tags
	// See: ExplainNode in src/backend/commands/explain.c for more detailed use

	switch (nodeTag(plan))
	{
		case T_Hash:
		case T_HashJoin:
		case T_HashJoinState:
		case T_HashPath:
		case T_HashState:
			ctx->node_count++;
			ctx->multiplier += hash_mem_multiplier;
			break;
		default:
			ctx->node_count++;
			ctx->multiplier += 1.0;
			break;
	}

	// Recurse into standard child trees.

	walk_plan(plan->lefttree, ctx);
	walk_plan(plan->righttree, ctx);
}

/**
 * Traverse the nodes of a planned statement to estimate work_mem maximum
 * 
 * Given a top-level planned statement, we have a query plan (planTree) and
 * may also have some number of subplans. This function calls walk_plan to
 * ensure a work_mem estimate includes all query parts.
 * 
 * This is split out to a separate function so it is available to our
 * ExecutorStart hook and the get_query_mem user function.
 *
 * Returns an integer representing the amount of KB a query _might_ allocate.
 */
int32_t
estimate_from_plan(PlannedStmt *plan)
{
	ListCell    *lc;
	WalkContext ctx = {0, 0.0};

	// Walk the main plan tree, but also traverse any top-level subplans
	// (correlated subqueries, CTEs) that might not be reachable via initPlan
	// links alone.

	walk_plan(plan->planTree, &ctx);

	foreach(lc, plan->subplans)
		walk_plan((Plan *) lfirst(lc), &ctx);

	return work_mem * ctx.multiplier;

} // estimate_from_plan

// Start the Postgres module!

int querymem_log_size;
int querymem_max_query_size;

static ExecutorStart_hook_type next_ExecutorStart_hook = NULL;

void _PG_init(void);

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(get_query_mem);

/**
 * Parse a query string and estimate the amount of work_mem it would use.
 * 
 * This is basically a wrapper for estimate_from_plan, but we need a few extra
 * steps to convert the text string argument into something the Postgres
 * planner recognizes.
 * 
 * Returns an integer representing the amount of KB a query _might_ allocate.
 */
Datum
get_query_mem(PG_FUNCTION_ARGS)
{
	ParseState  *pstate = make_parsestate(NULL);
	Query       *query;
	PlannedStmt *plan;
	int32_t		estimate;

	char *query_arg = text_to_cstring(PG_GETARG_TEXT_PP(0));

	// The raw_parser function actually parses all statements, no matter how
	// many were passed in the string. We only care about the first one.

	List *parse_tree = raw_parser(query_arg, RAW_PARSE_DEFAULT);
	RawStmt *parsed = (RawStmt *)linitial(parse_tree);

	// See: src/backend/parser/analyze.c for examples of transformTopLevelStmt
	// It's a convenient function for turning a raw statement into a query.

	query = transformTopLevelStmt(pstate, parsed);
	plan = planner(query, query_arg, 0, NULL);

	estimate = estimate_from_plan(plan);

	PG_RETURN_INT32(DatumGetInt32(estimate));

} // get_query_mem

/**
 * Intercept the Postgres executor and enforce work_mem limits
 * 
 * The goal here is to either log or entirely prevent execution of queries
 * that could allocate excessive amounts of RAM. Thankfully we can just use
 * error reporting or logging for that and then continue to the standard
 * executor if everything is fine.
 * 
 * See: executor/executor.h
 */
static void
querymem_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	int32_t estimate = 0;

	if (querymem_log_size > -1 || querymem_max_query_size > -1)
		estimate = estimate_from_plan(queryDesc->plannedstmt);

	if (querymem_max_query_size > -1 && estimate > querymem_max_query_size)
		ereport(ERROR,
			(errcode(ERRCODE_STATEMENT_TOO_COMPLEX),
			 errmsg("This query may use too much work_mem (%dk).", estimate),
			 errhint("Increase the querymem.max_query_size GUC to continue."))
		);

	if (querymem_log_size > -1 && estimate > querymem_log_size)
		elog(LOG, "Estimated peak memory usage: %d kB", estimate);

  /* Any time we don't abort, we need to explicitly call the next registered
   * ProcessUtility afterwards, or no queries would ever execute. OH NO!
   */

  if (next_ExecutorStart_hook)
    next_ExecutorStart_hook(queryDesc, eflags);
  else
    standard_ExecutorStart(queryDesc, eflags);

} // querymem_ProcessUtility

/**
 * Set our expected GUCs and register the executor callback.
 * 
 * Current GUC list under 'querymem' prefix:
 * - log_size - Log once potential work_mem crosses this amount.
 * - max_query_size - Block execution once potential crosses this.
 */
void
_PG_init(void)
{
	DefineCustomIntVariable(
		"querymem.log_size",
		"Log any queries exceeding this amount of estimated memory.",
		"Default: 10x work_mem. Set to -1 to disable.",
		&querymem_log_size,
		work_mem * 10, work_mem,	// Same limits as work_mem
		MAX_KILOBYTES,
		PGC_SIGHUP,				// Only supported by daemon reload
		GUC_UNIT_KB,			// This can be assigned in KB, MB, GB, etc.
		NULL, NULL, NULL		// No hooks necessary
	);

	DefineCustomIntVariable(
		"querymem.max_query_size",
		"Prevent execution of any query with estimated memory over this limit.",
		"Default: 100x work_mem. Set to -1 to disable.",
		&querymem_max_query_size,
		work_mem * 100, work_mem,	// Same limits as work_mem
		MAX_KILOBYTES,
		PGC_SIGHUP,				// Only supported by daemon reload
		GUC_UNIT_KB,			// This can be assigned in KB, MB, GB, etc.
		NULL, NULL, NULL		// No hooks necessary
	);

	MarkGUCPrefixReserved("querymem");

	if (ExecutorStart_hook)
		next_ExecutorStart_hook = ExecutorStart_hook;

	ExecutorStart_hook = querymem_ExecutorStart;

} // _PG_init
