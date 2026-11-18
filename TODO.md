# Library implementation tasks

The initial C++20 `BufferedMinhash` library is implemented in
[include/buffered_minhash/](include/buffered_minhash/), with usage and build
instructions in [README.md](README.md). Checked items describe this implementation;
unchecked items are follow-up work. Historical implementations are preserved in
[legacy/](legacy/).

## Current scope and priorities

The general library and legal-stream specialization are implemented. Add new correctness tests alongside the
implementation; performance benchmarks and statistical experiments belong to a
later phase and are not prerequisites for implementation. The paper supplies the
algorithmic proof; tests check that the code implements that algorithm correctly.

For this initial phase, assume buffer/container operations, hash evaluation, and
recovery input traversal complete successfully. The only operational failure
handled by the sketch is algorithmic buffer exhaustion (`NeedsRecovery`). Keep
the agreed API validation and invalid-state access checks; do not add unexpected
exception recovery or rollback machinery yet.

## Agreed: external set ownership and explicit recovery

- [x] Remove `explicitSet` and the internally maintained element set from the
  buffered MinHash implementations. The application owns the authoritative set.
- [x] Expose an update result such as `UpdateResult::Ok` or
  `UpdateResult::NeedsRecovery`, and retain the recovery state in the sketch.
- [x] When a deletion exhausts a buffer, enter `NeedsRecovery`. Reject signature
  queries and further incremental updates until recovery succeeds.
- [x] Provide `restore(range)` or an iterator-based equivalent. Rebuild all
  buffers and thresholds using the existing hash functions, without retaining
  or copying the full input set. A successful empty-set restore is valid too.
- [x] Document that the application updates its authoritative set before notifying
  the sketch. Recovery input must describe the current set after the deletion
  that caused the fault; the scan must be coherent with the update stream.
- [x] Return to the valid state after reconstruction completes. Exception-safe
  recovery and behavior after unexpected reconstruction failure are deferred.
- [x] Test fault signaling, rejection of operations while recovery is required,
  restoration from empty and nonempty sets, and preservation of hash functions.

Possible later convenience layer: a wrapper accepting a recovery callback that
obtains the current elements and invokes `restore`. Keep this separate from the
core sketch; the core does not retain a pointer to an application container.

## Agreed: implement general update semantics first

- [x] Implement the general variant first, supporting redundant insertions and
  deletions with set semantics. Store element identities alongside hash values
  and use consistent ordering for buffer entries and thresholds.
- [x] Verify correctness during implementation.
- [x] Implement the legal-stream specialization now, as subsequently requested,
  with array and tree buffers and the same public API and recovery protocol.
  `LegalBufferedMinhash` selects a compile-time storage/semantics policy in the
  shared implementation; the default `BufferedMinhash` remains general.
- [x] Store only hashes and retain collision multiplicities for legal streams.
  Skip duplicate search for arrays; use a multiset and erase one occurrence for
  trees. Finite legal thresholds are hashes, with nullopt still meaning infinity.
- [x] Document unchecked legal-stream preconditions: insert absent elements,
  erase present elements, and restore from distinct elements. Reinsertion after
  deletion is allowed. Violations may silently produce incorrect signatures.
- [x] Share hash families and support comparisons across general/legal and
  array/tree variants. Cover both element widths and both C++ hash output widths.
- [x] Test forced collisions, multiplicities, retained thresholds, conservative
  early recovery, empty/maximal values, one-pass restore, and copy/move behavior.
  Compare with recomputed signatures over exhaustive legal streams and seeded
  randomized streams; restore each variant independently when needed.
- [x] Expose legal array/tree classes for uint32_t and uint64_t in Python, with
  docstrings, shared hash families, all comparison combinations, and binding tests.
- [ ] In the experimental phase, compare general and legal variants on realistic
  workloads: time, actual memory, fault frequency and total recovery cost. Use
  these results to decide whether to maintain the specialization long-term.

## Agreed: public interface and shared hash configuration

- [x] Build the initial library without the experimental `Sketch` base class.
  Reconsider a common runtime interface only if a concrete need emerges.
- [x] Provide an immutable configuration containing the ordered collection of
  concrete hash functions and their parameters. Share ownership across sketches,
  for example through `std::shared_ptr<const HashFamily>`, eliminating
  `Hash**` ownership flags such as `doFreeHashes`.
- [x] Support both explicit hash configuration and automatic creation. Expose
  the configuration so other sketches can reuse automatically generated hashes.
  Support reproducible explicit construction through a seed.
- [x] Derive `k` from a supplied configuration. Validate nonempty hash collections
  and positive buffer capacities. Eliminate constructors leaving invalid or
  uninitialized state. Keep universe parameters in hash configuration only where
  required by the hash implementation.
- [x] Expose signatures as read-only views using C++20 `std::span`, with documented
  update/lifetime rules and an explicit way to obtain an independent copy.
- [x] Check signature compatibility using the ordered hash configuration and
  signature length. Shared configuration identity provides a fast compatibility
  check; define equivalence for independently created configurations as well.
  Different buffer capacities are allowed. Independently auto-generated hash
  configurations are not automatically compatible.
- [x] Keep the public API focused on construction, insertion, deletion, state,
  signature access, restoration, and configuration inspection. Keep buffer reset
  and internal fault handling private.
- [x] Do not expose exact membership, exact cardinality, or insertion/deletion
  results claiming whether the authoritative set changed. Update results report
  sketch validity; the general sketch does not retain global membership.
- [x] Define empty-set signature behavior separately from `NeedsRecovery`.
- [x] Evaluate a shared implementation parameterized by array/tree buffer
  strategy, maintaining the same public contract across both representations.
  Implemented as the `BufferKind` template parameter of `BufferedMinhash`.
- [x] Allow concrete hash types without requiring virtual dispatch for every
  hash evaluation. Treat runtime polymorphism as a separate optional need.

## Agreed: element and hash types

- [x] Parameterize the sketch by element type, initially supporting `uint32_t`
  and `uint64_t`, with `uint32_t` as the default.
- [x] Keep `element_type` distinct from `hash_type`. Use 64-bit hash outputs by
  default, with the output type determined by the hash configuration.
- [x] Preserve exact element identity in general buffer entries, independently of
  hash collisions; legal variants retain only hashes and their multiplicities.
  Do not initially support arbitrary objects, floating-point types,
  or signed integers; these require separately defined contracts if added later.
- [x] Represent empty buffers and infinite thresholds explicitly, without
  reserving maximum integer values as sentinels. All representable element and
  hash values must remain usable.
- [ ] Future feature: add `uint8_t` and `uint16_t` element support to reduce memory
  where appropriate. Measure actual storage including entry padding and metadata;
  smaller element types must not implicitly force narrower hash outputs.

## Agreed: explicit empty buffers, infinite thresholds, and empty signatures

- [x] Represent array buffers with capacity and active size; only active entries
  participate in the algorithm. Use the container's size/empty state for tree
  buffers. Do not fill unused entries with sentinel hashes or add an optional
  wrapper to every buffer slot.
- [x] Represent a threshold as `std::optional<Entry>`, where `Entry` contains the
  hash and element identity for general streams, or only the hash for legal
  streams. `nullopt` means positive infinity. Compare general finite entries
  lexicographically by hash, then element. Defer metadata/padding
  measurements to the experimental phase.
- [x] Preserve thresholds across deletions. A buffer below capacity can retain a
  finite threshold; never infer infinity from its current size. Initialize or
  restore infinity for buffers with fewer than the capacity's number of entries,
  and a finite maximum for full buffers. During incremental insertion, update
  the threshold to the buffer maximum when the resulting buffer is full.
- [x] Treat construction and successful restoration from an empty set as valid
  states with empty buffers and infinite thresholds. A deletion that exhausts a
  buffer enters `NeedsRecovery`; it does not establish that the set is empty.
- [x] Expose an optional read-only signature view: a value for a valid nonempty
  set and `nullopt` for a known empty set. Access during `NeedsRecovery` must
  report an explicit error, distinct from `nullopt`. Avoid per-coordinate
  sentinels or optional values for the signature.
- [x] Test finite thresholds after deletion, admission against those thresholds,
  maximum-valued elements/hashes, and the distinction between known empty state
  and recovery-required state.

## Agreed: library engineering and correctness tests

- [x] Use C++20, CMake, automated tests, and AddressSanitizer/UndefinedBehaviorSanitizer
  checks. Use RAII and standard resource-owning containers.
- [x] Define copy and move semantics. Copying a sketch duplicates its mutable
  buffers/state while sharing only the immutable hash configuration.
- [x] Archive the old experimental tests separately from the new library tests,
  preserving historical material and relative includes by moving the original
  sources and experimental driver together into [legacy/](legacy/). The archived
  files are byte-for-byte identical to their originals.
- [x] Write a new test-only reference using an explicit set and signatures
  recomputed from scratch with the same hash functions. Compare both buffer
  implementations after deterministic and seeded randomized update sequences,
  restoring from the reference set when required. Cover duplicate updates,
  deletion of absent elements, forced collisions, maximum values, and capacity 1.

## Deferred: robustness and experiments

- [ ] Define unexpected-exception handling for hash functions, allocations,
  buffer operations, and recovery traversal, including partial updates and
  failed restore. Account for the authoritative set having already changed;
  preserving the old sketch alone does not guarantee synchronization.
- [ ] Evaluate statistical similarity quality and document which theoretical
  guarantees apply to the chosen hash families, separately from implementation
  correctness.
- [ ] Benchmark update time, actual memory, allocation counts, fault frequency,
  and recovery cost across parameter sizes and workloads. Include metadata and
  padding, array/tree comparisons, and possible contiguous array storage.
