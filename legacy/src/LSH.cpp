#ifndef LSH_H
#define LSH_H

#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iostream>

using namespace std;

struct hash_pair
{
    size_t operator()(const pair<int, int> &p) const
    {
        auto hash1 = hash<int>{}(p.first);
        auto hash2 = hash<int>{}(p.second);

        if (hash1 != hash2)
            return hash1 ^ hash2;

        return hash1;
    }
};

string toString(uint32_t *sequence, int size)
{
    ostringstream oss("");
    for (int i = 0; i < size - 1; i++)
    {
        oss << sequence[i];
        oss << ",";
    }

    oss << sequence[size - 1];
    return oss.str();
}

uint32_t XorIt(uint32_t *sequence, int size)
{
    uint32_t h = 0;
    for (int i = 0; i < size; i++)
        h ^= sequence[i];

    return h;
}

/**
 * Compute the Locality Sensitive Hashing of the signatures
 * @param signatures the signatures of the elements
 * @param n the number of elements
 * @param r the number of elements in each band
 * @param b the number of bands
 * @return the candidate pairs
 */
unordered_set<pair<int, int>, hash_pair> *computeLSH(uint32_t **signatures, int n, int r, int b)
{
    unordered_multimap<string, int> *H = new unordered_multimap<string, int>[b];
    // unordered_multimap<uint32_t, int> *H = new unordered_multimap<uint32_t, int>[b];

    for (int j = 0; j < b; j++)
        H[j].reserve(10 * n);

    unordered_set<string> *seenKeys = new unordered_set<string>[b];
    // unordered_set<uint32_t> *seenKeys = new unordered_set<uint32_t>[b];

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < b; j++)
        {
            string s = toString(signatures[i] + j * r, r);
            // uint32_t s = XorIt(signatures[i] + j * r, r);
            H[j].insert({s, i});
            seenKeys[j].insert(s);
        }
    }

    unordered_set<pair<int, int>, hash_pair> *candidatePairs = new unordered_set<pair<int, int>, hash_pair>();
    for (int j = 0; j < b; j++)
    {
        // cout << "Tabella hash: " << j << endl;
        // for (auto itr = H[j].begin(); itr != H[j].end(); itr++)
        // {
        //     cout << "[" << H[j].bucket(itr->first) << "] " << itr->first << " -> " << itr->second << endl;
        // }
        // cout << endl;

        std::clog << "\rBand: " << j << ", Candidates: " << candidatePairs->size() << "    " << std::flush;

        for (auto key = seenKeys[j].begin(); key != seenKeys[j].end(); key++)
        {
            int bucket_index = H[j].bucket(*key);
            if (H[j].bucket_size(bucket_index) > 1)
            {
                auto bucket = H[j].equal_range(*key);
                for (auto el1 = bucket.first; el1 != bucket.second; el1++)
                {
                    for (auto el2 = bucket.first; el2 != bucket.second; el2++)
                    {
                        int A = el1->second;
                        int B = el2->second;

                        if (A > B)
                            candidatePairs->insert({B, A});
                        else if (B > A)
                            candidatePairs->insert({A, B});
                    }
                }
            }
        }
    }
    cerr << endl;

    delete[] H;
    delete[] seenKeys;

    return candidatePairs;
}

void f()
{
    unordered_multimap<string, int> LSH;

    string k1 = "1,2,43,2";
    string k2 = "3,234,1,9";

    LSH.insert({k1, 1});
    LSH.insert({k1, 1});
    LSH.insert({k1, 2});
    LSH.insert({k2, 3});

    for (auto search = LSH.find(k1); search != LSH.end(); search++)
        std::cout << "Found " << search->first << ' ' << search->second << '\n';
}

#endif