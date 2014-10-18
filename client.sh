#!/bin/bash
path=$(dirname "$0")
cd $path
cd src
#./client 10.97.240.135 60530 24 200 0 result 30 1>out1 2>out2
./client $1 $2 12 200 0 $3 30 2>out2
#./client 24 32 30 $1 $2 $3
