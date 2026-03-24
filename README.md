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

## Installation

Installing this extension is simple:

```bash
git clone git@github.com:bonesmoses/querymem.git
cd querymem
make
sudo make install
```

Then connect to any database and execute this statement:

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
         57344
```

## Discussion

This extension is intended as a learning exercise or skeleton for writing
Postgres extensions which do the following:

* Add functions which return integer values.
* Parse queries.
* Plan queries.
* Traverse query plans.
* Decode query plan nodes.
* Use GUCs.

## Compatibility

This extension has only been verified as functional against Postgres 18.
