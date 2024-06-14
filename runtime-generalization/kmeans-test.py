#!/usr/bin/python3

import os, argparse
from joblib import load
import numpy as np
from sklearn.decomposition import PCA
import yaml, csv

#../experiments/cBench/cBench_freqs/automotive_qsort1.10.yaml

def init_argparse() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        usage="%(prog)s --train-dir VAL --output-dir VAL [--granularity-fun]",
        description="KMeans clustering on opcode data."
    )
    parser.add_argument('--kmeans-dir', dest='kmeans_dir', required=True, help='Directory to save output results.')
    parser.add_argument('--func', dest='func', action='store_true')
    parser.add_argument('--target', dest='target', required=True)
    return parser

args = init_argparse().parse_args()

# Load the PCA and KMeans model previously trained.
#-----------------------------------------------------------------------------------------------------------------------
# Kmeans.
model_filename = os.path.join(args.kmeans_dir, 'kmeans_model.joblib')
kmeans = load(model_filename)
#print(f"Loaded KMeans model from {model_filename}")
# PCA.
pca_filename = os.path.join(args.kmeans_dir, 'pca_model.joblib')
pca = load(pca_filename)
#print(f"Loaded PCA model from {pca_filename}")
# AVG cluster cycles.
cluster_avg_cost = {}
clusters_filename = os.path.join(args.kmeans_dir, 'average_cycles_per_cluster.csv')
with open(clusters_filename, mode='r') as csv_file:
    csv_reader = csv.DictReader(csv_file)
    for row in csv_reader:
        cluster = int(row['cluster'])
        cycles = float(row['cycles'])
        cluster_avg_cost[cluster] = cycles
with open(args.target, 'r') as target_file:  # Update with your path
    target_yaml = yaml.safe_load(target_file)


# Utility: Convert the histograms to pca representation.
#-----------------------------------------------------------------------------------------------------------------------
def histogram2repr(histogram):
    bb_representation = [0] * 68
    for opcode_dict in histogram:
        for opcode, count in opcode_dict.items():
            bb_representation[int(opcode)] += count
    return pca.transform(np.array(bb_representation).reshape(1, -1))


# Calculate the total estimated cost
#-----------------------------------------------------------------------------------------------------------------------
total_cost = 0

for function in target_yaml['Module']['Functions']:
    function_name = function['Function']['Name']
    if args.func:
        histogram = [0] * 68
        for basic_block in function['Function']['BasicBlocks']: # Group all histograms by function.
            bb_histogram = basic_block['BasicBlock']['Histogram']
            for opcode_dict in bb_histogram:
                for opcode, count in opcode_dict.items():
                    histogram[int(opcode)] += count
        frequency = function['Function']['Freq']
        fun_representation = pca.transform(np.array(histogram).reshape(1, -1))
        cluster_label = kmeans.predict(fun_representation)[0]
        # Get the average cost for the cluster
        avg_cost = cluster_avg_cost[cluster_label]
        estimated_cost = avg_cost * frequency
        total_cost += estimated_cost
    else: # By default use basic block representation.
        for basic_block in function['Function']['BasicBlocks']:
            histogram = basic_block['BasicBlock']['Histogram']
            frequency = basic_block['BasicBlock']['Freq']
            # Preprocess histogram and predict cluster
            bb_representation = histogram2repr(histogram)
            cluster_label = kmeans.predict(bb_representation)[0]
            # Get the average cost for the cluster
            avg_cost = cluster_avg_cost[cluster_label]
            estimated_cost = avg_cost * frequency
            total_cost += estimated_cost

print(f"{total_cost}")
