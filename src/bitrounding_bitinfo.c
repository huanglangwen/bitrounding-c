#include "bitrounding_bitinfo.h"
#include "bitrounding_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void set_zero_insignificant(double *H, size_t nelements, double confidence) {
    double Hfree = binom_free_entropy(nelements, confidence);
    for (int i = 0; i < NBITS; ++i) {
        if (H[i] <= Hfree) H[i] = 0.0;
    }
}

static void bitpair_count_kernel(uint32_t a, uint32_t b, BitpairCounters *BC) {
    uint32_t mask = 1;
    for (uint32_t i = 0; i < NBITS; ++i) {
        uint32_t j = ((a & mask) >> i);
        uint32_t k = ((b & mask) >> i);
        BC->C[NBITS - i - 1][j][k] += 1;
        mask <<= 1;
    }
}

static BitpairCounters bitpair_count(float *A, float *B, size_t n) {
    uint32_t *Auint = (uint32_t *)A;
    uint32_t *Buint = (uint32_t *)B;
    BitpairCounters BC = {0};
    
    for (size_t i = 0; i < n; ++i) {
        bitpair_count_kernel(Auint[i], Buint[i], &BC);
    }
    
    return BC;
}

static double mutual_information_kernel(double p[2][2]) {
    double py[2] = { p[0][0] + p[1][0], p[0][1] + p[1][1] };
    double px[2] = { p[0][0] + p[0][1], p[1][0] + p[1][1] };
    
    double M = 0.0;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            if (p[i][j] > 0.0) {
                M += p[i][j] * log(p[i][j] / px[i] / py[j]);
            }
        }
    }
    
    return M / log(2.0);
}

static MutualInformation mutual_information(float *A, float *B, size_t nelements) {
    const double confidence = 0.99;
    BitpairCounters BC = bitpair_count(A, B, nelements);
    
    MutualInformation MI = {0};
    double P[2][2];
    
    for (int i = 0; i < NBITS; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                P[j][k] = ((double)BC.C[i][j][k]) / nelements;
            }
        }
        MI.M[i] = mutual_information_kernel(P);
    }
    
    set_zero_insignificant(MI.M, nelements, confidence);
    return MI;
}

static uint32_t signed_exponent_kernel(uint32_t Auint) {
    const uint32_t float_sign_mask = 0x80000000;
    const uint32_t float_significand_mask = 0x007fffff;
    const uint32_t float_exponent_mask = 0x7f800000;
    const int32_t float_significand_bits = 23;
    const int32_t float_exponent_bias = 127;
    
    const uint32_t sfmask = (float_sign_mask | float_significand_mask);
    const uint32_t emask = float_exponent_mask;
    const uint32_t esignmask = (float_sign_mask >> 1);
    
    const uint32_t sbits = float_significand_bits;
    const int32_t bias = float_exponent_bias;
    
    uint32_t ui = Auint;
    uint32_t sf = ui & sfmask;
    int32_t e = ((int32_t)((ui & emask) >> sbits)) - bias;
    uint32_t eabs = (uint32_t)abs(e);
    uint32_t esign = (e < 0) ? esignmask : 0;
    uint32_t esigned = esign | (eabs << sbits);
    
    return (sf | esigned);
}

void signed_exponent(float *A, size_t n) {
    uint32_t *Auint = (uint32_t *)A;
    for (size_t i = 0; i < n; ++i) {
        Auint[i] = signed_exponent_kernel(Auint[i]);
    }
}

MutualInformation bitinformation(float *A, size_t n) {
    return mutual_information(A, A + 1, n - 1);
}

int get_keepbits(const MutualInformation *bitInfo, double inflevel) {
    const int floatNMBITS = 9;
    int keepMantissaBits = 23;
    
    double bitInfoMax = -1e33;
    for (int i = 0; i < NBITS; ++i) {
        if (bitInfo->M[i] > bitInfoMax) bitInfoMax = bitInfo->M[i];
    }
    
    double bitInfoMaxLast4 = -1e33;
    for (int i = NBITS - 4; i < NBITS; ++i) {
        if (bitInfo->M[i] > bitInfoMaxLast4) bitInfoMaxLast4 = bitInfo->M[i];
    }
    bitInfoMaxLast4 *= 1.5;
    
    MutualInformation infoPerBitCleaned = {0};
    for (int i = 0; i < NBITS; ++i) {
        infoPerBitCleaned.M[i] = (bitInfo->M[i] > bitInfoMaxLast4) ? bitInfo->M[i] : 0.0;
    }
    
    for (int i = 1; i < NBITS; ++i) {
        infoPerBitCleaned.M[i] += infoPerBitCleaned.M[i - 1];
    }
    
    double lastBit = infoPerBitCleaned.M[NBITS - 1];
    if (lastBit > 0.0) {
        MutualInformation cdf = {0};
        for (int i = 0; i < NBITS; ++i) {
            cdf.M[i] = infoPerBitCleaned.M[i] / lastBit;
        }
        
        const int nonMantissaBits = floatNMBITS;
        
        for (int i = 0; i < NBITS; ++i) {
            if (cdf.M[i] > inflevel) {
                keepMantissaBits = i + 1 - nonMantissaBits;
                break;
            }
        }
    }
    
    int nsb = keepMantissaBits;
    if (nsb < 1) nsb = 1;
    if (nsb > 23) nsb = 23;
    
    return nsb;
}