-- Postgres will execute this SQL file upon invoking
-- the "CREATE EXTENSION querymem" command.

CREATE FUNCTION get_query_mem(text)
RETURNS INT
     AS 'MODULE_PATHNAME', 'get_query_mem'
     LANGUAGE C VOLATILE;
