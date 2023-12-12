#!/bin/bash

LLVM_DIR=/usr/lib/llvm-15
PATH_TO_BUILD=$1

export CC="$LLVM_DIR/bin/clang -O3"
export CXX="$LLVM_DIR/bin/clang++ -O3"

function build_pass {
    local path=$1
    cd $path
    if [[ $? -ne 0 ]]; then
        echo "Couldn't access ${path}!"
        exit
    fi
    if [[ -e CMakeLists.txt ]]; then
        echo "- BUILDING PASS IN ${path}"
        if [[ -d build ]]; then
            rm -r build
        fi
        mkdir build
        cd build
        cmake -GNinja -DLT_LLVM_INSTALL_DIR=$LLVM_DIR ..
        ninja
    else # Try to build subdirs.
        for pass in $(ls -d `pwd`/*/); do
            build_pass $pass
        done
    fi
}

build_pass ${PATH_TO_BUILD}
