#!/bin/bash

llvm_dir=/usr/lib/llvm-15
pass_name="InstrumentationPass"
plugin_path="$(pwd)/build/lib${pass_name}.so"

function run_tool {
    local target=$1 # The llvm ir target code.
    load_plugins=" -load-pass-plugin ${plugin_path} "
    # Run once for each instrumentation policy.
    $llvm_dir/bin/opt -S ${load_plugins} -passes=$pass_name -instrumentation-output="bb.txt" -granularity="basicblock" -yaml-output=$target.bb.yaml $target > $target.instrumented.bb.ll
    $llvm_dir/bin/opt -S ${load_plugins} -passes=$pass_name -instrumentation-output="fun.txt" -granularity="function" -yaml-output=$target.fun.yaml $target > $target.instrumented.fun.ll
#    $llvm_dir/bin/opt -disable-output ${load_plugins} -passes=$pass_name $target
}

run_tool $1
