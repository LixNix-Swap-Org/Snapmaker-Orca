// slice_facet() in doubles emulated by 64-bit integers or, with USE_FP64, native; bit-exact; tests build it as C++.

#ifdef __cplusplus
typedef uint64_t sd_u64;
typedef int64_t  sd_i64;
typedef uint32_t sd_u32;
static inline sd_u64 sd_mul_hi(sd_u64 a, sd_u64 b)
{
    const sd_u64 al = a & 0xffffffffu, ah = a >> 32, bl = b & 0xffffffffu, bh = b >> 32;
    const sd_u64 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    const sd_u64 mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
static inline int sd_clz(sd_u64 x)
{
    int n = 0;
    for (sd_u64 bit = sd_u64(1) << 63; bit != 0 && (x & bit) == 0; bit >>= 1)
        ++ n;
    return n;
}
static inline sd_u32 sd_float_bits(float f)
{
    sd_u32 u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
#define SD_INLINE static inline
#else
typedef ulong sd_u64;
typedef long  sd_i64;
typedef uint  sd_u32;
#define sd_mul_hi(a, b) mul_hi((ulong)(a), (ulong)(b))
#define sd_clz(x) ((int)clz((ulong)(x)))
#define sd_float_bits(f) as_uint(f)
#define SD_INLINE
#endif

typedef struct { sd_u64 hi, lo; } sd_u128;
// (-1)^neg * m * 2^e, exact.
typedef struct { sd_u128 m; int e; int neg; } sd_wide;
// A double: m < 2^53 with bit 52 set, or m == 0.
typedef struct { sd_u64 m; int e; int neg; } sd_dbl;

SD_INLINE int sd_bitlen(sd_u128 x) { return x.hi != 0 ? 128 - sd_clz(x.hi) : 64 - sd_clz(x.lo); }

// 0 <= s < 128
SD_INLINE sd_u128 sd_shl(sd_u128 x, int s)
{
    sd_u128 r = x;
    if (s >= 64) {
        r.hi = x.lo << (s - 64);
        r.lo = 0;
    } else if (s > 0) {
        r.hi = (x.hi << s) | (x.lo >> (64 - s));
        r.lo = x.lo << s;
    }
    return r;
}

SD_INLINE sd_u128 sd_shr(sd_u128 x, int s)
{
    sd_u128 r = x;
    if (s >= 64) {
        r.lo = x.hi >> (s - 64);
        r.hi = 0;
    } else if (s > 0) {
        r.lo = (x.lo >> s) | (x.hi << (64 - s));
        r.hi = x.hi >> s;
    }
    return r;
}

// Any of the lowest s bits set, 0 <= s <= 128.
SD_INLINE int sd_low_bits(sd_u128 x, int s)
{
    if (s <= 0)
        return 0;
    if (s >= 128)
        return (x.hi | x.lo) != 0;
    if (s >= 64)
        return x.lo != 0 || (s > 64 && (x.hi << (128 - s)) != 0);
    return (x.lo << (64 - s)) != 0;
}

SD_INLINE sd_u128 sd_add128(sd_u128 a, sd_u128 b)
{
    sd_u128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);
    return r;
}

SD_INLINE sd_u128 sd_sub128(sd_u128 a, sd_u128 b)
{
    sd_u128 r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0);
    return r;
}

SD_INLINE int sd_less128(sd_u128 a, sd_u128 b) { return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo; }

// Round to nearest even of (m + sticky) * 2^e at 53 bits; sticky only with more than 54 bits in m.
SD_INLINE sd_dbl sd_round(sd_u128 m, int e, int neg, int sticky)
{
    sd_dbl r;
    r.neg = neg;
    const int len = sd_bitlen(m);
    if (len == 0) {
        r.m = 0;
        r.e = 0;
        r.neg = 0;
    } else if (len <= 53) {
        r.m = sd_shl(m, 53 - len).lo;
        r.e = e - (53 - len);
    } else {
        int    s     = len - 53;
        sd_u64 kept  = sd_shr(m, s).lo;
        int    guard = (int)(sd_shr(m, s - 1).lo & 1);
        if (guard && (sticky || sd_low_bits(m, s - 1) || (kept & 1))) {
            ++ kept;
            if ((kept >> 53) != 0) {
                kept >>= 1;
                ++ s;
            }
        }
        r.m = kept;
        r.e = e + s;
    }
    return r;
}

SD_INLINE sd_wide sd_widen(sd_dbl d)
{
    sd_wide w;
    w.m.hi = 0;
    w.m.lo = d.m;
    w.e    = d.e;
    w.neg  = d.neg;
    return w;
}

// Round to nearest even of x + y, each with at most 106 significant bits.
SD_INLINE sd_dbl sd_add(sd_wide x, sd_wide y)
{
    const int lx = sd_bitlen(x.m), ly = sd_bitlen(y.m);
    if (ly == 0)
        return sd_round(x.m, x.e, x.neg, 0);
    if (lx == 0)
        return sd_round(y.m, y.e, y.neg, 0);
    // Normalized to 126 bits: at least 20 trailing zero bits, so shifts by up to 20 stay exact.
    sd_u128 X = sd_shl(x.m, 126 - lx), Y = sd_shl(y.m, 126 - ly);
    int     ex = x.e - (126 - lx), ey = y.e - (126 - ly), xneg = x.neg, yneg = y.neg;
    if (ey > ex || (ey == ex && sd_less128(X, Y))) {
        const sd_u128 T = X; X = Y; Y = T;
        int t = ex; ex = ey; ey = t;
        t = xneg; xneg = yneg; yneg = t;
    }
    const int d      = ex - ey;
    int       sticky = 0;
    if (d >= 128) {
        sticky = 1;
        Y.hi = Y.lo = 0;
    } else if (d > 0) {
        sticky = sd_low_bits(Y, d);
        Y      = sd_shr(Y, d);
    }
    sd_u128 S;
    if (xneg == yneg)
        S = sd_add128(X, Y);
    else {
        S = sd_sub128(X, Y);
        if (sticky) {
            sd_u128 one;
            one.hi = 0;
            one.lo = 1;
            S = sd_sub128(S, one);
        }
    }
    return sd_round(S, ex, xneg, sticky);
}

SD_INLINE sd_wide sd_mul(sd_dbl a, sd_dbl b)
{
    sd_wide w;
    w.m.lo = a.m * b.m;
    w.m.hi = sd_mul_hi(a.m, b.m);
    w.e    = a.e + b.e;
    w.neg  = a.neg ^ b.neg;
    return w;
}

// Round to nearest even of n / d, d != 0.
SD_INLINE sd_dbl sd_div(sd_dbl n, sd_dbl d)
{
    sd_u128 q;
    q.hi = 0;
    q.lo = 0;
    if (n.m == 0)
        return sd_round(q, 0, 0, 0);
    sd_u64 r = n.m;
    for (int i = 0; i < 56; ++ i) {
        q.lo <<= 1;
        if (r >= d.m) {
            r -= d.m;
            q.lo |= 1;
        }
        r <<= 1;
    }
    return sd_round(q, n.e - d.e - 55, n.neg ^ d.neg, r != 0);
}

SD_INLINE sd_dbl sd_from_float(float f, int *bad)
{
    const sd_u32 u  = sd_float_bits(f);
    const int    ex = (int)((u >> 23) & 0xff);
    sd_u128      m;
    m.hi = 0;
    m.lo = u & 0x7fffff;
    if (ex == 255)
        *bad = 1;
    if (ex != 0)
        m.lo |= 0x800000;
    sd_dbl r = sd_round(m, ex == 0 ? -149 : ex - 150, 0, 0);
    r.neg = m.lo != 0 ? (int)(u >> 31) : 0;
    return r;
}

SD_INLINE sd_dbl sd_neg(sd_dbl d)
{
    d.neg = d.m != 0 ? ! d.neg : 0;
    return d;
}

// floor(x), x below 2^62 in magnitude.
SD_INLINE sd_i64 sd_floor(sd_dbl x, int *bad)
{
    if (x.m == 0)
        return 0;
    if (x.e >= 0) {
        if (x.e > 9)
            *bad = 1;
        const sd_i64 v = (sd_i64)(x.m << (x.e > 9 ? 0 : x.e));
        return x.neg ? -v : v;
    }
    const int    s    = -x.e;
    const sd_u64 ip   = s >= 64 ? 0 : x.m >> s;
    const int    frac = s >= 64 ? 1 : (x.m & ((((sd_u64)1) << s) - 1)) != 0;
    return x.neg ? -(sd_i64)ip - frac : (sd_i64)ip;
}

// t = (double(z) - double(bz)) / (double(az) - double(bz)); -1 for t <= 0, 1 for t >= 1, else 0.
SD_INLINE int sd_param(float z, float az, float bz, sd_dbl *t, int *bad)
{
    const sd_dbl nb = sd_neg(sd_from_float(bz, bad));
    const sd_dbl n  = sd_add(sd_widen(sd_from_float(z, bad)), sd_widen(nb));
    const sd_dbl d  = sd_add(sd_widen(sd_from_float(az, bad)), sd_widen(nb));
    if (d.m == 0) {
        *bad = 1;
        t->m = 0;
        t->e = 0;
        t->neg = 0;
        return -1;
    }
    *t = sd_div(n, d);
    if (t->m == 0 || t->neg)
        return -1;
    return t->e + 52 >= 0 ? 1 : 0;
}

// floor(double(b) + (double(a) - double(b)) * t + 0.5), the product fused (fma) or rounded first.
SD_INLINE sd_i64 sd_lerp(float a, float b, sd_dbl t, int fused, int *bad)
{
    const sd_dbl fb = sd_from_float(b, bad);
    const sd_dbl dx = sd_add(sd_widen(sd_from_float(a, bad)), sd_widen(sd_neg(fb)));
    sd_wide      p  = sd_mul(dx, t);
    if (! fused)
        p = sd_widen(sd_round(p.m, p.e, p.neg, 0));
    sd_dbl one_half;
    one_half.m   = ((sd_u64)1) << 52;
    one_half.e   = -53;
    one_half.neg = 0;
    return sd_floor(sd_add(sd_widen(sd_add(sd_widen(fb), p)), sd_widen(one_half)), bad);
}

#ifndef __cplusplus

#if USE_FP64
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
// Products rounded on their own, as on the host.
#pragma OPENCL FP_CONTRACT OFF
SD_INLINE sd_i64 fp_lerp(float a, float b, double t)
{
    const double fb = (double)b, d = (double)a - fb;
#if HOST_FMA
    const double s = fma(d, t, fb);
#else
    const double p = d * t;
    const double s = fb + p;
#endif
    return (sd_i64)floor(s + 0.5);
}
#endif

#define SL_NONE 3u

// One work-item per (facet, plane) record: line a b, code bit 0 kept, bit 1 top, then 2-bit facet ids.
__kernel void slice_lines(__global const float *v, __global const int *tri, __global const float *zs, __global const uint *first,
                          __global const uint *offset, const uint face_begin, const uint face_end, const uint rec_begin,
                          __global int *pts, __global uint *codes, __global int *bad_flag)
{
    const uint rec = rec_begin + (uint)get_global_id(0);
    if (rec >= offset[face_end])
        return;
    uint f = face_begin, hi = face_end;
    while (hi - f > 1) {
        const uint mid = f + (hi - f) / 2;
        if (offset[mid] <= rec)
            f = mid;
        else
            hi = mid;
    }
    int   id[3];
    float x[3], y[3], z[3];
    for (int k = 0; k < 3; ++ k) {
        id[k] = tri[3 * f + k];
        x[k]  = v[3 * id[k]];
        y[k]  = v[3 * id[k] + 1];
        z[k]  = v[3 * id[k] + 2];
    }
    const float min_z  = fmin(z[0], fmin(z[1], z[2]));
    const int   lowest = z[1] == min_z ? 1 : (z[2] == min_z ? 2 : 0);
    const float sz     = zs[first[f] + (rec - offset[f])];
    int    bad = 0;
    sd_i64 px[3], py[3];
    uint   vert[3], edge[3];
    int    vid[3];
    int    num = 0, on_layer = -1;
    uint   code = 0;
    sd_i64 line[4];
    int    edge_done = 0;
    for (int j = 0; j < 3 && ! edge_done; ++ j) {
        const int k = (lowest + j) % 3, l = (k + 1) % 3;
        int a = k, b = l;
        if (z[a] == sz && z[b] == sz) {
            // An edge on the plane: kept as the top edge of a facet whose third vertex is below.
            if (z[0] < sz || z[1] < sz || z[2] < sz) {
                line[0] = (sd_i64)x[b];
                line[1] = (sd_i64)y[b];
                line[2] = (sd_i64)x[a];
                line[3] = (sd_i64)y[a];
                code    = 1u | 2u | ((uint)b << 2) | ((uint)a << 4) | (SL_NONE << 6) | (SL_NONE << 8);
            }
            edge_done = 1;
        } else if (z[a] == sz || z[b] == sz) {
            const int c = z[a] == sz ? a : b;
            if (on_layer < 0 || vid[on_layer] != id[c]) {
                on_layer  = num;
                px[num]   = (sd_i64)x[c];
                py[num]   = (sd_i64)y[c];
                vid[num]  = id[c];
                vert[num] = (uint)c;
                edge[num] = SL_NONE;
                ++ num;
            }
        } else if ((z[a] < sz && z[b] > sz) || (z[b] < sz && z[a] > sz)) {
            if (id[a] > id[b]) {
                const int s = a; a = b; b = s;
            }
#if USE_FP64
            const double nb = (double)z[b], t = ((double)sz - nb) / ((double)z[a] - nb);
            const int    side = t <= 0. ? -1 : t >= 1. ? 1 : 0;
#else
            sd_dbl    t;
            const int side = sd_param(sz, z[a], z[b], &t, &bad);
#endif
            if (side != 0) {
                const int c = side < 0 ? a : b;
                if (on_layer < 0 || vid[on_layer] != id[c]) {
                    px[num]   = (sd_i64)x[c];
                    py[num]   = (sd_i64)y[c];
                    vid[num]  = id[c];
                    vert[num] = (uint)c;
                    edge[num] = SL_NONE;
                    on_layer  = num ++;
                }
            } else {
#if USE_FP64
                px[num]   = fp_lerp(x[a], x[b], t);
                py[num]   = fp_lerp(y[a], y[b], t);
#else
                px[num]   = sd_lerp(x[a], x[b], t, HOST_FMA, &bad);
                py[num]   = sd_lerp(y[a], y[b], t, HOST_FMA, &bad);
#endif
                vid[num]  = -1;
                vert[num] = SL_NONE;
                edge[num] = (uint)k;
                ++ num;
            }
        }
    }
    if (! edge_done && num == 2) {
        line[0] = px[1];
        line[1] = py[1];
        line[2] = px[0];
        line[3] = py[0];
        code    = 1u | (vert[1] << 2) | (vert[0] << 4) | (edge[1] << 6) | (edge[0] << 8);
    }
    const uint r = rec - rec_begin;
    codes[r] = code;
    if (code != 0)
        for (int q = 0; q < 4; ++ q)
            pts[4 * r + q] = (int)line[q];
    if (bad)
        bad_flag[0] = 1;
}

#endif
