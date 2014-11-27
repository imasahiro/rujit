#!/bin/bash

if [ $# -ne 1 ]; then
    echo "usage $0 benchmark_file_name"
    exit 1
fi

file="./benchmark/$1.rb"

if [ -a $file ]; then
    echo "building miniruby"
else
    echo "$file file not found"
    exit 1
fi

TARGET=tmp
git pull --ff jit rujit3
# rm -rf $TARGET
if [ -d $TARGET ]; then
    echo
else
    mkdir -p $TARGET
    (cd $TARGET && ../configure --prefix=$HOME --with-openssl-dir=/usr/local/opt/openssl/ >& /dev/null && make miniruby >& /dev/null)
fi
(cd $TARGET && make miniruby >& /dev/null)
if [ $? -eq 1 ]; then
    echo "build failed"
    exit 1
fi

time -p ./$TARGET/miniruby $file
