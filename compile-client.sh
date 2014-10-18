#!/bin/bash
path=$(dirname "$0")
cd $path
cd src
sh compile_client.sh
