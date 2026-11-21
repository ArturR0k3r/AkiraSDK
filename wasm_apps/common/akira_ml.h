/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file akira_ml.h
 * @brief Float math for on-device ML in WASM apps (header-only, no libm).
 *
 * The apps link with -nostdlib, so expf/logf/tanhf are not available.  These
 * approximations are accurate to ~1e-6 relative (exp/log) over the ranges ML
 * code uses, cost a handful of operations, and behave the same natively and
 * in WASM.  Square root maps to the f32.sqrt instruction.
 */

#ifndef AKIRA_ML_H
#define AKIRA_ML_H

#include <stdint.h>

typedef union {
    float f;
    int32_t i;
} ml_bits_t;

/** e^x, saturating to 0 below -87 and clamped at e^88. */
static inline float ml_expf(float x)
{
    if (x < -87.0f) return 0.0f;
    if (x > 88.0f) x = 88.0f;
    float t = x * 1.44269504f; /* log2(e) */
    int i = (int)t;
    if (t < 0.0f && (float)i != t) i--; /* floor */
    float f = t - (float)i;              /* [0, 1) */
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    ml_bits_t u;
    u.i = (i + 127) << 23; /* 2^i */
    return p * u.f;
}

/** Natural log for x > 0 (returns -87 for x <= 0). */
static inline float ml_logf(float x)
{
    if (x <= 0.0f) return -87.0f;
    ml_bits_t u;
    u.f = x;
    int e = ((u.i >> 23) & 255) - 127;
    u.i = (u.i & 0x7FFFFF) | 0x3F800000; /* mantissa in [1, 2) */
    float m = u.f;
    if (m > 1.41421356f) { /* keep s small: m in [0.707, 1.414) */
        m *= 0.5f;
        e++;
    }
    float s = (m - 1.0f) / (m + 1.0f), s2 = s * s;
    float lm = 2.0f * s * (1.0f + s2 * (0.33333333f + s2 * (0.2f + s2 * (0.14285714f + s2 * 0.11111111f))));
    return lm + (float)e * 0.69314718f;
}

static inline float ml_tanhf(float x)
{
    if (x > 9.0f) return 1.0f;
    if (x < -9.0f) return -1.0f;
    float e = ml_expf(2.0f * x);
    return (e - 1.0f) / (e + 1.0f);
}

static inline float ml_sigmoidf(float x) { return 1.0f / (1.0f + ml_expf(-x)); }

static inline float ml_sqrtf(float x) { return x > 0.0f ? __builtin_sqrtf(x) : 0.0f; }

static inline float ml_absf(float x) { return x < 0.0f ? -x : x; }

/** In-place softmax of v[0..n) with temperature @p temp (> 0). */
static inline void ml_softmax(float *v, int n, float temp)
{
    float mx = v[0], sum = 0.0f;
    for (int i = 1; i < n; i++) {
        if (v[i] > mx) mx = v[i];
    }
    for (int i = 0; i < n; i++) {
        v[i] = ml_expf((v[i] - mx) / temp);
        sum += v[i];
    }
    for (int i = 0; i < n; i++) v[i] /= sum;
}

/** Index drawn from probabilities p[0..n) given a uniform u in [0, 1). */
static inline int ml_sample(const float *p, int n, float u)
{
    float acc = 0.0f;
    for (int i = 0; i < n; i++) {
        acc += p[i];
        if (u < acc) return i;
    }
    return n - 1;
}

#endif /* AKIRA_ML_H */
