#ifndef BITARRAY_H
#define BITARRAY_H

#include <cstdint>
#include <stdlib.h>
#include <cstring>
#include <random>
#include <nmmintrin.h>
#include <iostream>

#define __type uint64_t

using namespace std;

__type *init(uint32_t);

__type *init(uint32_t U)
{
    uint32_t size = U / sizeof(__type) + 1;
    return (__type *)calloc(size, sizeof(__type));
}

void flip(__type *array, uint32_t index)
{
    uint32_t j = index / sizeof(__type);
    uint32_t k = index % sizeof(__type);

    array[j] ^= (uint32_t)(1 << k);
}

bool get(__type *array, uint32_t index)
{
    uint32_t j = index / sizeof(__type);
    uint32_t k = index % sizeof(__type);

    return (array[j] & (uint32_t)(1 << k)) > 0;
}

__type *create(uint32_t U, float p)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    __type *array = init(U);
    for (uint32_t i = 0; i < U; i++)
        if (dis(gen) <= p)
            flip(array, i);

    return array;
}

uint32_t count_one(__type *array, uint32_t U)
{
    uint32_t size = U / sizeof(__type) + 1;
    long long unsigned int count = 0;
    for (int i = 0; i < size; i++)
        count += _mm_popcnt_u64(array[i]);

    return (uint32_t)count;
}

__type *perturbate(__type *array, uint32_t U, float p1, float p2)
{
    uint32_t size = U / sizeof(__type) + 1;
    __type *brray = new __type[size];
    memcpy(brray, array, size * sizeof(__type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    uint32_t c_del = 0;
    uint32_t c_ins = 0;

    for (int i = 0; i < U; i++)
    {
        float x = dis(gen);
        bool bit = get(array, i);
        if ((bit && (x <= p1)) || (!bit && (x <= p2)))
            flip(brray, i);
    }
    return brray;
}

uint32_t size_intersection(__type *A, __type *B, uint32_t U)
{
    uint32_t size = U / sizeof(__type) + 1;

    uint32_t result = 0;
    for (int i = 0; i < size; i++)
        result += _mm_popcnt_u64(A[i] & B[i]);
    return result;
}

uint32_t size_union(__type *A, __type *B, uint32_t U)
{
    uint32_t size = U / sizeof(__type) + 1;

    uint32_t result = 0;
    for (int i = 0; i < size; i++)
        result += _mm_popcnt_u64(A[i] | B[i]);
    return result;
}

double jaccard_sim(__type *A, __type *B, uint32_t U)
{
    return static_cast<double>(size_intersection(A, B, U)) / static_cast<double>(size_union(A, B, U));
}

#endif