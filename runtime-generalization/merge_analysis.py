#! /usr/bin/python3

import argparse
import os
import yaml

def init_argparse() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        usage=
        "%(prog)s --histogram-file VAL --analysis-file",
        description="kmeans."
    )
    parser.add_argument('--histogram-file', dest='hist_file')
    parser.add_argument('--analysis-file', dest='analysis_file')
    parser.add_argument('--output-dir', dest='output')
    return parser


args = init_argparse().parse_args()

def merge_cycles(histogram_file, analysis_file, output_file):
    with open(histogram_file, 'r') as file:
        hist_data = yaml.safe_load(file)
    with open(analysis_file, 'r') as file:
        analysis_data = yaml.safe_load(file)

    # The analysis_data has the runtime cicles data we want to merge in the histogram data.
    cycles_data = {}
    for function_data in analysis_data['Runtime_data']['Functions']:
        fun_name = function_data['Function']['Name']
        for bb_data in function_data['Function']['BasicBlocks']:
            bb = bb_data['BasicBlock']
            cycles_data[fun_name] = bb['Average']

    # Merge cycles_data into the hist_data
    for function_data in hist_data['Instrumentation_data']:
        fun_name = function_data['Function']['Name']
        for basic_block in function_data['Function']['BasicBlocks']:
            bb = basic_block['BasicBlock']
            if fun_name  in cycles_data:
                bb['Cycles'] = cycles_data[fun_name]
            else: # The bb may have not been executed during the dynamic analysis.
                bb['Cycles'] = 0

    # Dump merged file.
    with open(output_file, 'w') as file:
        yaml.dump(hist_data, file)

if not os.path.isfile(args.analysis_file):
    print(f'File {args.analysis_file} not found')
    exit(1)

if not os.path.isfile(args.hist_file):
    print(f'File {args.hist_file} not found')
    exit(1)

merge_cycles(args.hist_file, args.analysis_file, args.output)
