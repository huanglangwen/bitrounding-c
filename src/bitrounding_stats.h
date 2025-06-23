#ifndef BITROUNDING_STATS_H
#define BITROUNDING_STATS_H

#include <stddef.h>

double normal_density(double x);
double normal(double x);
double normal_inv(double p);
double binom_confidence(size_t n, double c);
double binom_free_entropy(size_t n, double c);

#endif