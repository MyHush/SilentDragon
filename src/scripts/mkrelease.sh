#!/bin/bash
# Copyright (c) 2019-2020 The Hush developers
# Thanks to Zecwallet for the original code
# Released under the GPLv3

if [ -z $QT_STATIC ]; then 
    echo "QT_STATIC is not set. Please set it to the base directory of a statically compiled Qt"; 
    exit 1; 
fi

if [ -z $APP_VERSION ]; then echo "APP_VERSION is not set"; exit 1; fi
#if [ -z $PREV_VERSION ]; then echo "PREV_VERSION is not set"; exit 1; fi

if [ -z $HUSH_DIR ]; then
    echo "HUSH_DIR is not set. Please set it to the base directory of a Hush project with built Hush binaries."
    exit 1;
fi

if [ -z $HUSH_DIR ]; then
    echo "HUSH_DIR is not set. Please set it to the base directory of hush3.git"
    exit 1;
fi

if [ ! -f $HUSH_DIR/komodod ]; then
    echo "Couldn't find komodod in $HUSH_DIR . Please build komodod."
    exit 1;
fi

if [ ! -f $HUSH_DIR/komodo-cli ]; then
    echo "Couldn't find komodo-cli in $HUSH_DIR . Please build komodo-cli."
    exit 1;
fi

if [ ! -f $HUSH_DIR/komodo-tx ]; then
    echo "Couldn't find komodo-tx in $HUSH_DIR . Please build komodo-tx."
    exit 1;
fi

#echo -n "Version files.........."
## Replace the version number in the .pro file so it gets picked up everywhere
#sed -i "s/${PREV_VERSION}/${APP_VERSION}/g" silentdragon.pro > /dev/null
#
## Also update it in the README.md
#sed -i "s/${PREV_VERSION}/${APP_VERSION}/g" README.md > /dev/null
#echo "[OK]"

echo -n "Cleaning..............."
rm -rf bin/*
#rm -rf artifacts/*
make distclean >/dev/null 2>&1
echo "[OK]"
echo "[Building on" `lsb_release -r`"]"

echo -n "Configuring............"
QT_STATIC=$QT_STATIC bash src/scripts/dotranslations.sh >/dev/null
$QT_STATIC/bin/qmake silentdragon.pro -spec linux-clang CONFIG+=release > /dev/null
echo "[OK]"


echo -n "Building..............."
rm -rf bin/silentdragon* bin/SilentDragon* > /dev/null
make clean > /dev/null
PATH=$QT_STATIC/bin:$PATH ./build.sh release > /dev/null
echo "[OK]"

# Test for Qt
echo -n "Static link............"
if [[ $(ldd silentdragon | grep -i "Qt") ]]; then
    echo "FOUND QT dynamicly linked in binary, aborting!";
    exit 1
fi
echo "[OK]"

#TODO: support armv8
OS=linux
ARCH=x86_64
RELEASEDIR=SilentDragon-v$APP_VERSION
RELEASEFILE1=$RELEASEDIR-$OS-$ARCH-no-params.tar.gz
RELEASEFILE2=$RELEASEDIR-$OS-$ARCH.tar.gz

# this is equal to the number of files we package plus 1, for the directory
# that is created
NUM_FILES1=10
NUM_FILES2=12 # 2 additional param files

echo "Packaging.............."
mkdir bin/$RELEASEDIR
echo "Created bin/$RELEASEDIR"
ls -la silentdragon
echo "Stripping............."
strip silentdragon
ls -la silentdragon

cp silentdragon                  bin/$RELEASEDIR > /dev/null
cp $HUSH_DIR/komodod    	 bin/$RELEASEDIR > /dev/null
cp $HUSH_DIR/komodo-cli          bin/$RELEASEDIR > /dev/null
cp $HUSH_DIR/komodo-tx           bin/$RELEASEDIR > /dev/null
cp $HUSH_DIR/hushd               bin/$RELEASEDIR > /dev/null
cp $HUSH_DIR/hush-cli            bin/$RELEASEDIR > /dev/null
cp $HUSH_DIR/hush-tx             bin/$RELEASEDIR > /dev/null
cp README.md                     bin/$RELEASEDIR > /dev/null
cp LICENSE                       bin/$RELEASEDIR > /dev/null


cd bin && tar czf $RELEASEFILE1 $RELEASEDIR/ #> /dev/null
#ls -la $RELEASEDIR/
echo "Created $RELEASEFILE1   [OK]"
cd ..

# Now copy params in so we can make another zip with params
# for first-time users
#ls -la *.params
# This assumes we have these 2 files symlinked to where they live in hush3.git
# or copied to this dir
cp -Lp sapling-*.params bin/$RELEASEDIR/
if [ $? -eq 0 ]; then
    echo "[OK] Copied Sapling params"
else
    echo "[ERROR] Failed to copy Sapling params!"
    exit 1
fi

cd bin && tar czf $RELEASEFILE2 $RELEASEDIR/
#ls -la $RELEASEDIR/
echo "Created $RELEASEFILE2   [OK]"
cd .. 

#mkdir artifacts >/dev/null 2>&1
#cp bin/linux-silentdragon-v$APP_VERSION.tar.gz ./artifacts/linux-binaries-silentdragon-v$APP_VERSION.tar.gz

if [ -f bin/$RELEASEFILE1 ] ; then
    echo -n "Package contents for $RELEASEFILE1 ......."
    # Test if the package is built OK
    if tar tf "bin/$RELEASEFILE1" | wc -l | grep -q "$NUM_FILES1"; then
        echo "[OK] $RELEASEFILE1 has correct number of files"
    else
        echo "[ERROR] Wrong number of files in $RELEASEFILE1 ! Should be $NUM_FILES1"
        exit 1
    fi
else
    echo "[ERROR] bin/$RELEASEFILE1 does not exist!"
    exit 1
fi

if [ -f bin/$RELEASEFILE2 ] ; then
    echo -n "Package contents for $RELEASEFILE2 ......."
    # Test if the package is built OK
    if tar tf "bin/$RELEASEFILE2" | wc -l | grep -q "$NUM_FILES2"; then
        echo "[OK] $RELEASEFILE2 has correct number of files"
    else
        echo "[ERROR] Wrong number of files in $RELEASEFILE2 ! Should be $NUM_FILES2"
        exit 1
    fi    
else
    echo "[ERROR] bin/$RELEASEFILE2 does not exist!"
    exit 1
fi

cd bin
echo "DONE! Checksums:"
sha256sum $RELEASEFILE1
sha256sum $RELEASEFILE2
cd ..
echo "Speak And Transact Freely!"

exit

echo "Skipping deb, use make-deb.sh instead"

echo -n "Building deb..........."
debdir=bin/deb/silentdragon-v$APP_VERSION
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat src/scripts/control | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

cp silentdragon                   $debdir/usr/local/bin/
# TODO: how does this interact with hushd deb ?
cp $HUSH_DIR/artifacts/komodod $debdir/usr/local/bin/hush-komodod

mkdir -p $debdir/usr/share/pixmaps/
cp res/silentdragon.xpm           $debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp src/scripts/desktopentry    $debdir/usr/share/applications/silentdragon.desktop

dpkg-deb --build $debdir >/dev/null
cp $debdir.deb                 artifacts/linux-deb-silentdragon-v$APP_VERSION.deb
echo "[OK]"


echo "Skipping windows"
exit

echo ""
echo "[Windows]"

if [ -z $MXE_PATH ]; then 
    echo "MXE_PATH is not set. Set it to ~/github/mxe/usr/bin if you want to build Windows"
    echo "Not building Windows"
    exit 0; 
fi

if [ ! -f $HUSH_DIR/artifacts/komodod.exe ]; then
    echo "Couldn't find komodod.exe in $HUSH_DIR/artifacts/. Please build komodod.exe"
    exit 1;
fi


if [ ! -f $HUSH_DIR/artifacts/komodo-cli.exe ]; then
    echo "Couldn't find komodo-cli.exe in $HUSH_DIR/artifacts/. Please build komodod-cli.exe"
    exit 1;
fi

export PATH=$MXE_PATH:$PATH

echo -n "Configuring............"
make clean  > /dev/null
rm -f zec-qt-wallet-mingw.pro
rm -rf release/
#Mingw seems to have trouble with precompiled headers, so strip that option from the .pro file
cat silentdragon.pro | sed "s/precompile_header/release/g" | sed "s/PRECOMPILED_HEADER.*//g" > zec-qt-wallet-mingw.pro
echo "[OK]"


echo -n "Building..............."
# TODO: where is this .pro file?
x86_64-w64-mingw32.static-qmake-qt5 zec-qt-wallet-mingw.pro CONFIG+=release > /dev/null
make -j32 > /dev/null
echo "[OK]"


echo -n "Packaging.............."
mkdir release/silentdragon-v$APP_VERSION  
cp release/silentdragon.exe          release/silentdragon-v$APP_VERSION 
cp $HUSH_DIR/artifacts/komodod.exe    release/silentdragon-v$APP_VERSION > /dev/null
cp $HUSH_DIR/artifacts/komodo-cli.exe   release/silentdragon-v$APP_VERSION > /dev/null
cp $HUSH_DIR/artifacts/hushd.bat    release/silentdragon-v$APP_VERSION > /dev/null
cp $HUSH_DIR/artifacts/hush-cli.bat   release/silentdragon-v$APP_VERSION > /dev/null
cp README.md                          release/silentdragon-v$APP_VERSION 
cp LICENSE                            release/silentdragon-v$APP_VERSION 
cd release && zip -r Windows-binaries-silentdragon-v$APP_VERSION.zip silentdragon-v$APP_VERSION/ > /dev/null
cd ..

mkdir artifacts >/dev/null 2>&1
cp release/Windows-binaries-silentdragon-v$APP_VERSION.zip ./artifacts/
echo "[OK]"

if [ -f artifacts/Windows-binaries-silentdragon-v$APP_VERSION.zip ] ; then
    echo -n "Package contents......."
    if unzip -l "artifacts/Windows-binaries-silentdragon-v$APP_VERSION.zip" | wc -l | grep -q "11"; then 
        echo "[OK]"
    else
        echo "[ERROR]"
        exit 1
    fi
else
    echo "[ERROR]"
    exit 1
fi
