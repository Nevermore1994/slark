#!/bin/bash
export SDK=$(xcrun --sdk iphoneos --show-sdk-path)
export CC="$(xcrun --sdk iphoneos -f clang)"
export CXX="$(xcrun --sdk iphoneos -f clang++)"
export CFLAGS="-arch arm64 -isysroot $SDK -mios-version-min=11.0"
export LDFLAGS="-arch arm64 -isysroot $SDK -mios-version-min=11.0"
export PREFIX="$(pwd)/build/ios-arm64"

./Configure ios64-cross \
    no-shared \
    no-tests \
    no-ssl3 \
    no-comp \
    no-async \
    no-dso \
    no-engine \
    no-tests \
    no-module \
    --prefix="$PREFIX"


make clean
make -j$(sysctl -n hw.logicalcpu)
make install_sw

unset CFLAGS
unset LDFLAGS
unset CC
unset CXX
unset SDK
unset PREFIX


