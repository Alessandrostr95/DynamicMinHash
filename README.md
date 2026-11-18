# BufferedMinhash

A C++20, header-only implementation of the general **ell-buffered k-MinHash**
sketch. Both array and tree buffers support set updates, including redundant
insertions and deletion of absent elements. The application owns the underlying
set; the sketch stores bounded buffers and requests explicit recovery when needed.

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
  elements are harmless. Iterator/range value types must match `element_type`.
- Restore also accepts an empty set and may replace the represented set while the
  sketch is ready. Individual sketches require external synchronization; shared
  hash configurations must support concurrent const evaluation if used by threads.
- The initial implementation assumes hash evaluation, allocations, container
  operations, and range traversal succeed. There is no rollback or supported
  recovery contract for unexpected exceptions. API argument/state errors are
  checked separately; robust unexpected-exception handling is future work.

Array and tree buffers hold distinct `(hash, element)` pairs in lexicographic
order (the array's physical storage is unordered). Each threshold is an optional
pair: absence means infinity. Deletions retain thresholds even when a buffer is
no longer full. A buffered element's deletion can therefore cause recovery later,
without admitting previously discarded elements above the retained threshold.

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

`compatible_with` and `similarity` support different buffer kinds and capacities,
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

Performance/statistical experiments, a legal-stream specialization, smaller
element types, and unexpected-exception robustness are deferred.
