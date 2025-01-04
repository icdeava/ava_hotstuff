//
// Created by tejas on 11/14/23.
//

#ifndef HS_HELPER_H
#define HS_HELPER_H

#endif //HS_HELPER_H


#include <cstdlib>
#include <iostream>
#include <stdint.h>

#include "hotstuff/util.h"
//#include "/usr/include/jemalloc/jemalloc.h"

class myrand
{
public:
    void init(uint64_t seed);
    uint64_t next();

private:
    uint64_t seed;
};


void myrand::init(uint64_t seed)
{
    this->seed = seed;
}

uint64_t myrand::next()
{
    seed = (seed * 1103515247UL + 12345UL) % (1UL << 63);
    return (seed / 65537) % RAND_MAX;
}

double zeta(uint64_t n, double theta)
{
    double sum = 0;
    for (uint64_t i = 1; i <= n; i++)
        sum += pow(1.0 / i, theta);
    return sum;
}

//
//class mem_alloc
//{
//public:
//    void *alloc(uint64_t size);
//    void *align_alloc(uint64_t size);
//    void *realloc(void *ptr, uint64_t size);
//    void free(void *block, uint64_t size);
//};
//
//
//
//
//
//void mem_alloc::free(void *ptr, uint64_t size)
//{
//
//    HOTSTUFF_LOG_INFO("free %ld 0x%lx\n", size, (uint64_t)ptr);
//#ifdef N_MALLOC
//    std::free(ptr);
//#else
//    je_free(ptr);
//#endif
//}
//
//void *mem_alloc::alloc(uint64_t size)
//{
//    void *ptr;
//
//#ifdef N_MALLOC
//    ptr = malloc(size);
//#else
//    ptr = je_malloc(size);
//#endif
//    HOTSTUFF_LOG_INFO("alloc %ld 0x%lx\n", size, (uint64_t)ptr);
//    assert(ptr != NULL);
//    return ptr;
//}
//
//void *mem_alloc::align_alloc(uint64_t size)
//{
//    uint64_t aligned_size = size + CL_SIZE - (size % CL_SIZE);
//    return alloc(aligned_size);
//}
//
//void *mem_alloc::realloc(void *ptr, uint64_t size)
//{
//#ifdef N_MALLOC
//    void *_ptr = std::realloc(ptr, size);
//#else
//    void *_ptr = je_realloc(ptr, size);
//#endif
//    HOTSTUFF_LOG_INFO("realloc %ld 0x%lx\n", size, (uint64_t)_ptr);
//    return _ptr;
//}

