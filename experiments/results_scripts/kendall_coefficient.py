#!/usr/bin/python3

import os, argparse
import csv
from scipy.stats import kendalltau
from scipy.stats import spearmanr
import numpy as np
import yaml

def simple_correlation(la, lb):
    indices = sorted(range(len(la)), key=lambda i: la[i])
    slb = [lb[i] for i in reversed(indices)]
    hits = 0
    misses = 0
    for idx, b in enumerate(slb):
        for prev in slb[:idx]:
            if prev > b: hits += 1
            elif prev < b: misses += 1
        for succ in slb[idx:]:
            if succ < b: hits += 1
            elif succ > b: misses += 1
    result=(hits*100)/(hits+misses)
    return result

def init_argparse() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        usage="%(prog)s [--csvs PATH] [--mca PATH] [-o OUTPUT_FILE]",
        description="Compute kendall's tau coefficient for wularus estimatives against runs with perf (cycles) or time."
    )
    parser.add_argument('--csvs', dest='csvs_dir', default='')
    parser.add_argument('--benches', dest='benches', default='')
    parser.add_argument('--output', dest='output_dir', default='kendalls_total')
    return parser


args = init_argparse().parse_args()

g_count = 0
total_perf = []
total_mcacycles = []
total_mcauops = []
total_mcathroughput = []
total_uicathroughput = []

avg_kendall_mcacycles = 0
avg_kendall_mcauops = 0
avg_kendall_mcathroughput = 0
avg_kendall_uicathroughput = 0

avg_spearman_mcacycles = 0
avg_spearman_mcauops = 0
avg_spearman_mcathroughput = 0
avg_spearman_uicathroughput = 0

avg_simple_mcacycles = 0
avg_simple_mcauops = 0
avg_simple_mcathroughput = 0
avg_simple_uicathroughput = 0


cost_options = ['latency', 'recipthroughput', 'codesize', 'sizeandlatency', 'kmeans']

avg_kendall_wl = dict()
avg_spearman_wl = dict()
avg_simple_wl = dict()
total_wl = dict()
for cost in cost_options:
    avg_kendall_wl[cost] = 0
    avg_spearman_wl[cost] = 0
    avg_simple_wl[cost] = 0
    total_wl[cost] = []

for bench in args.benches.split(','):
    perf_data = []
    mca_cycles = []
    mca_uops = []
    mca_throughput = []
    uica_throughput = []
    wl_data = dict()
    for cost in cost_options:
        wl_data[cost] = []

    print(bench)
    for seq in range(1, 15):
        if seq == 8 or seq == 9: continue
        perf_path = f'{args.csvs_dir}/perf.{bench}.b_{seq}.csv'
        mca_path  = f'{args.csvs_dir}/mca.{bench}.b_{seq}.csv'
        uica_path = f'{args.csvs_dir}/uica.{bench}.b_{seq}.csv'
        wl_path   = f'{args.csvs_dir}/{bench}.{seq}.yaml'

        # Check if all necessary CSV files exist for this iteration
        if not os.path.isfile(perf_path):
            print(f'Path not found for: {perf_path}')
            continue
        if not os.path.isfile(mca_path):
            print(f'Path not found for: {mca_path}')
            continue
        if not os.path.isfile(uica_path):
            print(f'Path not found for: {uica_path}')
            continue
        if not os.path.isfile(wl_path):
            print(f'Path not found for: {wl_path}')
            continue

        with open(perf_path, 'r') as perf_csv, open(mca_path, 'r') as mca_csv, open(uica_path, 'r') as uica_csv, open(wl_path, 'r') as wl_yaml:
            perf_reader = csv.DictReader(perf_csv)
            mca_reader  = csv.DictReader( mca_csv)
            uica_reader = csv.DictReader(uica_csv)
            wl_reader   = yaml.safe_load(wl_yaml)

            for row in perf_reader:
                perf_data.extend([float(row['cycles'])])

            for row in mca_reader:
                mca_cycles     .extend([float(row ['cycles'])])
                mca_uops       .extend([float(row ['uops'])])
                mca_throughput .extend([float(row ['throughput'])])

            for row in uica_reader:
                uica_throughput.extend([float(row['throughput'])])

            for option in wl_reader['Cost_options']:
                cost_option = option['Option']['Name'].lower()
                wl_data[cost_option].extend([float(option['Option']['Total cost'])])


    # Calculate Kendall's tau coefficient
    if len(perf_data) == len(mca_cycles) == len(uica_throughput) == len(wl_data['latency']):
        # For global kendall computation.
        g_count += 1
        total_perf.extend(perf_data)
        total_mcacycles.extend(mca_cycles)
        total_mcauops.extend(mca_uops)
        total_mcathroughput.extend(mca_throughput)
        total_uicathroughput.extend(uica_throughput)
        for cost in cost_options:
            total_wl[cost].extend(wl_data[cost])
        # KENDALL
        ################################################################################################################
        perf_cycles_kendall,_ = kendalltau(perf_data, mca_cycles)
        avg_kendall_mcacycles += perf_cycles_kendall
        perf_uops_kendall,_ = kendalltau(perf_data, mca_uops)
        avg_kendall_mcauops += perf_uops_kendall
        perf_throughput_kendall,_ = kendalltau(perf_data, mca_throughput)
        avg_kendall_mcathroughput += perf_throughput_kendall
        print("Kendall coefficient mca_cycles:", perf_cycles_kendall)
        print("Kendall coefficient mca_uops:", perf_uops_kendall)
        print("Kendall coefficient mca_throughput:", perf_throughput_kendall)
        perf_throughput_kendall,_ = kendalltau(perf_data, uica_throughput)
        avg_kendall_uicathroughput += perf_throughput_kendall
        print("Kendall coefficient uica:", perf_throughput_kendall)
        for cost in cost_options:
            wl_kendall,_ = kendalltau(perf_data, wl_data[cost])
            avg_kendall_wl[cost] += wl_kendall
            print(f'Kendall coefficient wl_{cost}:', wl_kendall)


        # SPEARMAN
        ################################################################################################################
        perf_cycles_spearman,_ = spearmanr(perf_data, mca_cycles)
        avg_spearman_mcacycles += perf_cycles_spearman
        perf_uops_spearman,_ = spearmanr(perf_data, mca_uops)
        avg_spearman_mcauops += perf_uops_spearman
        perf_throughput_spearman,_ = spearmanr(perf_data, mca_throughput)
        avg_spearman_mcathroughput += perf_throughput_spearman
        print("Spearman coefficient mca_cycles:", perf_cycles_spearman)
        print("Spearman coefficient mca_uops:", perf_uops_spearman)
        print("Spearman coefficient mca_throughput:", perf_throughput_spearman)
        perf_throughput_spearman,_ = spearmanr(perf_data, uica_throughput)
        avg_spearman_uicathroughput += perf_throughput_spearman
        print("Spearman coefficient uica:", perf_throughput_spearman)
        for cost in cost_options:
            wl_spearman,_ = spearmanr(perf_data, wl_data[cost])
            avg_spearman_wl[cost] += wl_spearman
            print(f'Spearman coefficient wl_{cost}:', wl_spearman)

        # SIMPLE
        ################################################################################################################
        perf_cycles_simple = simple_correlation(perf_data, mca_cycles)
        avg_simple_mcacycles += perf_cycles_simple
        perf_uops_simple = simple_correlation(perf_data, mca_uops)
        avg_simple_mcauops += perf_uops_simple
        perf_throughput_simple = simple_correlation(perf_data, mca_throughput)
        avg_simple_mcathroughput += perf_throughput_simple
        print("Simple coefficient mca_cycles:", perf_cycles_simple)
        print("Simple coefficient mca_uops:", perf_uops_simple)
        print("Simple coefficient mca_throughput:", perf_throughput_simple)
        perf_throughput_simple = simple_correlation(perf_data, uica_throughput)
        avg_simple_uicathroughput += perf_throughput_simple
        print("Simple coefficient uica:", perf_throughput_simple)
        for cost in cost_options:
            wl_simple = simple_correlation(perf_data, wl_data[cost])
            avg_simple_wl[cost] += wl_simple
            print(f'Simple coefficient wl_{cost}:', wl_simple)

    else:
        print("Error: Lengths of lists do not match.")
        print(f'perf = {len(perf_data)}')
        print(f'mca = {len(mca_cycles)}')
        print(f'uica = {len(uica_throughput)}')
        print(f'wl = {len(wl_data["latency"])}')
        print(f'wl = {len(wl_data["recipthroughput"])}')
        print(f'wl = {len(wl_data["kmeans"])}')
        print(perf_data)
        print(mca_cycles)
        print(wl_data["latency"])
        print(wl_data["kmeans"])
    print('------------------------------------------\n')

# KENDALL AVG
########################################################################################################################
print('KENDALL AVG:')
print(f'mca: cycles = {avg_kendall_mcacycles / g_count} // uops = {avg_kendall_mcauops / g_count} // throughput = {avg_kendall_mcathroughput / g_count} // uica = {avg_kendall_uicathroughput / g_count}')
for cost in cost_options:
    print(f'wl_{cost} = {avg_kendall_wl[cost] / g_count}')

# SPEARMAN AVG
########################################################################################################################
print('SPEARMAN AVG:')
print(f'mca: cycles = {avg_spearman_mcacycles / g_count} // uops = {avg_spearman_mcauops / g_count} // throughput = {avg_spearman_mcathroughput / g_count} // uica = {avg_spearman_uicathroughput / g_count}')
for cost in cost_options:
    print(f'wl_{cost} = {avg_spearman_wl[cost] / g_count}')

# SIMPLE AVG
########################################################################################################################
print('SIMPLE AVG:')
print(f'mca: cycles = {avg_simple_mcacycles / g_count} // uops = {avg_simple_mcauops / g_count} // throughput = {avg_simple_mcathroughput / g_count} // uica = {avg_simple_uicathroughput / g_count}')
for cost in cost_options:
    print(f'wl_{cost} = {avg_simple_wl[cost] / g_count}')


print('TOTAL:')
if len(total_perf) == len(total_mcauops) == len(total_uicathroughput) == len(total_wl['latency']):
    perf_cycles_kendall,_ = kendalltau(total_perf, total_mcacycles)
    perf_uops_kendall,_ = kendalltau(total_perf, total_mcauops)
    perf_throughput_kendall,_ = kendalltau(total_perf, total_mcathroughput)
    print("Kendall coefficient mca_cycles:", perf_cycles_kendall)
    print("Kendall coefficient mca_uops:", perf_uops_kendall)
    print("Kendall coefficient mca_throughput:", perf_throughput_kendall)
    perf_throughput_kendall,_ = kendalltau(total_perf, total_uicathroughput)
    print("Kendall coefficient uica:", perf_throughput_kendall)
    for cost in cost_options:
        total_wl_kendall,_ = kendalltau(total_perf, total_wl[cost])
        print(f'Kendall coefficient wl_{cost}:', total_wl_kendall)
else:
    print("Error: Lengths of lists do not match.")

# # Calculate Kendall's tau coefficient for each mca measurement against time and perf.
# with open(f'{args.output_dir}/perf.mca.cycles', 'w') as outfile:
#     tau, _ = kendalltau(perf_data, mca_cycles, variant='b')
#     print(f'tau = {tau} p = {_}')
#    outfile.write(f'{tau}\n')

# with open(f'{args.output_dir}/perf.mca.uops', 'w') as outfile:
#     tau, _ = kendalltau(perf_data, mca_uops)
#     outfile.write(f'{tau}\n')

# with open(f'{args.output_dir}/perf.mca.throughput', 'w') as outfile:
#     tau, _ = kendalltau(perf_data, mca_throughput)
#     outfile.write(f'{tau}\n')

# with open(f'{args.output_dir}/time.mca.cycles', 'w') as outfile:
#     tau, _ = kendalltau(time_data, mca_cycles)
#     outfile.write(f'{tau}\n')

# with open(f'{args.output_dir}/time.mca.uops', 'w') as outfile:
#     tau, _ = kendalltau(time_data, mca_uops)
#     outfile.write(f'{tau}\n')

# with open(f'{args.output_dir}/time.mca.throughput', 'w') as outfile:
#     tau, _ = kendalltau(time_data, mca_throughput)
#     outfile.write(f'{tau}\n')
