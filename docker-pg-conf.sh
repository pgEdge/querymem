#!/usr/bin/env bash

echo "shared_preload_libraries = 'querymem'" >> ${PGDATA}/postgresql.conf
