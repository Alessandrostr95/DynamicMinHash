#ifndef ARRAYKLMINHASH_H
#define ARRAYKLMINHASH_H

#include <cstdint>
#include <iostream>
#include <stdlib.h>
#include <unordered_set>
#include <bits/stdc++.h>
#include "hash.cpp"
#include "Sketch.cpp"

using namespace std;

class ArrayKLMinhash : public Sketch
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
    num *buffers;

    int *buffers_size;

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
    ArrayKLMinhash() : k(1), l(1), U(1) {}

    ArrayKLMinhash(int k, int l, num U, bool explicitSet = true)
    {
        // PairWiseHash<num> *hashes = new PairWiseHash<num>[k];

        // PairWiseHash<num> **hashes = (PairWiseHash<num> **)malloc(k * sizeof(PairWiseHash<num> *));
        TabulationHash<num> **hashes = (TabulationHash<num> **)malloc(k * sizeof(TabulationHash<num> *));
        for (int i = 0; i < k; i++)
            // hashes[i] = new PairWiseHash<num>(U);
            hashes[i] = new TabulationHash<num>();

        new (this) ArrayKLMinhash(k, l, U, (Hash<num> **)hashes, explicitSet, true);
    }

    /**
     * Constructor
     */
    ArrayKLMinhash(int k, int l, num U, Hash<num> **hashes, bool explicitSet = true, bool doFreeHashes = false)
        : k(k), l(l), U(U), hashes(hashes), explicitSet(explicitSet), doFreeHashes(doFreeHashes)
    {
        // this->hashes = new std::pair<num, num>[k];
        this->buffers = (num *)malloc(k * l * sizeof(num));
        this->buffers_size = (int *)malloc(k * sizeof(int));
        this->delta = (num *)malloc(k * sizeof(num));

        this->signature = (num *)malloc(this->k * sizeof(num));

        for (int i = 0; i < k; i++)
        {
            // this->buffers[i] = new num[l];
            this->buffers_size[i] = 0;
            this->delta[i] = NUM_MAX;

            for (int j = 0; j < l; j++)
                this->buffers[i * l + j] = NUM_MAX;

            this->signature[i] = NUM_MAX;
        }
    }

    ~ArrayKLMinhash()
    {
        // for (int i = 0; i < k; i++)
        // delete this->buffers[i];
        delete[] this->buffers;
        delete[] this->delta;
        delete[] this->signature;
        delete[] this->buffers_size;

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

            if (this->buffers_size[i] < this->l)
            {
                this->buffers[i * this->l + this->buffers_size[i]] = h;
                this->buffers_size[i]++;
            }
            else
            {
                num current_max = this->delta[i];
                for (int j = 0; j < this->l; j++)
                {
                    if (this->buffers[i * this->l + j] == current_max)
                    {
                        this->buffers[i * this->l + j] = h;
                        break;
                    }
                }
            }

            if (this->buffers_size[i] == this->l)
            {
                num max = 0;
                for (int j = 0; j < this->l; j++)
                {
                    if (this->buffers[i * this->l + j] > max)
                    {
                        max = this->buffers[i * this->l + j];
                    }
                }
                this->delta[i] = max;
            }

            if (this->signature[i] > h)
                this->signature[i] = h;
        }
    }

    /**
     * Removes x from the sketch.
     * If a fault occurs:
     * - the method returns true ArrayKLMinhash *A = new ArrayKLMinhash(k, l, U, hashes, false);
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

            // find the element to remove
            int index_to_remove = -1;
            for (int j = 0; j < this->buffers_size[i]; j++)
            {
                if (this->buffers[i * this->l + j] == h)
                {
                    index_to_remove = j;
                    break;
                }
            }

            if (index_to_remove == -1)
            {
                continue;
            }

            // remove the element
            this->buffers[i * this->l + index_to_remove] = this->buffers[i * this->l + this->buffers_size[i] - 1];
            // this->buffers[i * this->l + this->buffers_size[i] - 1] = NUM_MAX;
            this->buffers_size[i]--;

            // if the buffer is empty reset it and eventually recover the sketch
            if (this->buffers_size[i] == 0)
            {
                this->resetBuffer();
                if (this->explicitSet)
                    this->fault();

                return true;
            }

            // recover the minimum value if deleted
            if (this->signature[i] == h)
            {
                num min = NUM_MAX;
                for (int j = 0; j < this->buffers_size[i]; j++)
                {
                    if (this->buffers[i * this->l + j] < min)
                    {
                        min = this->buffers[i * this->l + j];
                    }
                }
                this->signature[i] = min;
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
     * Static method that given two sketches (ArrayKLMinhash) A & B returns the estimation of their jaccard similarity.
     */
    static double similarity(ArrayKLMinhash *A, ArrayKLMinhash *B)
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
            this->buffers_size[i] = 0;
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

        for (int j = 0; j < this->buffers_size[i]; j++)
        {
            if (this->buffers[i * this->l + j] == NUM_MAX)
                cout << start << "∞" << end << " ";
            else
                cout << start << this->buffers[i * this->l + j] << end << " ";
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
