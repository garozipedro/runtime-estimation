LLVM_PATH=/usr/lib/llvm-15
CC=${LLVM_PATH}/bin/clang++ -std=c++20
CONFIG=${LLVM_PATH}/bin/llvm-config
CFLAGS=-O2

all: papi

papi: papi_instrumentation.o papi_instrumentation_v2.o

papi_instrumentation.o: papi_instrumentation.cc
	 ${CC} ${CFLAGS} $^ -c -o $@

papi_instrumentation_v2.o: papi_instrumentation_v2.cc
	 ${CC} ${CFLAGS} $^ -c -o $@
