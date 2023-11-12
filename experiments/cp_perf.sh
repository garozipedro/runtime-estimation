#! /bin/bash

outdir=$1
bench_seq=$2
iter=$3

suffix=${bench_seq}.${iter}
perf_path=$(pwd)/perf.out
rand=${RANDOM}

cp ${perf_path} ${outdir}/perf.${suffix}.${rand}

