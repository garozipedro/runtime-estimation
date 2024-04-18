#! /bin/bash

outdir=$1
bench_seq=$2
iter=$3

suffix=${bench_seq}
mca_path=$(pwd)/mca.out

cp ${mca_path} ${outdir}/mca.${suffix}

