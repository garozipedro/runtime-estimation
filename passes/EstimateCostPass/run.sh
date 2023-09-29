#!/bin/bash

# get_plugin_path.sh exports the variables:
#   $plugin_path : the path to libEstimateCostPass.so;
#   ${deps_path[@]} : the path to analysis passes the main pass depends on.
source ./get_plugin_path.sh
llvm_dir=/usr/lib/llvm-15

function run_tool {
    load_plugins=""
    for path in ${deps_path[@]}; do load_plugins+=" -load-pass-plugin ${path} "; done
    load_plugins+=" -load-pass-plugin ${plugin_path} "
    for target in $(ls *.ll); do
        $llvm_dir/bin/opt ${load_plugins} -passes="EstimateCostPass" -disable-output $target --trace-indirect-calls
    done
}

selected_test=0
if [[ $1 ]]; then
    selected_test=$1
fi

base_dir=$(pwd)
cd tests/$selected_test
make
run_tool
cd $base_dir
