#ifndef SKETCH_H
#define SKETCH_H

#define num uint32_t
#define NUM_MAX UINT32_MAX

#include <cstdint>

class Sketch
{
public:
    virtual void insert(num) {}
    virtual bool remove(num) { return false; }

    virtual uint32_t *getSignature()
    {
        return nullptr;
    };
};

#endif