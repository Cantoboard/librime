# Rime with iOS

## Preparation

Install Xcode with command line tools.

Install other build tools:

``` sh
brew install cmake git
```

iOS compatible Boost C++ libraries:

If you are cross compiling Boost on iOS yourself, please build it for both x86_64 (simulator) and arm64 (device) and enable bitcode. Otherwise, linking will fail. 

You could also find the prebuilt package here: https://github.com/danoli3/ofxiOSBoost/releases/tag/1.60.0-libc%2B%2Bbitcode

After unpacking it, move folders around and create symlinks to make sure Boost folder layout is compatible with CMake.

The result should look like:
```
./include/boost/accumulators
{...}
./include/boost/config.hpp
./lib/libboost_system.a -> ./lib/libboost.a
./lib/libboost_regex.a -> ./lib/libboost.a
./lib/libboost_filesystem.a -> ./lib/libboost.a
./lib/libboost.a
```

## Get the code

``` sh
git clone --recursive https://github.com/rime/librime.git
```
or [download from GitHub](https://github.com/rime/librime), then get code for
third party dependencies separately.

## Build third-party libraries

``` sh
cd librime
make xcode/ios/thirdparty
```

This builds dependent libraries in `thirdparty/src/*`, and copies artifacts to
`thirdparty/lib` and `thirdparty/bin`.

You can build an individual library, eg. opencc, with:

``` sh
make xcode/ios/thirdparty/opencc
```

## Build librime

``` sh
export BOOST_ROOT=<...>
make xcode/ios/dist
```
This creates `dist/lib/librime.dylib`. It is a fat binary that supports both simulator & device.

Do not use the dylib under Release, it isn't fat binary and supports only one of the architectures.
