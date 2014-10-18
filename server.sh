#!/bin/bash
path=$(dirname "$0")
cd $path
cd src
#./pserver  24 200 data 60530 0
./pserver  12 200 $1 $2 0
