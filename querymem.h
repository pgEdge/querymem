#ifndef QUERYMEM_H
#define QUERYMEM_H

#define QUERYMEM_VERSION "1.0.0"
#define QUERYMEM_VERSION_NUM 10000

typedef struct
{
	uint16_t node_count;
	double multiplier;
} WalkContext;

static void walk_plan(Plan *plan, WalkContext *ctx);
static int32_t estimate_from_plan(PlannedStmt *plan);

// Some invisible variables for our GUCs

extern PGDLLIMPORT int querymem_log_size;
extern PGDLLIMPORT int querymem_max_query_size;

#endif // QUERYMEM_H
