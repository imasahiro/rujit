#!/bin/bash

if [ $# -ne 1 ]; then
    echo "usage $0 benchmark_file_name"
    exit 1
fi

file="./benchmark/$1.rb"

if [ -f $file ]; then
    echo "building miniruby"
else
    echo "$file file not found"
    exit 1
fi

TARGET=tmp
git pull --ff origin rujit3
# rm -rf $TARGET
if [ -d $TARGET ]; then
    echo
else
    mkdir -p $TARGET
    (autoreconf -ivf && cd $TARGET && ../configure --prefix=$HOME 2>&1 > /dev/null && make miniruby 2>&1 > /dev/null)
fi
(cd $TARGET && make miniruby 2>&1 /dev/null)
if [ $? -eq 1 ]; then
    echo "build failed"
    exit 1
fi

time -p ./$TARGET/miniruby $file
