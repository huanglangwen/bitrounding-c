#ifndef BITROUNDING_BITINFO_H
#define BITROUNDING_BITINFO_H

#include <stddef.h>
#include <stdint.h>

#define NBITS 32

typedef struct {
    double M[NBITS];
} MutualInformation;

typedef struct {
    int C[NBITS][2][2];
} BitpairCounters;

void signed_exponent(float *A, size_t n);
MutualInformation bitinformation(float *A, size_t n);
int get_keepbits(const MutualInformation *bitInfo, double inflevel);

#endif