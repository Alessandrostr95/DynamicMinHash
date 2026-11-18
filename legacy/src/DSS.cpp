#ifndef DSS_H
#define DSS_H

#include <cstdint>
#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <unordered_set>
#include <bits/stdc++.h>
#include "hash.cpp"
#include "Sketch.cpp"

using namespace std;

#define P32 4294966297
#define P64 18446744073709550671

/**
 * Implementation of Alg1 of ["Similarity Search for Dynamic Data Streams"](https://ieeexplore.ieee.org/abstract/document/8713878)
 */
class DSS : public Sketch
{

public:
    /**
     * U: the size of the universe of elements
     */
    uint32_t U;

    /**
     * size: the current size of the set
     */
    uint32_t size;

    /**
     * c: the number of columns in the signature matrix (in the paper it is denoted as c^2)
     */
    uint32_t c;

    /**
     * k: the number of rows in the signature matrix
     */
    uint32_t k;

    /**
     * h1, h2: the two hash functions used to compute the row index of the signature matrix
     *
     * h1 is a pairwise function |U| -> |U|. It is used to compute the row index of the signature matrix.
     * In order to compute the row index, we use the least significant bit of h1(x).
     *
     * h2 is a pairwise function |U| -> {0, 1, ..., c-1}. It is used to compute the column index of the signature matrix.
     */
    Hash<uint32_t> *h1, *h2;

    /**
     * T: the signature matrix
     */
    uint32_t **T;

    /**
     * t: the number of hash functions used to compute the minhash signature. It is commonly denoted as k in the literature.
     */
    int t;

    /**
     * hashes: the t hash functions used to compute the t-minhash signature
     */
    Hash<uint32_t> **hashes;

    bool doFreeHashes = true;

    /**
     * signature: the t-minhash signature of the sketch
     */
    uint32_t *signature;

    /**
     * Constructor
     * it randomly generates the two hash functions h1 and h2 and the t hash functions used to compute the t-minhash signature
     */
    DSS(uint32_t c, int t = 1)
    {
        PairWiseHash<uint32_t> *h1 = new PairWiseHash<uint32_t>();
        PairWiseHash<uint32_t> *h2 = new PairWiseHash<uint32_t>(c);

        PairWiseHash<uint32_t> **hashes = (PairWiseHash<uint32_t> **)malloc(t * sizeof(PairWiseHash<uint32_t> *));
        for (int i = 0; i < t; i++)
            hashes[i] = new PairWiseHash<uint32_t>();

        new (this) DSS(c, h1, h2, (Hash<uint32_t> **)hashes, t, true);
    }

    /**
     * Constructor
     */
    DSS(uint32_t c, Hash<uint32_t> *h1, Hash<uint32_t> *h2, Hash<uint32_t> **hashes, int t, bool doFreeHashes = false)
        : size(0), U(UINT32_MAX), c(c), h1(h1), h2(h2), hashes(hashes), t(t), doFreeHashes(doFreeHashes)
    {
        k = (int)floor(log2(U)) + 1;
        this->T = (uint32_t **)malloc(k * sizeof(uint32_t *));

        for (int i = 0; i < k; i++)
        {
            this->T[i] = (uint32_t *)malloc(c * sizeof(uint32_t));

            for (int j = 0; j < c; j++)
                this->T[i][j] = 0;
        }

        this->signature = (uint32_t *)malloc(t * sizeof(uint32_t));
    }

    ~DSS()
    {
        for (int i = 0; i < this->k; i++)
            delete[] this->T[i];
        delete[] this->T;
        delete[] this->signature;

        if (this->doFreeHashes)
        {
            for (int i = 0; i < this->t; i++)
                delete this->hashes[i];
            delete[] this->hashes;
            delete this->h1;
            delete this->h2;
        }
    }

    /**
     * Least significant bit
     * Returns the position of the least significant bit set to 1
     */
    int lsb(uint32_t x)
    {
        if (x == 0)
            return (int)floor(log2(U));
        return static_cast<int>(log2(x & (~(x - 1))));
    }

    /**
     * Insert an element in the sketch
     */
    void insert(uint32_t x)
    {
        this->update(x, 1);
    }

    /**
     * Remove an element from the sketch
     */
    bool remove(uint32_t x)
    {
        this->update(x, -1);
        return false;
    }

    /**
     * Update the sketch.
     * This method implements the insertion and deletion of an element in the sketch.
     * If op is 1, the element is inserted, otherwise it is removed.
     */
    void update(uint32_t x, int op)
    {
        int i = lsb((*this->h1)(x));
        int j = (*this->h2)(x);
        this->T[i][j] += op;
        this->T[0][j] += op;
        this->size += op;
    }

    /**
     * Returns the signature of the sketch, as the t-minhash signature of
     * the row corresponding to the index log2(size)
     */
    uint32_t *getSignature(double alpha = 1.0, double r = 1.0)
    {
        int row = static_cast<uint>(log2(alpha * r * this->size));
        return minHash(max(0, (int)row));
    }

    /**
     * Returns the minhash signature wrt the t-th hash function of the row-th row
     */
    uint32_t minHash(int t, int row)
    {
        uint32_t minh = UINT32_MAX;
        for (int j = 0; j < this->c; j++)
        {
            if (this->T[row][j] != 0)
                minh = min(minh, (*this->hashes[t])(j + row * this->c));
        }
        return minh;
    }

    /**
     * Returns the t-minhash signature of the row-th row
     */
    uint32_t *minHash(int row)
    {
        for (int i = 0; i < this->t; i++)
            signature[i] = minHash(i, row);

        return signature;
    }

    int mem()
    {
        return this->c * this->k + this->t;
    }

    /**
     * Returns the Jaccard similarity estimation between two sketches A and B, given the parameters alpha and r.
     */
    static float similarity(DSS *A, DSS *B, float alpha, float r)
    {
        // the size of the sketches
        uint32_t sA = A->size;
        uint32_t sB = B->size;

        // first computes the ranges (sxA, dxA) and (sxB, dxB)

        int sxA = static_cast<int>(log2(alpha * r * sA));
        int sxB = static_cast<int>(log2(alpha * r * sB));

        int dxA = static_cast<int>(log2(alpha * sA));
        int dxB = static_cast<int>(log2(alpha * sB));

        // printf("|A| = %d, range=(%d, %d)\n", sA, sxA, dxA);
        // printf("|B| = %d, range=(%d, %d)\n", sB, sxB, dxB);

        // check if the ranges do not intersect
        if (dxA < sxB || sxA > dxB)
        {
            // if they do not intersect, it means that the sketches very different sizes
            // so we can return a bad estimation
            return min(sA, sB) / max(sA, sB);
        }
        else
        {
            // otherwise, we can compute the similarity estimation using the t-minhash signatures of the sketches
            int row = min(dxA, dxB);
            uint32_t *sigA = A->minHash(row);
            uint32_t *sigB = B->minHash(row);

            float k = .0;
            for (int i = 0; i < A->t; i++)
                k += sigA[i] == sigB[i];
            return k / A->t;
        }
    }
};

#endif