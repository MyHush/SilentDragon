#!/bin/bash
# Copyright (c) 2019-2020 The Hush developers
# Thanks to Zecwallet for the original code
# Released under the GPLv3

DEBLOG=deb.log.$$

if [ -z $QT_STATIC ]; then
    echo "QT_STATIC is not set. Please set it to the base directory of a statically compiled Qt";
    exit 1;
fi

APP_VERSION=$(cat src/version.h | cut -d\" -f2)
if [ -z $APP_VERSION ]; then echo "APP_VERSION is not set"; exit 1; fi
#if [ -z $PREV_VERSION ]; then echo "PREV_VERSION is not set"; exit 1; fi

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

echo -n "Cleaning..............."
rm -rf bin/*
rm -rf artifacts/*
make distclean >/dev/null 2>&1
echo "[OK]"

echo ""
echo "[Building $APP_VERSION on" `lsb_release -r`" logging to $DEBLOG ]"

echo -n "Translations............"
QT_STATIC=$QT_STATIC bash src/scripts/dotranslations.sh >/dev/null
echo -n "Configuring............"
$QT_STATIC/bin/qmake silentdragon.pro -spec linux-clang CONFIG+=release > /dev/null
echo "[OK]"


echo -n "Building..............."
rm -rf silentdragon
make clean > /dev/null
./build.sh release > /dev/null
echo "[OK]"


# Test for Qt
echo -n "Static link............"
if [[ $(ldd silentdragon | grep -i "Qt") ]]; then
    echo "FOUND QT; ABORT";
    exit 1
fi
echo "[OK]"


echo -n "Packaging.............."
APP=SilentDragon-v$APP_VERSION
DIR=bin/$APP
mkdir $DIR > /dev/null
strip silentdragon

cp silentdragon                  $DIR > /dev/null
cp $HUSH_DIR/artifacts/komodod    $DIR > /dev/null
cp $HUSH_DIR/artifacts/komodo-cli $DIR > /dev/null
cp $HUSH_DIR/artifacts/komodo-tx $DIR > /dev/null
cp $HUSH_DIR/artifacts/hushd    $DIR > /dev/null
cp $HUSH_DIR/artifacts/hush-cli $DIR > /dev/null
cp $HUSH_DIR/artifacts/hush-tx $DIR > /dev/null
cp README.md                      $DIR > /dev/null
cp LICENSE                        $DIR > /dev/null

cd bin && tar czf $APP.tar.gz $DIR/ > /dev/null
cd ..

mkdir artifacts >/dev/null 2>&1
cp $DIR.tar.gz ./artifacts/$APP-linux.tar.gz
echo "[OK]"


if [ -f artifacts/$APP-linux.tar.gz ] ; then
    echo -n "Package contents......."
    # Test if the package is built OK
    if tar tf "artifacts/$APP-linux.tar.gz" | wc -l | grep -q "9"; then
        echo "[OK]"
    else
        echo "[ERROR] Wrong number of files does not match 9"
        exit 1
    fi
else
    echo "[ERROR]"
    exit 1
fi

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
cp $debdir.deb                 artifacts/$DIR.deb
echo "[OK]"

exit 0
