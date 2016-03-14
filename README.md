# Dynamips (Cisco Router Emulator)

[![Build Status](https://travis-ci.org/GNS3/dynamips.svg?branch=master)](https://travis-ci.org/GNS3/dynamips)

## Overview

Authors of this document: Fabien Devaux, Christophe Fillot, MtvE, 
Gordon Russell, Jeremy Grossmann and Flávio J. Saraiva.

Converted to markdown format by Daniel Lintott.

This is a continuation of Dynamips, based on the last development version and 
improved with patches wrote by various people from the community. This fork was
named Dynamips-community up to the 0.2.8-community release and renamed to the 
original Dynamips on the 0.2.9 release.

You can compile two different versions of Dynamips with this code.
Edit the Makefile to set the flags to suit your environment.
One of the flags, DYNAMIPS_CODE, can be "stable" or "unstable".

Unstable is the code which contains most of the development code, and is
in particular suitable for use on a 64 bit Mac. Unfortunately this has
proved to be unstable on other platforms.

Stable contains the same code as Unstable, minus some mips64 bit optimisations
and tcb code which seems to trigger instability on a number of platforms.
You should probably use stable unless you have a very good reason.

For more information on the how to use Dynamips see the README file

License: GNU GPLv2

### How to compile Dynamips

Dynamips now uses the CMake build system. To compile Dynamips you will need 
CMake and a working GCC or Clang compiler, as well as the build dependencies.

#### Build Dependencies

On Debian based systems the following build dependencies are required and can be
installed using apt-get:
- libelf-dev
- libpcap0.8-dev

On Redhat based systems (CentOS, Fedora etc) the following build dependencies are
required and can be installed using yum:
- elfutils-libelf-devel
- libpcap-devel

Similar packages should be available for most distributions, consult your
distributions package list to find them.

MacPort & Homebrew:
- libelf
- cmake

Windows with Cygwin:

- Install Winpcap: https://www.winpcap.org/
- Install Cygwin 32-bit (setup-x86.exe): https://cygwin.com/install.html
- In Cygwin setup, install the ``make``, ``cmake``, ``gcc-core`` and ``git`` packages
- Additionally, install the ``libelf0`` package (**important:** both bin and src)
- Download and unzip Winpcap developer pack: http://www.winpcap.org/devel.htm
- Copy the libraries ``WpdPack\Lib\libpacket.a`` and ``WpdPack\Lib\libwpcap.a`` to ``cygwin\lib\``
- Copy all headers from ``WpdPack\Include`` to ``cygwin\usr\include\``

#### Compiling (Linux/Mac)

Either download and extract a source tarball from the releases page or clone the
Git repository using:

```
git clone git://github.com/GNS3/dynamips.git
cd dynamips
mkdir build
cd build
cmake ..
```

On OSX Yosemite you need to force usage of GCC 4.9:
```
cmake ..  -DCMAKE_C_COMPILER=/usr/local/bin/gcc-4.9
```

And for building stable release:
```
cmake .. -DDYNAMIPS_CODE=stable  -DCMAKE_C_COMPILER=/usr/local/bin/gcc-4.9
```

This will generate the Makefiles required for compiling Dynamips. To just build 
Dynamips simple run:

```
make
```
or to build and install Dynamips run:

```
make install
```

The specify a differant installation location run:

```
cmake -DCMAKE_INSTALL_PREFIX=/target/path ..
```

#### Compiling (Windows)

Open the Cygwin terminal.

First, the libelf has to be manually compiled and installed:

``<MIRROR_DOWNLOADS>`` is the directory used by your Cygwin mirror to download packages.
It is possible that the libelf version differs from below.

```
cp <MIRROR_DOWNLOADS>/x86/release/libelf/libelf0/libelf0-0.8.13-2-src.tar.bz2 .
mkdir libelf && tar xvjf libelf0-0.8.13-2-src.tar.bz2 -C libelf
cd libelf
tar xvzf libelf-0.8.13.tar.gz
cd libelf-0.8.13
./configure
make
make install
```

Then, Dynamips can be build:

```
git clone git://github.com/GNS3/dynamips.git
cd dynamips
mkdir build
cd build
cmake ..
make
```

You will find ``dynamips.exe`` in the stable directory.
Put ``cygwin1.dll`` from the Cygwin bin directory in the same directory as ``dynamips.exe`` to be able to start it from outside Cygwin terminal.

### Releasing

* Update ChangeLog
* In common/dynamips.c update sw_version_tag with date
* Update RELEASE-NOTE
* git tag the release

### Useful Information 
Website: http://www.gns3.net/dynamips/

Forum: http://forum.gns3.net/

Repository: https://github.com/GNS3/dynamips

Bugtracker: https://github.com/GNS3/dynamips/issues

### Original websites
http://www.ipflow.utc.fr/index.php/Cisco_7200_Simulator
http://www.ipflow.utc.fr/blog/
