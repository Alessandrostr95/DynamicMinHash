# READ ME
This repository contains the implementation of the $\ell$-buffered $k$-MinHash data structure, as described in the paper ["Maintaining $k$-MinHash Signatures Over Fully-Dynamic Data Streams with Recovery"](https://arxiv.org/abs/2407.21614).
The structure of the repository is as follows:
- `src/`: contains all the source code of the project.
- `src/TreeKLMinHash.h`: contains the implementation of the $\ell$-buffered $k$-MinHash data structure.
- `src/DSS.cpp`: contains the implementation of the DSS sketch of ["Similarity Search for Dynamic Data Streams"](https://ieeexplore.ieee.org/abstract/document/8713878). 
- `src/DSSProactive.cpp`: contains the implementation of the proactive DSS sketch of ["Similarity Search for Dynamic Data Streams"](https://ieeexplore.ieee.org/abstract/document/8713878).
- `src/Sketch.cpp`: contains the interface of the sketches.
- `src/hash.cpp`: contains the implementation of the hash functions.
- `src/Utils.cpp`: contains the implementation of the utility functions.
- `src/BitArray.cpp`: implementation of set operations on bit arrays.
- `src/test/`: contains the test files.
- `experiments.cpp`: experiments to evaluate the performance of the $\ell$-buffered $k$-MinHash, DSS and proactive DSS sketches.
- `dataset/dataset.py`: script to generate the dataset used in the experiments.

# How to compile
To compile the project, run the following command:
```bash
g++ experiments.cpp -O3 -mavx -fopenmp
```
