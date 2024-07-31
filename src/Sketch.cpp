#ifndef SKETCH_H
#define SKETCH_H

#include <cstdint>

class Sketch
{
public:
    void insert() {}
    bool remove() { return false; }

    uint32_t *getSignature()
    {
        return nullptr;
    };
};

#endif