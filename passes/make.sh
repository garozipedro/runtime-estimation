#!/bin/bash

LLVM_DIR=/usr/lib/llvm-15
PATH_TO_BUILD=$1
NDEBUG=$2

export CC="$LLVM_DIR/bin/clang -O3"
export CXX="$LLVM_DIR/bin/clang++ -O3"

function build_pass {
    local path=$1
    local extra=$2
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
        cmake -GNinja -DLT_LLVM_INSTALL_DIR=$LLVM_DIR -DEXTRA_OPTIONS=$extra ..
        ninja
    else # Try to build subdirs.
        for pass in $(ls -d `pwd`/*/); do
            build_pass $pass
        done
    fi
}

if [[ $NDEBUG ]]; then
    EXTRA_STR="-DNDEBUG -O3"
fi

echo "EXTRA_STR = ${EXTRA_STR}"

build_pass ${PATH_TO_BUILD} ${EXTRA_STR}
