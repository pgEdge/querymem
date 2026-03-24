// These are almost always necessary for an extension

#include "postgres.h"
#include "fmgr.h"			// Extension function macros
#include "miscadmin.h"		// work_mem, hash_mem_multiplier
#include "utils/builtins.h"	// text_to_cstring

// We'll be doing a _lot_ of parsing

#include "parser/parser.h"
#include "nodes/print.h"			// pprint
#include "nodes/parsenodes.h"		// PlannedStmt
#include "optimizer/optimizer.h"	// planner
#include "commands/prepare.h"		// ParseState
#include "parser/analyze.h"			// transformTopLevelStmt

// Our own include comes last.

#include "querymem.h"

// Helper utilities needed by Postgres C functions

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

// Start the Postgres module!

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(get_query_mem);

Datum
get_query_mem(PG_FUNCTION_ARGS)
{
	ParseState  *pstate = make_parsestate(NULL);
	Query       *query;
	PlannedStmt *plan;
	WalkContext  ctx = {0, 0.0};
	ListCell    *lc;

	char *query_arg = text_to_cstring(PG_GETARG_TEXT_PP(0));
	List *parse_tree = raw_parser(query_arg, RAW_PARSE_DEFAULT);
	RawStmt *parsed = (RawStmt *)linitial(parse_tree);

	query = transformTopLevelStmt(pstate, parsed);
	plan = planner(query, query_arg, 0, NULL);

	// Walk the main plan tree
	walk_plan(plan->planTree, &ctx);

	// Also walk any top-level subplans (correlated subqueries, CTEs)
	// that might not be reachable via initPlan links alone
	foreach(lc, plan->subplans)
		walk_plan((Plan *) lfirst(lc), &ctx);

	elog(DEBUG1, 
	     "Total query nodes: %d, work_mem (including hash) multiplier: %.2f, "
		 "estimated peak: %d kB",
		 ctx.node_count, ctx.multiplier,
		 (int32_t)(work_mem * ctx.multiplier));

	PG_RETURN_INT32(DatumGetInt32(work_mem * ctx.multiplier));

} // get_query_mem
