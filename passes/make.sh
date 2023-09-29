#!/bin/bash

LLVM_DIR=/usr/lib/llvm-15
PATH_TO_BUILD=$1

export CC="$LLVM_DIR/bin/clang"
export CXX="$LLVM_DIR/bin/clang++"

cd ${PATH_TO_BUILD}
if [[ -d build ]]; then
    rm -r build
fi
mkdir build
cd build
cmake -GNinja -DLT_LLVM_INSTALL_DIR=$LLVM_DIR ..
ninja
