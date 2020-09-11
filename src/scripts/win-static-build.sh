#!/bin/bash
# Copyright 2019 The Hush Developers



VERSION=$(cat src/version.h |cut -d\" -f2)
echo "Compiling SilentDragonLite $VERSION .exe with $JOBS threads..."
CONF=silentdragon-lite.pro

set -e
echo 'source $HOME/.cargo/env' >> $HOME/.bashrc
CC_x86_64_pc_windows_gnu="x86_64-w64-mingw32.static-gcc"
PATH="/home/$USER/github/mxe/usr/bin:${PATH}"
mkdir release
cp src/precompiled.h release/
qbuild () {
 /home/$USER/github/mxe/usr/bin/x86_64-w64-mingw32.static-qmake-qt5 $CONF CONFIG+=release

   #lupdate $CONF
   #lrelease $CONF
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
else
   qbuild
fi
