#! /usr/bin/python3

'''
generate_seqs.py
Generate random sequences of transformations to be applied by llvm-opt.
'''

import random
import argparse
import subprocess
import os
import yaml as yl

LLVM_FLAGS = [
    '-adce', '-always-inline', '-break-crit-edges', '-codegenprepare', '-constmerge',
    '-dce', '-deadargelim', '-dse', '-function-attrs', '-globaldce',
    '-globalopt', '-gvn', '-indvars', '-inline', '-instcombine',
    '-aggressive-instcombine', '-ipsccp', '-jump-threading', '-lcssa',
    '-licm', '-loop-deletion', '-loop-extract', '-loop-extract-single', '-loop-reduce',
    '-loop-rotate', '-loop-simplify', '-loop-unroll', '-loop-unroll-and-jam', '-lower-global-dtors',
    '-loweratomic', '-lowerinvoke', '-lowerswitch', '-mem2reg', '-memcpyopt',
    '-mergefunc', '-mergereturn', '-partial-inliner', '-prune-eh', '-reassociate',
    '-reg2mem', '-sroa', '-sccp', '-simplifycfg', '-sink',
    '-strip', '-strip-dead-debug-info', '-strip-dead-prototypes', '-strip-debug-declare', '-strip-nondebug',
    '-tailcallelim'
]

default_len_min = 1
default_len_max = 37
default_seqs_total = 5


def generate_seqs(seed, lmin, lmax, total, flags):
    random.seed(seed)
    generated_seqs = dict()
    ret=dict()
    while len(generated_seqs) < total:
        N = random.randint(lmin, lmax)
        new_seq = []
        while len(new_seq) < N:
            new_seq.append(flags[random.randint(0, len(flags) - 1)])
        seq_str = ' '.join(new_seq)
        if seq_str not in generated_seqs:
            ret[len(generated_seqs)] = new_seq
            generated_seqs[seq_str] = None # Add new generated sequence.
    return ret


def init_argparse() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        usage="%(prog)s [--min VAL] [--max VAL] [--seed VAL] [--total VAL] [--flags-file PATH_TO_FILE] [-o OUTPUT_FILE]",
        description="Generate random sequences of optimizations."
    )
    parser.add_argument('--min', dest='len_min', default=default_len_min)
    parser.add_argument('--max', dest='len_max', default=default_len_max)
    parser.add_argument('--total', dest='total', default=default_seqs_total)
    parser.add_argument('--flags-file', dest='flags_file', default=None)
    parser.add_argument('--output', '-o', dest='output', default='output.yaml')
    parser.add_argument('--seed', dest='seed')
    return parser


args = init_argparse().parse_args()

if args.flags_file != None:
    with open(args.flags_file) as f:
        flags = yl.safe_load(f)
else:
    flags = LLVM_FLAGS

print(flags)
seqs = generate_seqs(args.seed, int(args.len_min), int(args.len_max), int(args.total), flags)
data=dict()
data['metodology'] = dict()
data['metodology']['used flags'] = flags
data['metodology']['minimum sequence length'] = args.len_min
data['metodology']['maximum sequence length'] = args.len_max
data['metodology']['total sequences'] = args.total
data['sequences']=seqs
with open(args.output,'w') as f:
    yl.dump(data,f)
