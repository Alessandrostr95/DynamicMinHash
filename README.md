# BufferedMinhash

A C++20, header-only implementation of the **ell-buffered k-MinHash** sketch.
Both array and tree buffers support general set updates, including redundant
insertions and deletion of absent elements. Specialized legal-stream variants
store only hashes when the caller guarantees nonredundant updates. The application
owns the underlying set; sketches request explicit recovery when needed.
The library also provides vanilla `Minhash`, without buffers, with selective
recovery of only the signature coordinates lost during deletion.

The algorithm is described in [Maintaining k-MinHash Signatures over Fully-Dynamic
Data Streams with Recovery](https://arxiv.org/abs/2407.21614). The original
experimental implementation, including DSS/BSS, is preserved in [legacy/](legacy/).
It is separate from this library and its new correctness tests.

## Build and test

A C++20 compiler and CMake 3.20 or newer are required. No third-party dependencies
are downloaded or required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/buffered_minhash_example
```

For AddressSanitizer and UndefinedBehaviorSanitizer with GCC or Clang:

```sh
cmake -S . -B build-sanitizers -DCMAKE_BUILD_TYPE=Debug \
  -DBUFFERED_MINHASH_ENABLE_SANITIZERS=ON
cmake --build build-sanitizers
ctest --test-dir build-sanitizers --output-on-failure
```

Consumers can add this repository with `add_subdirectory` and link the interface
target `BufferedMinhash::buffered_minhash`. An installed package is also supported:

```sh
cmake --install build --prefix /your/install/prefix
```

```cmake
find_package(BufferedMinhash CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE BufferedMinhash::buffered_minhash)
```

## Python wrapper (optional)

The pybind11 extension exposes the same update/recovery contract. It requires
Python 3.10 or newer, its matching development headers, and a C++20 compiler.
On Fedora, install the system prerequisites before building with pip:

```sh
sudo dnf install gcc-c++ python3-devel python3-pip
```

The [Fedora python3-devel package](https://packages.fedoraproject.org/pkgs/python3.14/python3-devel/)
provides the headers needed to compile Python extensions. If building with a
specific interpreter version, use its matching development package and invoke
pip through that interpreter. For example, for Python 3.14:

```sh
sudo dnf install python3.14-devel
python3.14 -m venv .venv
.venv/bin/python -m pip install .
```

For the default Python interpreter, install into a virtual environment with:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install .
```

If CMake reports that `/usr/include/python3.14/patchlevel.h` cannot be read,
check that the matching development package is installed and its headers are
accessible. Missing headers can also cause follow-up errors such as `list GET
given empty list` and `Could NOT find Python (missing: Interpreter
Development.Module)`. Pip's isolated build environment installs Python build
dependencies, but does not install system development headers. The message
`Defaulting to user installation` only describes pip's destination and is not
the cause of a missing-header error.

The build uses pybind11 and scikit-build-core, following the
[pybind11 CMake packaging workflow](https://pybind11.readthedocs.io/en/stable/compiling.html).
They are build dependencies; the installed module has no Python runtime dependencies.
If both are already installed, `python -m pip install --no-build-isolation .`
can reuse them without creating an isolated build environment.

```python
from buffered_minhash import (
    BufferedMinhash, TreeBufferedMinhash, HashFamily, UpdateResult, similarity,
)

hashes = HashFamily(k=128, seed=42)
a = BufferedMinhash(capacity=16, hashes=hashes)
b = TreeBufferedMinhash(capacity=32, hashes=hashes)
elements = set()

elements.add(10)
a.insert(10)
b.restore(elements)  # also accepts generators
assert similarity(a, b) == 1.0

elements.discard(10)
if a.erase(10) == UpdateResult.NeedsRecovery:
    a.restore(elements)
assert a.signature() is None

# Or let the sketch construct its hashes:
c = BufferedMinhash(k=128, capacity=16, seed=42)  # seed is optional
```

| Update contract | Element domain | Array buffers | Tree buffers | Shared hashes |
| --- | --- | --- | --- | --- |
| General | uint32_t | `BufferedMinhash` | `TreeBufferedMinhash` | `HashFamily` |
| General | uint64_t | `BufferedMinhash64` | `TreeBufferedMinhash64` | `HashFamily64` |
| Legal | uint32_t | `LegalBufferedMinhash` | `LegalTreeBufferedMinhash` | `HashFamily` |
| Legal | uint64_t | `LegalBufferedMinhash64` | `LegalTreeBufferedMinhash64` | `HashFamily64` |

Both widths use 64-bit tabulation hash outputs. The initial binding exposes these
built-in hashes, not arbitrary Python hash callbacks or other C++ hash types.
Methods include `insert`, `erase`, `restore`, `signature`, `copy_signature`,
`state`, `hash_family`, `signature_length`, `buffer_capacity`, `compatible_with`,
and `copy`. Python's `copy.copy` also creates independent buffers sharing hashes.
Docstrings and parameter names are available through `help()`.
Legal variants have the same methods and constructors; their stricter update and
restore preconditions are described [below](#legal-stream-specialization).
All buffered classes for a given element width can share a hash family and compare
signatures with each other using `compatible_with` and `similarity`.
They can also share hashes and compare with vanilla `Minhash` (`uint32_t` inputs)
and `Minhash64` (`uint64_t` inputs). These take `k, seed=None` or `hashes` in their
constructors, without a capacity parameter or `buffer_capacity()` method.
See [vanilla MinHash](#vanilla-minhash-and-selective-recovery) for the recovery contract.

Unlike the C++ view, Python `signature()` returns an **independent list**, or
`None` for a known empty set. Invalid state raises `RuntimeError`; invalid
configuration or incompatible similarity raises `ValueError`. Elements must
support integer indexing and fit the selected unsigned width: negative or
out-of-range values raise `OverflowError`, and bools/floats/strings raise
`TypeError`. The authoritative set remains the application's responsibility.

`restore` consumes an iterable once without materializing the full set. If an
element conversion or iteration fails after reconstruction starts, it propagates
the Python exception and leaves the sketch requiring recovery; retry with a valid
complete iterable. Iterators must not reenter or modify the same sketch. The
wrapper retains the GIL and does not promise concurrent updates.

To build the optional extension directly and run Python plus C++ correctness tests
(with pybind11 installed for the chosen interpreter):

```sh
cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release \
  -DBUFFERED_MINHASH_BUILD_PYTHON=ON
cmake --build build-python
ctest --test-dir build-python --output-on-failure
```

Ordinary C++ builds leave `BUFFERED_MINHASH_BUILD_PYTHON=OFF` and do not require
Python or pybind11. Binding-specific checks are in [tests/test_python.py](tests/test_python.py).

## Shared hashes and construction

```cpp
#include <buffered_minhash/buffered_minhash.hpp>
using namespace buffered_minhash;

auto hashes = make_hash_family<>(128, 42); // k = 128, reproducible seed
BufferedMinhash<> a(16, hashes);           // ell = 16, array buffers
BufferedMinhash<std::uint32_t, BufferKind::Tree> b(32, hashes);

BufferedMinhash<> automatic(128, 16);      // generate a fresh configuration
BufferedMinhash<> peer(16, automatic.hash_family());
BufferedMinhash<> seeded(128, 16, 42);     // equivalent to a's hash configuration
```

The first template parameter is the element type (`uint32_t` by default, also
`uint64_t`). The second selects `BufferKind::Array` (default) or `BufferKind::Tree`.
The third is a concrete hash function type, defaulting to
`TabulationHash<Element, uint64_t>`. Input and hash output widths are independent;
hash outputs may be `uint32_t` or `uint64_t`. All values, including the maximum
integer values, are usable. Smaller element types are planned for a later release.

Hash configurations are immutable and shared via `shared_ptr<const HashFamily<Hash>>`.
No hash tables are copied when sharing them, and updates do not perform virtual
hash dispatch. A copy of a sketch has independent mutable buffers and shares only
the hash configuration. Moves transfer the state; moved-from sketches must be
assigned a configured sketch before reuse. Their state is `NeedsRecovery`, their
configuration is null, and `restore` rejects them.

The signature length is derived from the supplied configuration. Zero capacity,
zero signature length, and null configurations are rejected with `invalid_argument`.

## Updates and recovery

```cpp
std::unordered_set<std::uint32_t> elements;

elements.insert(10);
(void)a.insert(10);
elements.insert(10);
(void)a.insert(10); // harmless duplicate

elements.erase(10);
if (a.erase(10) == UpdateResult::NeedsRecovery) {
    a.restore(elements);
}
```

Include `<unordered_set>` when using this example. A complete executable is in
[examples/recovery.cpp](examples/recovery.cpp).

- Update the authoritative set **before** notifying the sketch. `Ok` describes
  sketch validity, not whether the set changed. No membership or exact-cardinality
  API is provided.
- If a deletion empties any buffer, `erase` returns `NeedsRecovery`. Signature
  access and further updates then throw `logic_error` until `restore` completes.
  This applies even when the authoritative set has become empty.
- `restore(range)` and `restore(first, last)` rebuild **all** buffers from the
  current set after the triggering deletion, using the same hash functions. Input
  must be a coherent snapshot/scan relative to updates. The input is traversed
  once and is neither retained nor copied into a full internal set. Repeated input
  elements are harmless in general variants; legal variants require distinct
  input elements. Iterator/range value types must match `element_type`.
- Restore also accepts an empty set and may replace the represented set while the
  sketch is ready. Individual sketches require external synchronization; shared
  hash configurations must support concurrent const evaluation if used by threads.
- The initial implementation assumes hash evaluation, allocations, container
  operations, and range traversal succeed. There is no rollback or supported
  recovery contract for unexpected exceptions. API argument/state errors are
  checked separately; robust unexpected-exception handling is future work.

General array and tree buffers hold distinct `(hash, element)` pairs in lexicographic
order (the array's physical storage is unordered). Each threshold is an optional
pair: absence means infinity. Deletions retain thresholds even when a buffer is
no longer full. A buffered element's deletion can therefore cause recovery later,
without admitting previously discarded elements above the retained threshold.

## Legal-stream specialization

Use `LegalBufferedMinhash` when the application guarantees **legal set updates**:
insert an element only when it was absent, and erase it only when it was present.
Reinsertion after deletion is allowed. `restore` must enumerate each current
element **exactly once**, including when given a generator or input iterator.
These membership/uniqueness preconditions are **not checked**; violating them
can silently produce incorrect signatures. No internal membership set is kept.

```cpp
// Same header, hash-family type, constructors and recovery API as the general variant.
LegalBufferedMinhash<> legal_array(16, hashes);
LegalBufferedMinhash<std::uint32_t, BufferKind::Tree> legal_tree(16, hashes);
```

The alias selects `StreamKind::Legal` as the fourth compile-time parameter of
`BufferedMinhash`; the default remains `StreamKind::General`. Both contracts
share the update, signature and recovery implementation, without a runtime mode
branch. Legal buffers and finite thresholds store only hash values. Arrays omit
the insertion duplicate search; trees use `std::multiset`. Equal hashes from
distinct elements retain multiplicities, and a deletion removes just one
occurrence. No hash value is reserved for an empty buffer or infinite threshold.

Truncating a collision bucket at the threshold loses element identities. Deleting
any element tied at that threshold consumes one stored occurrence, even if its
identity would have been discarded by a general buffer. This can cause earlier,
conservative recovery. It does not change the minimum hash while the sketch is
Ready under the legal contract. Recover each sketch independently; do not assume
that general and legal variants fault on the same update.

```python
from buffered_minhash import LegalBufferedMinhash, LegalTreeBufferedMinhash

legal = LegalBufferedMinhash(capacity=16, hashes=hashes)
peer = LegalTreeBufferedMinhash(capacity=16, hashes=hashes)
elements = set()

value = 10
if value not in elements:
    elements.add(value)
    legal.insert(value)
peer.restore(elements)  # sets enumerate distinct elements
assert similarity(legal, peer) == 1.0

if value in elements:
    elements.remove(value)
    if legal.erase(value) == UpdateResult.NeedsRecovery:
        legal.restore(elements)
```

Element/hash widths, fault-state guards, copy/move behavior and hash-family
sharing match the general API. The specialization removes identity storage and
duplicate checks; end-to-end time and memory benefits, including recovery cost,
remain to be measured in the experimental phase.

## Vanilla MinHash and selective recovery

[`Minhash<Element, Hash>`](include/buffered_minhash/minhash.hpp) maintains one
minimum and its winning element per hash function, with no buffers or thresholds.
The defaults are `uint32_t` elements and 64-bit tabulation hash outputs, as in the
buffered variants. It supports general set semantics: redundant insertions,
deletions of absent elements, and repeated elements during restoration are harmless.
Hash collisions are resolved by element identity when selecting a winner.

```cpp
#include <buffered_minhash/minhash.hpp>
#include <set>
using namespace buffered_minhash;

auto hashes = make_hash_family<>(128, 42);
Minhash<> sketch(hashes);                  // shares hashes; no capacity argument
Minhash<> seeded(128, 42);                 // k and seed
Minhash<> automatic(128);                  // k, with automatically seeded hashes

std::set<std::uint32_t> elements{1, 2, 3};
sketch.restore(elements);                 // Ready: initializes all coordinates
elements.erase(1);
if (sketch.erase(1) == UpdateResult::NeedsRecovery) {
    sketch.restore(elements);             // Fault: repairs ONLY lost coordinates
}
```

Insertion updates the minima and returns `Ok`. Deletion checks **every** winning
identity and invalidates all coordinates won by the removed element before
returning `NeedsRecovery`. It does not stop at the first lost minimum. Signature
reads and incremental updates are then blocked until restoration succeeds.

`restore(range)` and `restore(first, last)` scan the current set once. Their
behavior depends on the sketch state:

- **NeedsRecovery:** recompute only the lost coordinates. Valid coordinates and
  their winning identities are preserved, and their hash functions are not
  evaluated during the scan. With `r` lost coordinates and `n` input elements,
  recovery performs `n*r` hash evaluations, with O(n*r + r) work.
- **Ready:** rebuild all coordinates, so the same method can initialize a new
  sketch or replace its represented set, as in the buffered API.

After a fault, the recovery range **must describe the set immediately after the
triggering deletion, without any further unreported updates**. The retained
minima depend on that precondition. To replace the set arbitrarily while faulty,
construct a new sketch using the same hash family and initialize it instead.
A failed input scan keeps the sketch in `NeedsRecovery`; retrying clears the
same repair targets and recomputes them from the complete current set.

An empty recovery succeeds when the set has become empty. `signature()` returns
`nullopt` in C++ or `None` in Python only for a known empty, Ready sketch; fault
state is reported separately. There are no reserved element or hash values.
Storage is O(k), including winners, signature values and recovery indices.

This is **not equivalent to `BufferedMinhash` with capacity 1**: the buffered
implementation rebuilds every buffer after a fault, whereas vanilla preserves
all surviving minima. The buffered recovery algorithm is unchanged.

Python exposes both input widths with the same methods and docstrings:

```python
from buffered_minhash import HashFamily, Minhash, UpdateResult

hashes = HashFamily(k=128, seed=42)
sketch = Minhash(hashes=hashes)             # or Minhash(k=128, seed=42)
elements = {1, 2, 3}
sketch.restore(x for x in elements)        # one-pass generators supported

elements.remove(1)
if sketch.erase(1) == UpdateResult.NeedsRecovery:
    sketch.restore(x for x in elements)    # only the invalidated coordinates
signature = sketch.signature()            # independent list, or None if empty
```

Use `Minhash64` with `HashFamily64` for 64-bit elements. Hash sharing,
`compatible_with`, `similarity`, `copy_signature`, state guards, and copy/move
semantics match the buffered API. Comparisons work in both directions between
vanilla and any general/legal array/tree variant with the same hash configuration.
There is no separate legal-only vanilla class in this implementation.

## Signatures and similarity

```cpp
if (auto signature = a.signature()) {    // optional<span<const hash_type>>
    for (auto value : *signature) {
        // Read the current hash values.
        (void)value;
    }
} // nullopt means a known empty set

auto saved = a.copy_signature();         // optional<vector<hash_type>>
double estimate = similarity(a, b);      // both sketches must be ready/compatible
```

A signature view is read-only and must not be used after any mutation, assignment,
move, or destruction of its sketch. Use `copy_signature` for an independent value.
Previously obtained views do not carry a live validity check.

`compatible_with` and `similarity` support different buffer kinds, update contracts and capacities,
with the same element/hash types. Compatibility requires the same ordered hash
configuration, not just the same `k`. Sharing the configuration enables a pointer
identity check; separately constructed configurations compare their full hash
parameters. Independently generated configurations need not be compatible.

`similarity` rejects incompatible configurations with `invalid_argument` and
recovery-required sketches with `logic_error`. For compatible, ready sketches,
the convention is similarity 1 for two empty sets and 0 for exactly one empty set.

## Custom hash functions

A hash is a copyable value type with `element_type`, `hash_type`, const call
operator, and equality comparing its full configuration. Evaluation must be
deterministic and not change configuration. For example:

```cpp
struct IdentityHash { // for testing, not a similarity-quality recommendation
    using element_type = std::uint32_t;
    using hash_type = std::uint64_t;
    hash_type operator()(element_type x) const noexcept { return x; }
    bool operator==(const IdentityHash&) const = default;
};

auto identity = std::make_shared<const HashFamily<IdentityHash>>(
    std::vector<IdentityHash>{IdentityHash{}});
BufferedMinhash<std::uint32_t, BufferKind::Array, IdentityHash> sketch(16, identity);
```

The family owns the supplied functions; no borrowed pointer array or ownership
flag is used. Hashes holding references to external mutable state do not satisfy
the semantic immutability contract. Seeded factories additionally require a hash
constructor accepting `uint64_t`.

The supplied tabulation implementation uses 4-bit chunks and 64-bit outputs by
default. Seeded construction uses `mt19937_64` outputs directly for reproducibility.
Matching the static signature for a chosen hash configuration is distinct from
the configuration's statistical quality or the paper's probabilistic performance
assumptions. This release tests implementation correctness; it does not claim a
new statistical validation or a proof of those assumptions for tabulation hashing.

## Layout and scope

- [include/buffered_minhash/](include/buffered_minhash/): header-only library.
- [tests/](tests/): deterministic, exhaustive, and seeded differential correctness
  checks; no performance experiments.
- [examples/](examples/): application-owned set and explicit recovery example.
- [legacy/](legacy/): original sketches and experimental code, preserved for reference.
- [TODO.md](TODO.md): agreed design, implementation status, and deferred work.

Performance/statistical experiments, smaller element types, and
unexpected-exception robustness are deferred.
