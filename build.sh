#!/bin/bash
# Copyright 2019-2020 The Hush Developers
# Released under the GPLv3

set -e
set -x
UNAME=$(uname)

if [ "$UNAME" == "Linux" ] ; then
    JOBS=$(nproc)
elif [ "$UNAME" == "FreeBSD" ] ; then
    JOBS=$(nproc)
elif [ "$UNAME" == "Darwin" ] ; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=1
fi

VERSION=$(cat src/version.h |cut -d\" -f2)
echo "Compiling SilentDragon $VERSION with $JOBS threads..."
CONF=silentdragon.pro

qbuild () {
   qmake $CONF -spec linux-clang CONFIG+=debug
   make -j$JOBS
}

qbuild_release () {
   qmake $CONF -spec linux-clang CONFIG+=release
   make -j$JOBS
}

if [ "$1" == "clean" ]; then
   make clean
elif [ "$1" == "linguist" ]; then
    lupdate $CONF
    lrelease $CONF
elif [ "$1" == "cleanbuild" ]; then
   make clean
   qbuild
elif [ "$1" == "release" ]; then
   qbuild_release
else
   qbuild
fi
