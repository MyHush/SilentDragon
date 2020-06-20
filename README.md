# SilentDragon

SilentDragon desktop wallet for HUSH runs on Linux, Windows and macOS.
This is experimental software under active development!

![Screenshots](silentdragon.png?raw=true)

## PRIVACY NOTICE

SilentDragon contacts a few different external websites to get various
bits of data.

    * coingecko.com for price data API
    * explorer.myhush.org for explorer links
    * dexstats.info for address utilities
    * wormhole.myhush.org for Wormhole services

This means your IP address is known to these servers. Enable Tor setting
in SilentDragon to prevent this, or better yet, use TAILS: https://tails.boum.org/

# Installation

Go to the releases page and grab the latest installers or binary. https://github.com/MyHush/SilentDragon/releases

## hushd

SilentDragon needs a Hush full node running hushd. If you already have a hushd node running, SilentDragon will connect to it.

If you don't have one, SilentDragon will start its embedded hushd node.

Additionally, if this is the first time you're running SilentDragon or a hushd daemon, SilentDragon will find Sapling params (~50 MB) and configure `HUSH3.conf` for you. 

Pass `--no-embedded` to disable the embedded hushd and force SilentDragon to connect to an external node.

## Compiling from source

SilentDragon is written in C++ 14, and can be compiled with g++/clang++/visual
c++. It also depends on Qt5, which you can get from
[here](https://www.qt.io/download). Note that if you are compiling from source,
you won't get the embedded hushd by default. You can either run an external
hushd, or compile hushd as well.


### Building on Linux

#### Ubuntu 18.04:

```
sudo apt-get -y install qt5-default qt5-qmake libqt5websockets5-dev qtcreator
git clone https://github.com/MyHush/SilentDragon.git
cd SilentDragon
./build.sh linguist # compile translations
./build.sh
./silentdragon
```

#### Arch Linux:

```
sudo pacman -S qt5-base qt5-tools qtcreator qt5-websockets rust
git clone https://github.com/MyHush/SilentDragonLite.git
cd SilentDragonLite
./build.sh linguist
./build.sh release
./silentdragon
```

### Building on Windows
You need Visual Studio 2017 (The free C++ Community Edition works just fine). 

From the VS Tools command prompt
```
git clone  https://github.com/MyHush/SilentDragon.git
cd SilentDragon
c:\Qt5\bin\qmake.exe silentdragon.pro -spec win32-msvc CONFIG+=debug
nmake

debug\SilentDragon.exe
```

To create the Visual Studio project files so you can compile and run from Visual Studio:
```
c:\Qt5\bin\qmake.exe silentdragon.pro -tp vc CONFIG+=debug
```

### Building on macOS

You need to install the Xcode app or the Xcode command line tools first, and then install Qt. 


```
git clone https://github.com/MyHush/SilentDragon.git
cd SilentDragon
qmake silentdragon.pro CONFIG+=debug
make

./SilentDragon.app/Contents/MacOS/SilentDragon
```

### Emulating the embedded node

In binary releases, SilentDragon will use node binaries in the current directory to sync a node from scratch.
It does not attempt to download them, it bundles them. To simulate this from a developer setup, you can symlink
these four files in your Git repo:

```
    ln -s ../hush3/src/hushd
    ln -s ../hush3/src/komodod
    ln -s ../hush3/src/komodo-cli
```

The above assumes silentdragon and hush3 git repos are in the same directory. File names on Windows will need to be tweaked.

### Support

For support or other questions, Join [Discord](https://myhush.org/discord), or tweet at [@MyHushTeam](https://twitter.com/MyHushTeam) or [file an issue](https://github.com/MyHush/SilentDragon/issues).

