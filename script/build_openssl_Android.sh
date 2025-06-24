export ANDROID_NDK_HOME=$ANDROID_NDK_ROOT
export PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin:$PATH
export TOOLCHAIN=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64
export API=21
export CC=$TOOLCHAIN/bin/aarch64-linux-android$API-clang
export CXX=$TOOLCHAIN/bin/aarch64-linux-android$API-clang++
export PREFIX="$(pwd)/build/android-arm64"
export RANLIB=$TOOLCHAIN/bin/llvm-ranlib
export STRIP=$TOOLCHAIN/bin/llvm-strip

unset CFLAGS
unset LDFLAGS

./Configure android-arm64 \
    no-shared \
    no-tests \
    no-ssl3 \
    no-comp \
    no-async \
    no-dso \
    no-engine \
    no-module \
    --prefix="$PREFIX" \
    --openssldir="$PREFIX/ssl" \
    -D__ANDROID_API__=$API

make clean
make -j4
make install_sw


unset TOOLCHAIN
unset API
unset CC
unset CXX
unset PREFIX


