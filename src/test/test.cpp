#include "../TreeKLMinhash.h"
#include "../DSS.cpp"
#include "../DSSProactive.cpp"
#include "../TreeBottomKL.h"
#include "../LSH.cpp"
#include "../BitArray.cpp"
#include <algorithm>
#include <chrono>
using namespace std::chrono;
using namespace std;

void permute(int *a, int n)
{
    random_shuffle(a, a + n);
}

/**
 * This function generates a random sample of N elements, over the universe [0, UINT32_MAX].
 * The sample is stored in a dynamic array.
 * @param N the size of the sample
 * @return the sample
 */
uint32_t *generate_random_sample(uint32_t N)
{
    unordered_set<uint32_t> values = unordered_set<uint32_t>(N);
    std::random_device rd;                               // Obtain a random seed from the hardware
    std::mt19937 rng(rd());                              // Create a generator instance with the seed
    std::uniform_int_distribution<uint32_t> uint_dist_n; //  range [0, n]

    uint32_t *sample = new uint32_t[N];
    int i = 0;

    while (i < N)
    {
        uint32_t x = uint_dist_n(rng);
        if (values.insert(x).second)
            sample[i++] = x;
    }

    return sample;
}

/**
 * This experiment evaluates the performance of the BufferKLMinhash sketch.
 * The sketch is created with k buffers of size l.
 * This expermient first inserts N elements in the sketch and then removes them, measuring the time.
 * @param k number of hash functions
 * @param l size of the buffers
 * @param N 2*N is the number of operations
 */
void singleSetImplicit(int k, int l, int N)
{
    // counter of faults
    int n_fault = 0;

    // create a new TreeKLMinhash sketch
    TreeKLMinhash *S = new TreeKLMinhash(k, l, UINT32_MAX, false);

    // generate a random sample
    uint32_t *sample = generate_random_sample(N);

    // start the timer
    auto start = high_resolution_clock::now();

    // insert all elements in the sketch
    for (int i = 0; i < N; i++)
        S->insert(sample[i]);

    // remove all elements from the sketch
    for (int i = 0; i < N; i++)
    {
        int doFault = S->remove(sample[i]);
        if (doFault)
        {
            n_fault++;

            // recovery query
            for (int j = i + 1; j < N; j++)
                S->insert(sample[j]);
        }
    }

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DMH, %d, %d, %u, %d, %f\n", k, l, 2 * N, n_fault, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the BufferKLMinhash sketch.
 * The sketch is created with k buffers of size l.
 * This experiment performs a sequence of insertions and removals, following a sliding window model.
 * @param k number of hash functions
 * @param l size of the buffers
 * @param U size of the universe
 * @param N 2*N is the number of operations
 * @param max_size size of the sliding window
 */
void slidingWindowMinHash(int k, int l, uint32_t U, int N, int max_size)
{
    TreeKLMinhash *S = new TreeKLMinhash(k, l, U, false);
    for (int j = 0; j < max_size; j++)
        S->insert(j);

    auto start = high_resolution_clock::now();
    int n_fault = 0;
    int first = 0;
    for (uint32_t i = 0; i < N; i++)
    {
        bool doFault = S->remove(first);
        if (doFault)
        {
            n_fault++;
            for (uint32_t j = first + 1; j < first + max_size; j++)
                S->insert(j);
        }
        S->insert(first + max_size + 1);
        first++;
    }

    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    float t = (float)duration.count() / 1000000.0;

    printf("%d, %d, %u, %d, %d, %f\n", k, l, 2 * N, max_size, n_fault, t);
    delete S;
}

/**
 * This experiment evaluates the performance of the DSS sketch.
 * The sketch first inserts N elements and then removes them, measuring the time.
 * @param c (equivalent to c^2 in the original paper)
 * @param N 2*N is the number of operations
 */
void testDSS(int c, int N)
{   
    // create a new DSS sketch
    DSS *S = new DSS(c);

    // generate a random sample
    uint32_t *sample = generate_random_sample(N);

    // start the timer
    auto start = high_resolution_clock::now();

    // insert all elements in the sketch
    for (int i = 0; i < N; i++)
        S->insert(sample[i]);

    // remove all elements from the sketch
    for (int i = 0; i < N; i++)
        S->remove(sample[i]);

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DSS, %d, %d, %u, %f\n", c, S->k, 2 * N, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the DSSProactive sketch.
 * The sketch first inserts N elements and then removes them, measuring the time.
 * @param c (equivalent to c^2 in the original paper)
 * @param N 2*N is the number of operations
 * @param n_hashes number of hash functions
 */
void testDSSProactive(int c, int N, int n_hashes)
{
    // create a new DSSProactive sketch
    DSSProactive *S = new DSSProactive(c, n_hashes);

    // generate a random sample
    uint32_t *sample = generate_random_sample(N);

    // start the timer
    auto start = high_resolution_clock::now();

    // insert all elements in the sketch
    for (int i = 0; i < N; i++)
        S->insert(sample[i]);

    // remove all elements from the sketch
    for (int i = 0; i < N; i++)
        S->remove(sample[i]);

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DSSp, %d, %d, %u, %f\n", c, S->k, 2 * N, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the DSS sketch, after a sequence of queries.
 * The sketch is initialized with `size` elements.
 * Then the time needed to execute `n_query` queries is measured.
 * @param c (equivalent to c^2 in the original paper)
 * @param size the size of the initial sample
 * @param n_query the number of queries
 * @param n_hashes number of hash functions
 */
void testDSSQuery(int c, int size, int n_query, int n_hashes)
{
    // create a new DSS sketch
    DSS *S = new DSS(c, n_hashes);

    // generate a random sample
    uint32_t *sample = generate_random_sample(size);

    // insert all elements in the sketch
    for (int i = 0; i < size; i++)
        S->insert(sample[i]);

    // start the timer
    auto start = high_resolution_clock::now();

    // execute n_query queries
    for (int i = 0; i < n_query; i++)
        S->getSignature();

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DSS, %d, %d, %u, %u, %d, %f\n", c, S->k, size, n_query, n_hashes, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the DSSProactive sketch, after a sequence of queries.
 * The sketch is initialized with `size` elements.
 * Then the time needed to execute `n_query` queries is measured.
 */
void testDSSProactiveQuery(int c, int size, int n_query, int n_hashes)
{
    // create a new DSSProactive sketch
    DSSProactive *S = new DSSProactive(c, n_hashes);

    // generate a random sample
    uint32_t *sample = generate_random_sample(size);

    // insert all elements in the sketch
    for (int i = 0; i < size; i++)
        S->insert(sample[i]);

    // start the timer
    auto start = high_resolution_clock::now();

    // execute n_query queries
    for (int i = 0; i < n_query; i++)
        S->getSignature();

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DSSp, %d, %d, %u, %u, %d, %f\n", c, S->k, size, n_query, n_hashes, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the BufferKLMinhash sketch, after a sequence of queries.
 * The sketch is initialized with `size` elements.
 * Then the time needed to execute `n_query` queries is measured.
 * @param l size of the buffers
 * @param size the size of the initial sample
 * @param n_query the number of queries
 * @param n_hashes number of hash functions
 */
void testKLMinhashQuery(int l, int size, int n_query, int n_hashes)
{   
    // create a new TreeKLMinhash sketch
    TreeKLMinhash *S = new TreeKLMinhash(n_hashes, l, UINT32_MAX, false);

    // generate a random sample
    uint32_t *sample = generate_random_sample(size);

    // insert all elements in the sketch
    for (int i = 0; i < size; i++)
        S->insert(sample[i]);

    // start the timer
    auto start = high_resolution_clock::now();
    for (int i = 0; i < n_query; i++)
        S->getSignature();

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DMH, %d, %d, %u, %u, %f\n", n_hashes, l, size, n_query, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the DSS sketch, after a sequence of updates with interleaved queries.
 * More precisely, are performed `N` insertions and `N` deletions, but a fraction `p` of operations are replaced by queries.
 * @param c (equivalent to c^2 in the original paper)
 * @param N 2*N is the number of operations
 * @param n_hashes number of hash functions
 * @param p fraction of queries
 * @param start the sketch could be initialized with a sample of `start` elements
 */
void testDSSUpdatesAndQuery(int c, int N, int n_hashes, float p, int start = 1)
{   
    // create a new DSS sketch
    DSS *S = new DSS(c, n_hashes);

    // generate a random sample
    uint32_t *sample = generate_random_sample(N + start);

    // compute the number of queries
    int n_query = (int)(1 / p);

    // remove the queries from the total number of operations
    N = N - (int)p * N;

    // insert the first `start` elements in the sketch
    for (int i = 0; i < start; i++)
        S->insert(sample[i]);

    // start the timer
    auto start_time = high_resolution_clock::now();

    // insert the elements in the sketch
    for (int i = start; i < N + start; i++)
    {
        if (i % n_query == 0)
            S->getSignature();

        S->insert(sample[i]);
    }

    // remove the elements from the sketch
    for (int i = start; i < N + start; i++)
    {
        if (i % n_query == 0)
            S->getSignature();

        S->remove(sample[i]);
    }

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start_time);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DSS, %d, %d, %u, %d, 0, %.2f, %f\n", c, S->k, 2 * N, n_hashes, p, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the DSSProactive sketch, after a sequence of updates with interleaved queries.
 * More precisely, are performed `N` insertions and `N` deletions, but a fraction `p` of operations are replaced by queries.
 * @param c (equivalent to c^2 in the original paper)
 * @param N 2*N is the number of operations
 * @param n_hashes number of hash functions
 * @param p fraction of queries
 * @param start the sketch could be initialized with a sample of `start` elements
 */
void testDSSProactiveUpdatesAndQuery(int c, int N, int n_hashes, float p, int start = 1)
{
    // create a new DSSProactive sketch
    DSSProactive *S = new DSSProactive(c, n_hashes);

    // generate a random sample
    uint32_t *sample = generate_random_sample(N + start);

    // compute the number of queries
    int n_query = (int)(1 / p);

    // remove the queries from the total number of operations
    N = N - (int)p * N;

    // insert the first `start` elements in the sketch
    for (int i = 0; i < start; i++)
        S->insert(sample[i]);

    // start the timer
    auto start_time = high_resolution_clock::now();

    // insert the elements in the sketch
    for (int i = start; i < N + start; i++)
    {
        if (i % n_query == 0)
            S->getSignature();

        S->insert(sample[i]);
    }

    // remove the elements from the sketch
    for (int i = start; i < N + start; i++)
    {
        if (i % n_query == 0)
            S->getSignature();

        S->remove(sample[i]);
    }

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start_time);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DSSp, %d, %d, %u, %d, 0, %.2f, %f\n", c, S->k, 2 * N, n_hashes, p, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the performance of the TreeKLMinhash sketch, after a sequence of updates with interleaved queries.
 * More precisely, are performed `N` insertions and `N` deletions, but a fraction `p` of operations are replaced by queries.
 * @param n_hashes number of hash functions
 * @param l size of the buffers
 * @param N 2*N is the number of operations
 * @param p fraction of queries
 * @param start the sketch could be initialized with a sample of `start` elements
 */
void testKLMinhashUpdatesAndQuery(int n_hashes, int l, int N, float p, int start = 1)
{   
    // create a new TreeKLMinhash sketch
    TreeKLMinhash *S = new TreeKLMinhash(n_hashes, l, UINT32_MAX, false);

    // generate a random sample
    uint32_t *sample = generate_random_sample(N + start);

    // compute the number of queries
    int n_query = (int)(1 / p);

    // remove the queries from the total number of operations
    N = N - (int)p * N;

    // insert the first `start` elements in the sketch
    for (int i = 0; i < start; i++)
        S->insert(sample[i]);

    // initialize the counter of faults
    int n_fault = 0;

    // start the timer
    auto start_time = high_resolution_clock::now();

    // insert the elements in the sketch
    for (int i = start; i < N + start; i++)
    {
        if (i % n_query == 0)
            S->getSignature();

        S->insert(sample[i]);
    }

    // remove the elements from the sketch
    for (int i = start; i < N + start; i++)
    {
        if (i % n_query == 0)
            S->getSignature();

        int doFault = S->remove(sample[i]);
        if (doFault)
        {
            n_fault++;

            // recovery query
            for (int j = i + 1; j < N; j++)
                S->insert(sample[j]);
        }
    }

    // stop the timer
    auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start_time);
    float t = (float)duration.count() / 1000000.0;

    // print the results
    printf("DMH, %d, %d, %u, %d, %d, %.2f, %f\n", n_hashes, l, 2 * N, n_hashes, n_fault, p, t);

    delete S;
    delete[] sample;
}

/**
 * This experiment evaluates the quality of the Similarity Estimation (SE) of the TreeKLMinhash sketch.
 * The sketch is created with k buffers of size l.
 * Two sets A and B are created according the synthetic dataset of E. Cohen, M. Datar, S. Fujiwara, A. Gionis, P. Indyk, R. Motwani, J.D. Ullman, and C. Yang. Finding interesting associations without support pruning, with parameter p1 and p2.
 * The Jaccard similarity is computed between A and B.
 * The sketch is then used to estimate the similarity between A and B.
 * Then the squared error between the estimation and the real similarity is computed.
 * @param k number of hash functions
 * @param l size of the buffers
 * @param U size of the universe
 * @param p1 probability of 1 in the set A
 * @param p2 probability of 1 in the set B
 * @param hashes array of (`k`) hash functions
 * @return the squared error of the jacard similarity estimation
 */
double SE_DMH(int k, int l, uint32_t U, double p1, double p2, Hash<uint32_t> **hashes)
{
    // create a new TreeKLMinhash sketch for set A
    TreeKLMinhash *SA = new TreeKLMinhash(k, l, UINT32_MAX, hashes, false);

    // create a new TreeKLMinhash sketch for set B
    TreeKLMinhash *SB = new TreeKLMinhash(k, l, UINT32_MAX, hashes, false);
    
    // create the sets A and B
    __type *A = create(U, 0.05);
    __type *B = perturbate(A, U, p1, p2);

    // insert the elements in the sketches
    for (int i = 0; i < U; i++)
    {
        if (get(A, i) == 1)
            SA->insert(i);
        if (get(B, i) == 1)
            SB->insert(i);
    }

    // estimate the similarity between A and B
    double estimation = TreeKLMinhash::similarity(SA, SB);

    // compute the real Jaccard similarity between A and B
    double js = jaccard_sim(A, B, U);

    // return the squared error
    double err = estimation - js;

    delete SA;
    delete SB;
    delete[] A;
    delete[] B;

    return err * err;
}


/**
 * This experiment evaluates the quality of the Similarity Estimation (SE) of the DSS sketch.
 * The sketch is created with k buffers of size l.
 * Two sets A and B are created according the synthetic dataset of E. Cohen, M. Datar, S. Fujiwara, A. Gionis, P. Indyk, R. Motwani, J.D. Ullman, and C. Yang. Finding interesting associations without support pruning, with parameter p1 and p2.
 * The Jaccard similarity is computed between A and B.
 * The sketch is then used to estimate the similarity between A and B.
 * Then the squared error between the estimation and the real similarity is computed.
 * @param c (equivalent to c^2 in the original paper)
 * @param k number of hash functions
 * @param U size of the universe
 * @param p1 probability of 1 in the set A
 * @param p2 probability of 1 in the set B
 * @param hashes array of (`k`) hash functions
 * @param h1 the first hash function for the DSS sketch
 * @param h2 the second hash function for the DSS sketch
 * @return the squared error of the jacard similarity estimation
 */
double SE_DSS(int c, int k, uint32_t U, double p1, double p2, Hash<uint32_t> **hashes, Hash<uint32_t> *h1, Hash<uint32_t> *h2)
{
    // create a new DSS sketch for set A
    DSS *SA = new DSS(c, h1, h2, hashes, k, false);

    // create a new DSS sketch for set B
    DSS *SB = new DSS(c, h1, h2, hashes, k, false);

    // create the sets A and B
    __type *A = create(U, 0.05);
    __type *B = perturbate(A, U, p1, p2);

    // insert the elements in the sketches
    for (int i = 0; i < U; i++)
    {
        if (get(A, i) == 1)
            SA->insert(i);
        if (get(B, i) == 1)
            SB->insert(i);
    }

    // estimate the similarity between A and B
    double alpha = .1;
    double r = .25;

    double estimation = DSS::similarity(SA, SB, alpha, r);

    // compute the real Jaccard similarity between A and B
    double js = jaccard_sim(A, B, U);

    // return the squared error
    double err = estimation - js;

    delete SA;
    delete SB;
    delete[] A;
    delete[] B;

    return err * err;
}