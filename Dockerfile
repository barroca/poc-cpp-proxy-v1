FROM ubuntu:xenial

ADD resources/docker/sources.list /etc/apt/sources.list

ADD resources/docker/dumb-init_1.2.0_amd64 /usr/local/bin/dumb-init
RUN chmod +x /usr/local/bin/dumb-init

RUN apt-get update
RUN apt-get install -y libcppnetlib-dev libboost-dev build-essential
