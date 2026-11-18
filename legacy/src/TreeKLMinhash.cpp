#ifndef TREEKLMINHASH_H
#define TREEKLMINHASH_H

#include <cstdint>
#include <iostream>
#include <stdlib.h>
#include <unordered_set>
#include <bits/stdc++.h>
#include "hash.cpp"
#include "Sketch.cpp"

using namespace std;

#define num uint32_t
#define NUM_MAX UINT32_MAX

class TreeKLMinhash : public Sketch
{
private:
    /**
     * U: the maximum size of the set (aka the universe size)
     */
    num U;

    /**
     * k: number of hash functions and buffers in the signature
     */
    int k;

    /**
     * l: number of hash values for each buffer
     */
    int l;

    /**
     * buffers: the signature of the set.
     * It represents a sequence of k buffers, each containing l elements.
     */
    multiset<num> **buffers;

    /**
     * delta: the maximum value in each buffer
     */
    num *delta;

    /**
     * hashes: array of k hash functions
     */
    Hash<num> **hashes;

    bool doFreeHashes = true;

    /**
     * signature: the minhash signature.
     */
    num *signature;

    /**
     * explicitSet: if true, the set is explicitly stored.
     * TODO: it will be deleted in the future
     */
    bool explicitSet;

    /**
     * elements: the set
     * TODO: it will be deleted in the future
     */
    std::unordered_set<num> elements;

public:
    TreeKLMinhash() : k(1), l(1), U(1) {}

    TreeKLMinhash(int k, int l, num U, bool explicitSet = true)
    {
        // PairWiseHash<num> *hashes = new PairWiseHash<num>[k];

        // PairWiseHash<num> **hashes = (PairWiseHash<num> **)malloc(k * sizeof(PairWiseHash<num> *));
        TabulationHash<num> **hashes = (TabulationHash<num> **)malloc(k * sizeof(TabulationHash<num> *));
        for (int i = 0; i < k; i++)
            // hashes[i] = new PairWiseHash<num>(U);
            hashes[i] = new TabulationHash<num>();

        new (this) TreeKLMinhash(k, l, U, (Hash<num> **)hashes, explicitSet, true);
    }

    /**
     * Constructor
     */
    TreeKLMinhash(int k, int l, num U, Hash<num> **hashes, bool explicitSet = true, bool doFreeHashes = false)
        : k(k), l(l), U(U), hashes(hashes), explicitSet(explicitSet), doFreeHashes(doFreeHashes)
    {
        // this->hashes = new std::pair<num, num>[k];
        this->buffers = (multiset<num> **)malloc(k * sizeof(multiset<num> *));
        this->delta = (num *)malloc(k * sizeof(num));

        this->signature = (num *)malloc(this->k * sizeof(num));

        for (int i = 0; i < k; i++)
        {
            // this->buffers[i] = multiset<num>();
            this->buffers[i] = new multiset<num>();
            this->delta[i] = NUM_MAX;

            for (int j = 0; j < l; j++)
                this->buffers[i]->insert(NUM_MAX);

            this->signature[i] = NUM_MAX;
        }
    }

    ~TreeKLMinhash()
    {
        for (int i = 0; i < k; i++)
            delete this->buffers[i];
        delete[] this->buffers;
        delete[] this->delta;
        delete[] this->signature;

        if (doFreeHashes)
        {
            for (int i = 0; i < k; i++)
                delete this->hashes[i];

            delete[] this->hashes;
        }
    }

    /**
     * Computes the hash of x using the i-th hash function
     */
    num hash(num x, int i)
    {
        return (*this->hashes[i])(x);
    }

    void insert(num x)
    {
        this->insert(x, true);
    }

    /**
     * Inserts x into the sketch.
     * If insertIntoSet is true (default) and isExplicitSet flag is true, x is also explicitaly stored.
     */
    void insert(num x, bool insertIntoSet)
    {
        if (this->explicitSet && insertIntoSet)
            this->elements.insert(x);

        for (int i = 0; i < this->k; i++)
        {
            num h = hash(x, i);
            // num h = x;
            if (h > this->delta[i])
                continue;

            auto current_max = this->buffers[i]->rbegin();
            this->buffers[i]->erase(next(current_max).base());
            this->buffers[i]->insert(h);

            this->signature[i] = *this->buffers[i]->begin();

            num max = *this->buffers[i]->rbegin();
            if (max < this->delta[i])
                this->delta[i] = max;
        }
    }

    /**
     * Removes x from the sketch.
     * If a fault occurs:
     * - the method returns trueTreeKLMinhash *A = new TreeKLMinhash(k, l, U, hashes, false);
     * - the buffer is reset
     * - if the explicitSet flag is true, all the elements are reinserted
     * The method returns false otherwise
     */
    bool remove(num x)
    {
        if (this->explicitSet)
            this->elements.erase(x);

        for (int i = 0; i < this->k; i++)
        {
            num h = hash(x, i);
            // num h = x;
            if (h > this->delta[i])
                continue;

            auto element = this->buffers[i]->find(h);
            if (element != this->buffers[i]->end())
            {
                this->buffers[i]->erase(element);
                this->buffers[i]->insert(NUM_MAX);

                if (*this->buffers[i]->begin() == NUM_MAX)
                {
                    this->resetBuffer();
                    if (this->explicitSet)
                        this->fault();

                    return true;
                }

                this->signature[i] = *this->buffers[i]->begin();
            }
        }

        return false;
    }

    /**
     * Reinserts all the elements in the sketch.
     */
    void fault()
    {
        auto itr = this->elements.begin();
        for (; itr != this->elements.end(); ++itr)
            this->insert(*itr, false);
    }

    /**
     * Returns the k-minhash signature
     */
    num *getSignature()
    {
        // for (int i = 0; i < this->k; i++)
        //     this->signature[i] = *this->buffers[i]->begin();
        return this->signature;
    }

    /**
     * Static method that given two sketches (TreeKLMinhash) A & B returns the estimation of their jaccard similarity.
     */
    static double similarity(TreeKLMinhash *A, TreeKLMinhash *B)
    {
        num *sigA = A->getSignature();
        num *sigB = B->getSignature();

        int k = A->k;
        double c = .0;
        for (int i = 0; i < k; i++)
            c += sigA[i] == sigB[i];
        return c / static_cast<double>(k);
    }

    /**
     * Resets the buffer to default values.
     */
    void resetBuffer()
    {
        for (int i = 0; i < this->k; i++)
        {
            this->delta[i] = NUM_MAX;
            this->buffers[i]->clear();
            for (int j = 0; j < this->l; j++)
                this->buffers[i]->insert(NUM_MAX);

            this->signature[i] = NUM_MAX;
        }
    }

    /**
     * Prints the i-th row of the signature matrix.
     */
    void printRow(int i)
    {
        num current_max = this->delta[i];

        string start;
        string end;

        if (current_max == NUM_MAX)
            cout << "[∞]\t";
        else
            cout << "[" << current_max << "]\t";

        for (auto itr = this->buffers[i]->begin(); itr != this->buffers[i]->end(); itr++)
        {
            if (*itr == NUM_MAX)
                cout << start << "∞" << end << " ";
            else
                cout << start << *itr << end << " ";
        }
        cout << endl;
    }

    /**
     * Prints the signature matrix.
     */
    void print()
    {
        for (int i = 0; i < this->k; i++)
            this->printRow(i);
    }
};

#endif
