#ifndef _FREESTANDING_MATH_H
#define _FREESTANDING_MATH_H

static inline float fabsf(float x) { return x < 0.0f ? -x : x; }
static inline double fabs(double x) { return x < 0.0 ? -x : x; }
static inline double fmin(double x, double y) { return x < y ? x : y; }
static inline double fmax(double x, double y) { return x > y ? x : y; }

#endif
