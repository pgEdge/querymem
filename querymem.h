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

#endif // QUERYMEM_H
