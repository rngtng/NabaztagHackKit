#!/usr/bin/env bash

HOST=192.168.1.104

cd src 
rake remote:compile FILE=b2.mtl
mv bootcode.bin ../public/bootcode/ 
cd .. 
rackup -p 9090