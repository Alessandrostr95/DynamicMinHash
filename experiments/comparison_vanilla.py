from buffered_minhash import (
    LegalBufferedMinhash,
    Minhash,
    HashFamily,
    UpdateResult,
)
from time import time
import numpy as np
from tqdm import tqdm
from collections import deque
import json
from pprint import pprint
from pathlib import Path

n = 2**19
k = 2000
l = 32
n_runs = 1


def steady_size(n, capacity, k, vanilla=False):
    hashes = HashFamily(k)
    if vanilla:
        a = Minhash(hashes=hashes)
    else:
        a = LegalBufferedMinhash(capacity=capacity, hashes=hashes)

    A = deque(np.random.randint(0, 2**28, size=n, dtype=np.uint32))
    inserts = np.random.randint(2**28, 2**32, size=n, dtype=np.uint32)

    a.restore(A)

    start = time()
    n_faults = 0

    for i in tqdm(range(2 * n)):
        if i % 2 == 0:
            a.insert(inserts[i // 2])
            A.append(inserts[i // 2])
        else:
            # j = np.random.randint(0, len(A))
            # if a.erase(A.pop(j)) == UpdateResult.NeedsRecovery:
            if a.erase(A.popleft()) == UpdateResult.NeedsRecovery:
                a.restore(A)
                n_faults += 1

    return time() - start, n_faults


def insert_then_delete(n, capacity, k, vanilla=False):
    hashes = HashFamily(k)
    if vanilla:
        a = Minhash(hashes=hashes)
    else:
        a = LegalBufferedMinhash(capacity=capacity, hashes=hashes)

    start = time()
    n_faults = 0

    for i in range(n):
        a.insert(i)

    for i in tqdm(range(n, -1, -1)):
        if a.erase(i) == UpdateResult.NeedsRecovery:
            a.restore(range(i))
            n_faults += 1

    return time() - start, n_faults


results = {}
results["params"] = {"n": n, "n_hashes": k, "buffer_size": l, "n_runs": n_runs}

print("===== STEADY SIZE =====")

results["steady_size"] = {"buffered_minhash": {}, "vanilla": {}}

legal_times = []
legal_faults = []
for i in range(n_runs):
    print(f"\tBuffered Minhash: run {i+1}/{n_runs}")
    t, f = steady_size(n, l, k)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    legal_times.append(t)
    legal_faults.append(f)

print(legal_times)
print(legal_faults)

results["steady_size"]["buffered_minhash"]["times"] = legal_times
results["steady_size"]["buffered_minhash"]["faults"] = legal_faults

print("-" * 40)

vanilla_times = []
vanilla_faults = []
for i in range(n_runs):
    print(f"\tVanilla: run {i+1}/{n_runs}")
    t, f = steady_size(n, 1, k, vanilla=True)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    vanilla_times.append(t)
    vanilla_faults.append(f)

print(vanilla_times)
print(vanilla_faults)

results["steady_size"]["vanilla"]["times"] = vanilla_times
results["steady_size"]["vanilla"]["faults"] = vanilla_faults

print()
print("-" * 40)

print(
    f"Legal: {np.mean(legal_times):.2f} ± {np.std(legal_times):.2f} seconds, "
    f"{np.mean(legal_faults):.2f} ± {np.std(legal_faults):.2f} faults"
)
print(
    f"Vanilla: {np.mean(vanilla_times):.2f} ± {np.std(vanilla_times):.2f} seconds, "
    f"{np.mean(vanilla_faults):.2f} ± {np.std(vanilla_faults):.2f} faults"
)

print("\n\n===== INSERT THEN DELETE =====")

results["insert_then_delete"] = {"buffered_minhash": {}, "vanilla": {}}

legal_times = []
legal_faults = []
for i in range(n_runs):
    print(f"\tBuffered Minhash: run {i+1}/{n_runs}")
    t, f = insert_then_delete(n, l, k)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    legal_times.append(t)
    legal_faults.append(f)

print(legal_times)
print(legal_faults)

results["insert_then_delete"]["buffered_minhash"]["times"] = legal_times
results["insert_then_delete"]["buffered_minhash"]["faults"] = legal_faults

print("-" * 40)

vanilla_times = []
vanilla_faults = []
for i in range(n_runs):
    print(f"\tVanilla: run {i+1}/{n_runs}")
    t, f = insert_then_delete(n, 1, k, vanilla=True)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    vanilla_times.append(t)
    vanilla_faults.append(f)

print(vanilla_times)
print(vanilla_faults)

results["insert_then_delete"]["vanilla"]["times"] = vanilla_times
results["insert_then_delete"]["vanilla"]["faults"] = vanilla_faults

print()
print("-" * 40)

print(
    f"Legal: {np.mean(legal_times):.2f} ± {np.std(legal_times):.2f} seconds, "
    f"{np.mean(legal_faults):.2f} ± {np.std(legal_faults):.2f} faults"
)
print(
    f"Vanilla: {np.mean(vanilla_times):.2f} ± {np.std(vanilla_times):.2f} seconds, "
    f"{np.mean(vanilla_faults):.2f} ± {np.std(vanilla_faults):.2f} faults"
)

pprint("\n\n===== RESULTS =====")
pprint(results)

# ---- SAVE TO FILE ----

# check if the file comparison_vanilla.json already exists, otherwise create it
# the file should be a LIST of dictionaries, each dictionary containing the results of a parameter combination
output_file = Path(__file__).parent / "comparison_vanilla.json"
if output_file.exists():
    with open(output_file, "r") as f:
        old_results = json.load(f)
    if not isinstance(old_results, list):
        raise ValueError(
            f"Expected a list of dictionaries in {output_file}, but got {type(old_results)}"
        )
else:
    old_results = []

old_results.append(results)

with open(output_file, "w") as f:
    json.dump(old_results, f, indent=4)
