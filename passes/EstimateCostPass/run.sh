#!/bin/bash

# get_plugin_path.sh exports the variables:
#   $plugin_path : the path to libEstimateCostPass.so;
#   ${deps_path[@]} : the path to analysis passes the main pass depends on.
source ./get_plugin_path.sh
llvm_dir=/usr/lib/llvm-15

function run_tool {
    local target=$1
    load_plugins=""
    for path in ${deps_path[@]}; do load_plugins+=" -load-pass-plugin ${path} "; done
    load_plugins+=" -load-pass-plugin ${plugin_path} "
    $llvm_dir/bin/opt ${load_plugins} -passes="EstimateCostPass" -disable-output $target --prediction-cost-kind='latency,recipthroughput,codesize,sizeandlatency' # --use-points-to-analysis
}

run_tool $1
