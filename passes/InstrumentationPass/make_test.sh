#! /bin/bash

target=$1
clang++ $target papi/papi_instrumentation.o -lpapi -o $target.out
