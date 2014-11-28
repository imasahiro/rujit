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
    (autoreconf -ivf && cd $TARGET && ../configure --prefix=$HOME > /dev/null 2>&1 && make miniruby > /dev/null 2>&1)
fi
(cd $TARGET && make miniruby > /dev/null 2>&1 )
if [ $? -eq 1 ]; then
    echo "build failed"
    exit 1
fi

{ /usr/bin/time -p ./$TARGET/miniruby $file; } 2> out.txt
cat out.txt
