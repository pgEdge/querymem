MODULE_big = querymem

PGFILEDESC = "Extension to view query memory usage"
EXTENSION = querymem
DATA = querymem--1.0.sql
OBJS = querymem.o

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
