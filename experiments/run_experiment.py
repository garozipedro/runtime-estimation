#! /usr/bin/python3

'''
WARNING: Before running this script, you MUST source shrc from spec base dir, so that runcpu is available.

specspeed:
  .intspeed:
    600.perlbench_s, 602.gcc_s, 605.mcf_s, 620.omnetpp_s, 623.xalancbmk_s, 625.x264_s, 631.deepsjeng_s, 641.leela_s, 657.xz_s, 998.specrand_is.
    F: 648.exchange2_s
  .fpspeed:
    619.lbm_s, 638.imagick_s, 644.nab_s, 996.specrand_fs.
    F: 603.bwaves_s, 607.cactuBSSN_s, 621.wrf_s, 627.cam4_s, 628.pop2_s, 649.fotonik3d_s, 654.roms_s.

C/C++ benchs:
600.perlbench_s,602.gcc_s,605.mcf_s,620.omnetpp_s,623.xalancbmk_s,625.x264_s,631.deepsjeng_s,641.leela_s,657.xz_s,998.specrand_is,619.lbm_s,638.imagick_s,644.nab_s,996.specrand_fs
'''

import argparse
import os
import random
import subprocess
import yaml as yl

# Experiment arguments.
def init_argparse() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        usage=
        "%(prog)s [--iterations VAL] [--benchmarks VAL] [--flags-file PATH_TO_FILE]"
        "[--result-dir PATH_TO_DIR] [--spec17-dir PATH_TO_SPEC_CPU] [--llvm-dir PATH_TO_LLVM]"
        "[--experiments {time,gprof,wl,uica,mca} default=all] [--test] [--fake] [--O-flags]",
        description="Run specified experiments."
    )
    parser.add_argument('--iterations', dest='iterations', default=3)
    parser.add_argument('--benchmarks', dest='benchmarks', default="602.gcc_s")
    parser.add_argument('--flags-file', dest='flags_file', default=None)
    parser.add_argument('--result-dir', dest='result_dir', default=None)
    parser.add_argument('--spec17-dir', dest='spec17_dir', default=None, required=True)
    parser.add_argument('--llvm-dir', dest='llvm_dir', default='/usr')
    parser.add_argument('--experiments', dest='experiments', default='all')
    parser.add_argument('--test', dest='use_test_size', action='store_true')
    parser.add_argument('--fake', dest='fake_cmd', action='store_true')
    parser.add_argument('--O-flags', dest='o_flags', action='store_true')
    return parser

args = init_argparse().parse_args()

# Get optimization flags.
sequences = []
if args.flags_file:
    try:
        with open(args.flags_file) as f:
            sequences_yaml = yl.safe_load(f)
            sequences = sequences_yaml['sequences']
    except:
        print('Sequences file not found\n')


line_separator = '\n'+'*'*120+'\n'
script_dir = os.getcwd()

# Create a file meaning the sequence has already run,
# this is for resuming the experiment when it was killed before completion.
def create_completed_file(path):
    print(f'Completed execution of {path}')
    os.system(f'touch {path}/complete')


# Run the runcpu command.
def run_cpu(is_o_seq, seq, seq_name, experiment, benchmark):
    output_path = f'{args.result_dir}/{experiment}/{benchmark}.{seq_name}'
    os.makedirs(output_path, exist_ok=True)
    if os.path.exists(f'{output_path}/complete'):
        print(f'Skipping {benchmark}.{seq_name}: found {output_path}/complete')
        return
    wl_size = 'test' if args.use_test_size else 'ref'
    fake_str = '--fake' if args.fake_cmd else ''
    dash = '-' if is_o_seq else ''
    only_build = '--action build' if experiment == 'mca' else ''
    cmd_str = (
        f"runcpu {fake_str} --loose {only_build} --define SEQ_NAME='{seq_name}' --define OPT_FLAGS='{dash}{seq}'"
        f" --define OUTPUT_DIR={output_path} --config experiment-{experiment}.cfg"
        f" --define llvm_dir={args.llvm_dir}"
        f" --define CP_{experiment.upper()}_SH={script_dir}/cp_{experiment}.sh"
        f" --iterations {args.iterations} --size {wl_size} {benchmark}"
    )
    print('*' * 120); print('=' * 120)
    print(cmd_str)
    print('=' * 120); print('*' * 120)
    os.chdir(f'{args.spec17_dir}/config')
    os.system(cmd_str)
    if args.fake_cmd == False:
        create_completed_file(output_path)


# Check if user arguments are valid
def get_valid_set(selected, options_list):
    result = set()
    for selection in selected.split(','):
        selection = selection.strip()
        if len(selection) == 0: continue
        if selection in options_list:
            result.add(selection)
        elif selection == 'all':
            result.update(options_list)
    return result


# Run the experiment as specified by the arguments.
def run_experiment():
    # Get selected experiments.
    experiments = get_valid_set(args.experiments, ['time', 'gprof', 'perf', 'mca', 'uica', 'wl']);
    # Get selected benchmarks.
    benchmarks = get_valid_set(args.benchmarks, [
            '600', '602', '605', '620', '623', '625', '631', '641', '657', '998', '619', '638', '644', '996'
    ])
    if (len(benchmarks) == 0): # Maybe full name?
        benchmarks = get_valid_set(args.benchmarks, [
            '600.perlbench_s','602.gcc_s','605.mcf_s','620.omnetpp_s','623.xalancbmk_s','625.x264_s','631.deepsjeng_s',
            '641.leela_s','657.xz_s','998.specrand_is','619.lbm_s','638.imagick_s','644.nab_s','996.specrand_fs'
        ])
    if (len(benchmarks) == 0): # benchmark set (specspeed, intspeed, ...)?
        benchmarks = {args.benchmarks}

    if args.result_dir == None: # Create output directory if none is supplied.
        args.result_dir = f'{os.getcwd()}/output_{random.randint(0, 1000000)}'
        if (args.fake_cmd == False):
            os.mkdir(args.result_dir)

    o_seqs = ['O0','O1','O2','O3'] if args.o_flags else []
#    o_seqs = ['O3'] if args.o_flags else []
    for benchmark in benchmarks:
        benchmark = benchmark.split('.')[0] # Use only the benchmark number.
        for exp in experiments:
            print(f'{line_separator}Running [{exp}] experiment...')
            if exp == 'mca':
                print('llvm-mca will run during spec build, so it won\'t report running the benchmark.')
            if exp == 'perf':
                paranoid_level = int(subprocess.check_output("cat /proc/sys/kernel/perf_event_paranoid", shell=True))
                if paranoid_level > 0:
                    print(f'Perf paranoid level should be below 1. Currently it is: {paranoid_level}\n'
                          'Skipping perf experiment...')
                    continue
            print(line_separator)
            for seq in o_seqs:
                run_cpu(True, seq, seq, exp, benchmark)
            for seq in sequences:
                seq_name = f'S{seq}'
                seq_str = ' '.join(sequences[seq])
                run_cpu(False, seq_str, seq_name, exp, benchmark)


run_experiment()
