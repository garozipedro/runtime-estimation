#!/bin/bash

run_dir=$1
build_dir=$2

granularity='function'

for bench in $(ls $run_dir); do
    for prog in $(ls $run_dir/$bench); do
        build_number=$(echo $prog | sed -n 's/.*b_\([0-9]*\).*/\1/p')
        baseprog=$(echo $prog | cut -f1 -d.)
        build_path=$baseprog.$build_number/$granularity.histogram.yaml

        histogram_path=$build_dir/$bench/$build_path
        analysis_path=$run_dir/$bench/$prog/$granularity.analysis.yaml

        output=$bench.$prog.yaml
        ./merge_analysis.py --histogram-file $histogram_path --analysis-file $analysis_path --output train_data/$output
    done
done
