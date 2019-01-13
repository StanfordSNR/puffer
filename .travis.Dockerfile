FROM ubuntu:18.04

MAINTAINER fyy "https://github.com/francisyyan"

RUN apt-get update -qq

RUN apt-get install -y -q gcc-7 g++-7 libmpeg2-4-dev libpq-dev \
                          libssl-dev libcrypto++-dev libyaml-cpp-dev \
                          libboost-dev liba52-dev opus-tools libopus-dev \
                          libsndfile-dev libavformat-dev libavutil-dev ffmpeg \
                          git automake libtool python python3 cmake wget

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 99
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 99

# install the latest version of libpqxx
RUN git clone https://github.com/jtv/libpqxx.git
RUN cd libpqxx && git checkout 6.2.5 && ./configure --enable-documentation=no && make -j3 install

RUN useradd --create-home --shell /bin/bash user
COPY . /home/user/puffer/
RUN chown user -R /home/user/puffer/

ENV LANG C.UTF-8
ENV LANGUAGE C:en
ENV LC_ALL C.UTF-8

USER user
