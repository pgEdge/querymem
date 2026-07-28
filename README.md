<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/img/pgedge-labs-dark.svg">
    <img alt="pgEdge Labs" src="docs/img/pgedge-labs-light.svg" width="320">
  </picture>
</div>

# The querymem Postgres Extension

We've all heard the warning:

> Don't set `work_mem` too high; every query node can use that much memory.

But what is a query node? Sure it's possible to use `EXPLAIN` to see the nodes
in a query plan, but counting these can get pretty tedious with exceptionally
large queries. Additionally, not everyone knows that hash operations actually
use `work_mem` \* `hash_mem_multiplier`.

This is where the querymem extension comes in! It provides a function that will
count all nodes in a query plan and project the maximum amount of RAM it 
_could_ allocate based on current GUC settings. Check your big queries for
maximum consumption before you even run them!

The extension even provides GUCs to log or outright cancel queries that go
over certain thresholds. Never let a rogue query exhaust your memory pool!

## Installation

Installing this extension is simple:

```bash
git clone git@github.com:bonesmoses/querymem.git
cd querymem
make
sudo make install
```

Then add `querymem` to `shared_preload_libraries` in your `postgresql.conf`:
and choose your desired mode like so:

```ini
shared_preload_libraries = 'querymem'
```

Finally, connect to any database and create the extension:

```sql
CREATE EXTENSION querymem;
```

## Usage

This extension currently only provides one function.

The `get_query_mem()` function returns the amount of kilobytes a query could
allocate assuming each query node uses the maximum allowed `work_mem` for each
node type. The only parameter is the query to check:

```sql
SELECT get_query_mem($$
  SELECT a.* FROM pgbench_accounts a ORDER BY a.bid
$$);

 get_query_mem 
---------------
          8192
```

It calls internal Postgres planning functions, so tables, views, and other
objects must exist. It does not parameterize queries, so treat it like using
`EXPLAIN` by substituting static values or functions.

## Using Docker

This project includes a `Dockerfile` to simplify deployment of the extension.
It extends the standard [Postgres DockerHub](https://hub.docker.com/_/postgres/)
image, so should work exactly the same.

To use, build the image:

```bash
cd querymem
docker build -t querymem:18 .
```

Launch a container:

```bash
docker run -d --name=querymem \
    -e POSTGRES_HOST_AUTH_METHOD=trust \
    querymem:18
```

Connect and use it as normal:

```bash
docker exec -it -u postgres querymem psql
```

The image should automatically create the extension, so verify it's working:

```sql
SELECT get_query_mem('SELECT 1');

 get_query_mem 
---------------
          4096
```

## Configuration

The querymem extension currently accepts two parameters:

* `querymem.log_size` - Log all queries with an estimated work_mem size over 
  this amount of kilobytes. Default 10x `work_mem`. Set to -1 to disable.
* `querymem.max_query_size` - Cancel all queries with an estimated work_mem
  size over this amount of kilobytes. Default 100x `work_mem`. Set to -1 to
  disable.

Both parameters require a SIGHUP or `pg_reload_conf()` to change.

## Examples

The following query uses several common query techniques on standard `pgbench`
tables to increase query complexity and thus node count:

```sql
SELECT get_query_mem($$
  WITH bdata AS (
    SELECT * FROM pgbench_branches WHERE bid > 5
  )
  SELECT a.* 
    FROM pgbench_accounts a
    JOIN bdata b ON (b.bid = a.bid)
   WHERE EXISTS (
           SELECT * FROM pgbench_tellers t
            WHERE t.bid = b.bid
         )
   ORDER BY b.bid;
$$);

 get_query_mem 
---------------
         45056
```

Next, we'll artificially lower the `max_query_size` and attempt to actually
run the query:

```sql
ALTER SYSTEM SET querymem.max_query_size = '30MB';
SELECT pg_reload_conf();

WITH bdata AS (
  SELECT * FROM pgbench_branches WHERE bid > 5
)
SELECT a.* 
  FROM pgbench_accounts a
  JOIN bdata b ON (b.bid = a.bid)
 WHERE EXISTS (
         SELECT * FROM pgbench_tellers t
          WHERE t.bid = b.bid
       )
 ORDER BY b.bid;

ERROR:  This query may use too much work_mem (45056k).
HINT:  Increase the querymem.max_query_size GUC to continue.
```

The `ERROR` and `HINT` also appear in the log along with the query body.

## Discussion

This extension is intended as a learning exercise or skeleton for writing
Postgres extensions which do the following:

* Add functions which return integer values.
* Parse queries.
* Plan queries.
* Traverse query plans.
* Decode query plan nodes.
* Use GUCs.
* Employ backend hooks.

## Compatibility

This extension has only been verified as functional against Postgres 18.
