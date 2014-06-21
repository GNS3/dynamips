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
- uuid-dev
- libpcap0.8-dev

Similar packages should be available for most distributions, consult your 
distributions package list to find them.

#### Compiling

Either download and extract a source tarball from the releases page or clone the
Git repository using:

```
git clone git://github.com/GNS3/dynamips.git
cd dynamips
mkdir build
cd build
cmake ..
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

### Useful Information 
Website: http://www.gns3.net/dynamips/

Forum: http://forum.gns3.net/

Repository: https://github.com/GNS3/dynamips

Bugtracker: https://github.com/GNS3/dynamips/issues

### Original websites
http://www.ipflow.utc.fr/index.php/Cisco_7200_Simulator
http://www.ipflow.utc.fr/blog/
