FROM postgres:18

RUN apt-get -y update && apt-get -y upgrade && \
    apt-get -y install build-essential clang postgresql-server-dev-18 \
        libreadline-dev zlib1g-dev flex bison libxml2-dev \
        libxslt-dev libssl-dev libxml2-utils xsltproc \
        ccache pkg-config libicu-dev && \
    apt-get clean

COPY . /build
COPY docker-init.sql /docker-entrypoint-initdb.d
COPY docker-pg-conf.sh /docker-entrypoint-initdb.d

RUN cd /build && make && make install
