# Historical experimental implementation

This directory preserves the original C++ sources, including array/tree buffered
MinHash for legal streams, DSS/BSS and its proactive variant, hashing, bit arrays,
and the experimental driver. The former repository-root `src/` and
`experiments.cpp` were moved here together, so their relative includes are
preserved. The old tests remain in [src/test/](src/test/) as historical experiments;
they are not the new library's correctness suite.

This code is not compiled, installed, or linked by the new CMake library targets.
It retains the original ownership patterns and limitations and is not maintained
as the general-stream API. The active implementation is in
[../include/buffered_minhash/](../include/buffered_minhash/).

The historical compile command, adapted for relocation, is:

```sh
g++ legacy/experiments.cpp -O3 -mavx -fopenmp
```

Run from the repository root to preserve the driver's relative dataset paths.
Dataset tools remain in [../dataset/](../dataset/). This archive is retained for
reference, not as a validated benchmark or a supported build target.
