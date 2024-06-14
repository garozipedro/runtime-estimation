#!/usr/bin/python3

from sklearn.cluster import KMeans
from sklearn.decomposition import PCA
import argparse
import glob, os
import matplotlib.pyplot as plt
import numpy as np
import yaml
from joblib import dump, load

def init_argparse() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        usage=
        "%(prog)s --train-dir VAL --output-dir VAL --granularity-fun",
        description="kmeans."
    )
    parser.add_argument('--train-dir', dest='train_dir')
    parser.add_argument('--output-dir', dest='output_dir')
    parser.add_argument('--granularity-fun', dest='granularity_fun', action='store_true')
    return parser


args = init_argparse().parse_args()

instrumentation_data = []
if args.train_dir:
    granularity = 'fun' if args.granularity_fun else 'bb'
    for file_path in glob.glob(os.path.join(args.train_dir, '*.yaml')):
        try:
            with open(file_path) as train_file:
                print(f'Loading {file_path}...')
                yaml_file = yaml.safe_load(train_file)
                instrumentation_data.extend(yaml_file['Instrumentation_data'])
        except:
            print(f"Couldn't open train file {train}\n")

program_representation = []
cycles_data = []

for data in instrumentation_data:
    for bb in data['Function']['BasicBlocks']:
        bb_cycles = bb['BasicBlock']['Cycles']
        if bb_cycles == 0:
            continue
        bb_representation = [0] * 68
        for opcode_dict in bb['BasicBlock']['OpCodes']:
            for opcode, count in opcode_dict.items():
                opcode_index = int(opcode)
                if 0 <= opcode_index < len(bb_representation):
                    bb_representation[opcode] += count
                else:
                    print(f'Opcode index {opcode_index} is out of bounds')
        program_representation.append(bb_representation)
        cycles_data.append(bb_cycles)

np_data = np.array(program_representation)
pca = PCA(n_components=2)
kmeans_data = pca.fit_transform(np_data)

num_clusters = 50
kmeans = KMeans(n_clusters=num_clusters)
kmeans.fit(kmeans_data)
centroids = kmeans.cluster_centers_
labels = kmeans.labels_

average_cycles_per_cluster = []
for i in range(num_clusters): # Compute average cycles per cluster.
    cluster_cycles = [cycles_data[j] for j in range(len(labels)) if labels[j] == i]
    average_cycles = np.mean(cluster_cycles) if cluster_cycles else 0
    average_cycles_per_cluster.append(average_cycles)
    print(f"Cluster {i} has an average of {average_cycles:.2f} cycles.")

output_cycles_path = os.path.join(args.output_dir, 'average_cycles_per_cluster.csv')
with open(output_cycles_path, 'w') as file: # Save the average cycles per cluster to the output directory
    file.write(f"cluster,cycles\n")
    for i, avg_cycles in enumerate(average_cycles_per_cluster):
        file.write(f"{i},{avg_cycles:.2f}\n")


# Plot the clusters
for i in range(num_clusters):
    # Get data points for the current cluster
    cluster_points = kmeans_data[labels == i]
    # Plot the data points for the current cluster
    plt.scatter(cluster_points[:, 0], cluster_points[:, 1], label=f'Cluster {i}')

# Plot the centroids
plt.scatter(centroids[:, 0], centroids[:, 1], marker='x', color='black', label='Centroids')

# Add labels and legend
plt.xlabel('Opcode 1')
plt.ylabel('Opcode 2')
plt.title('KMeans Clustering of Opcode Data')
plt.legend()
plt.grid(True)

# Show the plot
plt.show()


# Save the PCA model.
pca_model_filename = os.path.join(args.output_dir, 'pca_model.joblib')
dump(pca, pca_model_filename)
print(f"Saved PCA model to {pca_model_filename}")

# Save the trained KMeans model to a file
model_filename = os.path.join(args.output_dir, 'kmeans_model.joblib')
dump(kmeans, model_filename)
print(f"Trained KMeans model saved to {model_filename}")
