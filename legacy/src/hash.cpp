#ifndef HASH
#define HASH

#include <stdlib.h>
#include <cstdint>
#include <random>
typedef std::mt19937 RNG; // the Mersenne Twister with a popular choice of parameters

using namespace std;

template <class T>
class Hash
{
public:
    virtual T operator()(T x) = 0;
};

template <class T>
class TabulationHash : public Hash<T>
{
private:
    T **table;

public:
    TabulationHash();
};

// template <>
// class TabulationHash<uint32_t> : public Hash<uint32_t>
// {
// private:
//     uint32_t table[4][256];

// public:
//     TabulationHash()
//     {
//         for (int i = 0; i < 4; i++)
//             for (int j = 0; j < 256; j++)
//                 table[i][j] = rand();
//     }

//     uint32_t operator()(uint32_t x)
//     {
//         uint32_t res = 0;
//         for (int i = 0; i < 4; i++)
//             res ^= table[i][(char)(x >> 8 * i)];
//         return res;
//     }
// };

/**
 * Al momento ho implementato questa tabulation hashing a 32-bit con i seguenti parametri:
 * r = 4
 * t = 8
 * T[t][2^r]
 * per un totale di:
 * spazio: 32 * t * 2^r = 2^5 * 2^3 * 2^4 = 2^12 bits (2^7 = 128 interi)
 * tempo: 8 xor + 8 shift
 *
 * Questa implementazine occupa molto meno spazio rispetto alla precedente, la quale aveva i seguenti parametri
 * r = 8
 * t = 4
 * T[t][2^r]
 * per un totale di:
 * spazio: 32 * t * 2^r = 2^5 * 2^2 * 2^8 = 2^15 bits (2^10 = 1024 interi)
 * tempo: 4 xor + 4 shift
 */
template <>
class TabulationHash<uint32_t> : public Hash<uint32_t>
{
private:
    uint32_t table[8][16];
    uint32_t U;

public:
    TabulationHash(uint32_t U) : U(U)
    {
        std::random_device rd; // Obtain a random seed from the hardware
        RNG rng(rd());
        std::uniform_int_distribution<uint32_t> uint_dist(0, U); // by default range [0, MAX]
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 16; j++)
                table[i][j] = uint_dist(rng);
    }

    TabulationHash() {
        new (this) TabulationHash(UINT32_MAX);
    }

    ~TabulationHash() {}

    uint32_t operator()(uint32_t x)
    {
        uint32_t res = 0;
        for (int i = 0; i < 8; i++)
            res ^= table[i][(uint8_t)((x >> 4 * i) & 0b1111)]; // added the end with binary 00001111 to slip the 4 least significant bits
        return res;
    }
};

template <>
class TabulationHash<uint64_t> : public Hash<uint64_t>
{
private:
    uint64_t table[8][256];

public:
    TabulationHash()
    {
        std::random_device rd;                             // Obtain a random seed from the hardware
        RNG rng(rd());                                     // Create a generator instance with the seed
        std::uniform_int_distribution<uint32_t> uint_dist; // by default range [0, MAX]
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 256; j++)
                table[i][j] = ((uint64_t)uint_dist(rng) << 32) | uint_dist(rng);
    }

    uint64_t operator()(uint64_t x)
    {
        uint64_t res = 0;
        for (int i = 0; i < 8; i++)
            res ^= table[i][(uint8_t)(x >> 8 * i)];
        return res;
    }
};

template <class T>
class PairWiseHash : public Hash<T>
{
};

template <>
class PairWiseHash<uint32_t> : public Hash<uint32_t>
{
private:
    uint64_t a;
    uint64_t b;
    uint64_t M = 18446744073709550671ull;
    uint64_t n;

public:
    PairWiseHash()
    {
        new (this) PairWiseHash(UINT32_MAX);
    }

    PairWiseHash(uint32_t n) : n(n)
    {
        // RNG rng(1);
        std::random_device rd;                                         // Obtain a random seed from the hardware
        RNG rng(rd());                                                 // Create a generator instance with the seed
        std::uniform_int_distribution<uint32_t> uint_dist_n(0, n - 1); //  range [0, n]
        this->a = uint_dist_n(rng);
        if (!this->a)
            this->a++; // set a as non-zero value
        this->b = uint_dist_n(rng);
    }

    ~PairWiseHash() {}

    uint32_t operator()(uint32_t x)
    {
        uint64_t X = static_cast<uint64_t>(x);
        return static_cast<uint32_t>(((a * X + b) % M) % n);
    }
};

template <class T>
class IdentityHash : public Hash<T>
{
};

template <>
class IdentityHash<uint32_t> : public Hash<uint32_t>
{
public:
    uint32_t operator()(uint32_t x)
    {
        return x;
    }
};

#endif
