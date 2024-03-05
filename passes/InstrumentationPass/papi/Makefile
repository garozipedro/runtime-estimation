LLVM_PATH=/usr/lib/llvm-15
CC=${LLVM_PATH}/bin/clang++ -std=c++20
CONFIG=${LLVM_PATH}/bin/llvm-config

all: papi

papi: papi_instrumentation.o

papi_instrumentation.o: papi_instrumentation.cc
	 ${CC} $^ -c -o $@
