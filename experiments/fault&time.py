from buffered_minhash import (
    LegalBufferedMinhash,
    LegalTreeBufferedMinhash,
    HashFamily,
    UpdateResult,
)
from tqdm import tqdm
import numpy as np
from time import time
import csv

n = 2**16
ks = np.array([1000, 2000])
ls = np.arange(5, 201, 5)
n_runs = 100


def insert_then_delete(n, capacity, k, tree=False):
    hashes = HashFamily(k)
    if tree:
        a = LegalTreeBufferedMinhash(capacity=capacity, hashes=hashes)
    else:
        a = LegalBufferedMinhash(capacity=capacity, hashes=hashes)

    n_faults = 0
    start = time()

    for i in range(n):
        a.insert(i)

    for i in range(n, -1, -1):
        if a.erase(i) == UpdateResult.NeedsRecovery:
            a.restore(range(i))
            n_faults += 1

    return time() - start, n_faults


csvfile = open("results_fault_time.csv", "w", newline="")
writer = csv.writer(csvfile)
writer.writerow(
    [
        "k",
        "l",
        "time_tree_mean",
        "time_tree_std",
        "faults_tree_mean",
        "faults_tree_std",
        "time_array_mean",
        "time_array_std",
        "faults_array_mean",
        "faults_array_std",
    ]
)

for k in ks:
    for l in ls:
        times_tree = []
        faults_tree = []

        times_array = []
        faults_array = []

        for _ in tqdm(range(n_runs), desc=f"Run for k={k}, l={l}"):
            t, f = insert_then_delete(n, l, k, tree=True)
            times_tree.append(t)
            faults_tree.append(f)

            t, f = insert_then_delete(n, l, k, tree=False)
            times_array.append(t)
            faults_array.append(f)

        times_tree_mean = np.mean(times_tree)
        times_tree_std = np.std(times_tree)
        faults_tree_mean = np.mean(faults_tree)
        faults_tree_std = np.std(faults_tree)

        times_array_mean = np.mean(times_array)
        times_array_std = np.std(times_array)
        faults_array_mean = np.mean(faults_array)
        faults_array_std = np.std(faults_array)

        writer.writerow(
            [
                k,
                l,
                times_tree_mean,
                times_tree_std,
                faults_tree_mean,
                faults_tree_std,
                times_array_mean,
                times_array_std,
                faults_array_mean,
                faults_array_std,
            ]
        )

csvfile.close()
