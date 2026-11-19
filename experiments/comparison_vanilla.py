from buffered_minhash import (
    LegalBufferedMinhash,
    BufferedMinhash,
    HashFamily,
    UpdateResult,
)
from time import time
import numpy as np
from tqdm import tqdm
from collections import deque

n = 2**16
k = 2000
l = 32
n_runs = 5


def steady_size(n, capacity, k, legal=False):
    hashes = HashFamily(k)
    if legal:
        a = LegalBufferedMinhash(capacity=capacity, hashes=hashes)
    else:
        a = BufferedMinhash(capacity=capacity, hashes=hashes)

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


def insert_then_delete(n, capacity, k, legal=False):
    hashes = HashFamily(k)
    if legal:
        a = LegalBufferedMinhash(capacity=capacity, hashes=hashes)
    else:
        a = BufferedMinhash(capacity=capacity, hashes=hashes)

    start = time()
    n_faults = 0

    for i in range(n):
        a.insert(i)

    for i in tqdm(range(n, -1, -1)):
        if a.erase(i) == UpdateResult.NeedsRecovery:
            a.restore(range(i))
            n_faults += 1

    return time() - start, n_faults

print("===== STEADY SIZE =====")

legal_times = []
legal_faults = []
for i in range(n_runs):
    print(f"\tBuffered Minhash: run {i+1}/{n_runs}")
    t, f = steady_size(n, l, k, legal=True)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    legal_times.append(t)
    legal_faults.append(f)

print(legal_times)
print(legal_faults)

print("-" * 40)

vanilla_times = []
vanilla_faults = []
for i in range(n_runs):
    print(f"\tVanilla: run {i+1}/{n_runs}")
    t, f = steady_size(n, 1, k, legal=False)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    vanilla_times.append(t)
    vanilla_faults.append(f)

print(vanilla_times)
print(vanilla_faults)

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

legal_times = []
legal_faults = []
for i in range(n_runs):
    print(f"\tBuffered Minhash: run {i+1}/{n_runs}")
    t, f = insert_then_delete(n, l, k, legal=True)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    legal_times.append(t)
    legal_faults.append(f)

print(legal_times)
print(legal_faults)

print("-" * 40)

vanilla_times = []
vanilla_faults = []
for i in range(n_runs):
    print(f"\tVanilla: run {i+1}/{n_runs}")
    t, f = insert_then_delete(n, 1, k, legal=False)
    print(f"\t\ttime, faults: {t:.2f}, {f}")
    vanilla_times.append(t)
    vanilla_faults.append(f)

print(vanilla_times)
print(vanilla_faults)

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
