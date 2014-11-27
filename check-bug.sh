#!/bin/sh

dummy() {
    sleep 0.1
}

ERR="./TODO"
OUT="./out.log"
find /tmp/ -name "ruby_jit.*" | xargs rm -rf
if [ -e $ERR ]; then
    mv $ERR ${ERR}.old
fi

touch $ERR
TARGET=build
RUJIT_DISABLE_JIT=1 make -j8 -C $TARGET
DEFAULT_TIMEOUT=25
LONG_TIMEOUT=45

run() {
    timeout=$1
    file=$2
    arg1=$3
    arg2=$4
    echo $file $arg1 $arg2
    gtimeout $timeout ./$TARGET/ruby $file $arg1 $arg2 > /dev/null
    err=$?
    if [ "$err" -eq "0" ]; then
        dummy
    else
        echo $file >> $ERR
    fi
}

for file in `ls benchmark/*.rb`; do
    echo $file
    run $DEFAULT_TIMEOUT $file
done

for file in `ls test-jit/*.rb | grep -v "web.rb"`; do
    echo $file
    run $DEFAULT_TIMEOUT $file
done

# run $DEFAULT_TIMEOUT ext/dl/callback/mkcallback.rb -output=callback ext/dl/dl.h
# rm callback*.c
wc -l $ERR
cat $ERR
