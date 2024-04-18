#! /bin/bash

outdir=$1
bench_seq=$2
iter=$3
cmdexe=$4

suffix=${bench_seq}.${iter}
gmon_path=$(pwd)/gmon.out
time_path=$(pwd)/time.out
exe_path=$(pwd)/${cmdexe}
exe=${bench_seq}


rand=${RANDOM}
cp ${time_path} ${outdir}/time.${suffix}.${rand}
cp ${gmon_path} ${outdir}/gmon.${suffix}.${rand}
if [ ! -f ${outdir}/${exe} ]; then
    cp ${exe_path} ${outdir}/${exe}
fi
