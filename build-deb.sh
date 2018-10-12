#!/bin/bash

apt-get -y -qq update
apt-get -y install curl

curl -fsSL http://packages.gameap.ru/debian/gameap-rep.gpg.key | apt-key add -
echo "deb http://packages.gameap.ru/debian jessie main" >> /etc/apt/sources.list

apt-get -qq update

apt-get -y install build-essential make cmake

apt-get -y install libboost-dev libboost-system-dev libboost-filesystem-dev libboost-iostreams-dev libboost-thread-dev
apt-get -y install libboost-process-dev libbinn-dev
apt-get -y install libjsoncpp-dev libssl-dev libmysqlclient-dev
apt-get -y install librestclientcpp-dev

cmake .
make
