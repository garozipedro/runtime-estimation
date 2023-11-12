#! /bin/bash

outdir=$1
bench_seq=$2
iter=$3

suffix=${bench_seq}.${iter}
time_path=$(pwd)/time.out
rand=${RANDOM}

cp ${time_path} ${outdir}/time.${suffix}.${rand}

