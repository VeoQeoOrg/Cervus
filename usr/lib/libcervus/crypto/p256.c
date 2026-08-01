#include <crypto.h>
#include <string.h>

typedef unsigned __int128 u128;
typedef uint64_t f256[4];

typedef struct { uint64_t m[4]; uint64_t n0; uint64_t r2[4]; } mont;

static const uint8_t P_BE[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff };
static const uint8_t N_BE[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51 };
static const uint8_t GX_BE[32] = {
    0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96 };
static const uint8_t GY_BE[32] = {
    0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5 };

static void be_to_limbs(const uint8_t *b, uint64_t o[4]) {
    for (int i = 0; i < 4; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = (v << 8) | b[i*8 + j];
        o[3 - i] = v;
    }
}
static void limbs_to_be(const uint64_t a[4], uint8_t *b) {
    for (int i = 0; i < 4; i++) {
        uint64_t v = a[3 - i];
        for (int j = 0; j < 8; j++) b[i*8 + j] = (uint8_t)(v >> (56 - j*8));
    }
}

static int u256_cmp(const uint64_t a[4], const uint64_t b[4]) {
    for (int i = 3; i >= 0; i--) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
static int u256_sub(uint64_t a[4], const uint64_t b[4]) {
    u128 borrow = 0;
    for (int i = 0; i < 4; i++) { u128 t = (u128)a[i] - b[i] - borrow; a[i] = (uint64_t)t; borrow = (t >> 127) & 1; }
    return (int)borrow;
}
static int u256_add(uint64_t a[4], const uint64_t b[4]) {
    u128 carry = 0;
    for (int i = 0; i < 4; i++) { u128 t = (u128)a[i] + b[i] + carry; a[i] = (uint64_t)t; carry = t >> 64; }
    return (int)carry;
}
static int u256_iszero(const uint64_t a[4]) { return (a[0]|a[1]|a[2]|a[3]) == 0; }

static void mont_setup(mont *C, const uint8_t *m_be) {
    be_to_limbs(m_be, C->m);
    uint64_t m0 = C->m[0];
    uint64_t inv = m0;
    for (int i = 0; i < 6; i++) inv = inv * (2 - m0 * inv);
    C->n0 = (uint64_t)(-inv);
    uint64_t t[4] = {1,0,0,0};
    for (int i = 0; i < 512; i++) {
        int carry = u256_add(t, t);
        if (carry || u256_cmp(t, C->m) >= 0) u256_sub(t, C->m);
    }
    memcpy(C->r2, t, sizeof t);
}

static void mont_mul(const mont *C, const uint64_t a[4], const uint64_t b[4], uint64_t out[4]) {
    uint64_t t[6]; memset(t, 0, sizeof t);
    for (int i = 0; i < 4; i++) {
        u128 c = 0;
        for (int j = 0; j < 4; j++) { u128 x = (u128)a[j]*b[i] + t[j] + c; t[j] = (uint64_t)x; c = x >> 64; }
        u128 s = (u128)t[4] + c; t[4] = (uint64_t)s; t[5] = (uint64_t)(s >> 64);
        uint64_t mm = t[0] * C->n0;
        u128 x0 = (u128)mm*C->m[0] + t[0]; c = x0 >> 64;
        for (int j = 1; j < 4; j++) { u128 x = (u128)mm*C->m[j] + t[j] + c; t[j-1] = (uint64_t)x; c = x >> 64; }
        u128 s2 = (u128)t[4] + c; t[3] = (uint64_t)s2; c = s2 >> 64;
        t[4] = t[5] + (uint64_t)c; t[5] = 0;
    }
    if (t[4] || u256_cmp(t, C->m) >= 0) u256_sub(t, C->m);
    memcpy(out, t, 32);
}

static void mont_to(const mont *C, const uint64_t a[4], uint64_t out[4]) { mont_mul(C, a, C->r2, out); }
static void mont_from(const mont *C, const uint64_t a[4], uint64_t out[4]) {
    uint64_t one[4] = {1,0,0,0}; mont_mul(C, a, one, out);
}
static void fadd(const mont *C, const uint64_t a[4], const uint64_t b[4], uint64_t o[4]) {
    memcpy(o, a, 32); int c = u256_add(o, b);
    if (c || u256_cmp(o, C->m) >= 0) u256_sub(o, C->m);
}
static void fsub(const mont *C, const uint64_t a[4], const uint64_t b[4], uint64_t o[4]) {
    memcpy(o, a, 32); int br = u256_sub(o, b);
    if (br) u256_add(o, C->m);
}
static void finv(const mont *C, const uint64_t a[4], uint64_t o[4]) {
    uint8_t exp[32]; uint64_t e[4]; memcpy(e, C->m, 32);
    uint64_t two[4] = {2,0,0,0}; u256_sub(e, two);
    limbs_to_be(e, exp);
    uint64_t r[4]; uint64_t one[4] = {1,0,0,0}; mont_to(C, one, r);
    int started = 0;
    for (int i = 0; i < 32; i++) {
        for (int b = 7; b >= 0; b--) {
            if (started) mont_mul(C, r, r, r);
            if ((exp[i] >> b) & 1) { if (!started) { memcpy(r, a, 32); started = 1; } else mont_mul(C, r, a, r); }
        }
    }
    memcpy(o, r, 32);
}

typedef struct { f256 X, Y, Z; } jpoint;

static const mont *Cp;

static void small_mul(const uint64_t a[4], int k, uint64_t o[4]) {
    uint64_t acc[4] = {0,0,0,0};
    for (int i = 0; i < k; i++) fadd(Cp, acc, a, acc);
    memcpy(o, acc, 32);
}

static int p_is_inf(const jpoint *P) { return u256_iszero(P->Z); }

static void p_double(const jpoint *P, jpoint *R) {
    if (p_is_inf(P)) { memset(R, 0, sizeof *R); return; }
    f256 delta, gamma, beta, alpha, t1, t2, x3, y3, z3;
    mont_mul(Cp, P->Z, P->Z, delta);
    mont_mul(Cp, P->Y, P->Y, gamma);
    mont_mul(Cp, P->X, gamma, beta);
    fsub(Cp, P->X, delta, t1);
    fadd(Cp, P->X, delta, t2);
    mont_mul(Cp, t1, t2, t1);
    small_mul(t1, 3, alpha);
    mont_mul(Cp, alpha, alpha, x3);
    small_mul(beta, 8, t1);
    fsub(Cp, x3, t1, x3);
    fadd(Cp, P->Y, P->Z, z3);
    mont_mul(Cp, z3, z3, z3);
    fsub(Cp, z3, gamma, z3);
    fsub(Cp, z3, delta, z3);
    small_mul(beta, 4, t1);
    fsub(Cp, t1, x3, t1);
    mont_mul(Cp, alpha, t1, y3);
    mont_mul(Cp, gamma, gamma, t2);
    small_mul(t2, 8, t2);
    fsub(Cp, y3, t2, y3);
    memcpy(R->X, x3, 32); memcpy(R->Y, y3, 32); memcpy(R->Z, z3, 32);
}

static void p_add(const jpoint *P, const jpoint *Q, jpoint *R) {
    if (p_is_inf(P)) { *R = *Q; return; }
    if (p_is_inf(Q)) { *R = *P; return; }
    f256 z1z1, z2z2, u1, u2, s1, s2, h, i, j, r, v, t, x3, y3, z3;
    mont_mul(Cp, P->Z, P->Z, z1z1);
    mont_mul(Cp, Q->Z, Q->Z, z2z2);
    mont_mul(Cp, P->X, z2z2, u1);
    mont_mul(Cp, Q->X, z1z1, u2);
    mont_mul(Cp, P->Y, Q->Z, s1); mont_mul(Cp, s1, z2z2, s1);
    mont_mul(Cp, Q->Y, P->Z, s2); mont_mul(Cp, s2, z1z1, s2);
    fsub(Cp, u2, u1, h);
    fsub(Cp, s2, s1, r);
    if (u256_iszero(h)) {
        if (u256_iszero(r)) { p_double(P, R); return; }
        memset(R, 0, sizeof *R); return;
    }
    small_mul(r, 2, r);
    small_mul(h, 2, t);
    mont_mul(Cp, t, t, i);
    mont_mul(Cp, h, i, j);
    mont_mul(Cp, u1, i, v);
    mont_mul(Cp, r, r, x3);
    fsub(Cp, x3, j, x3);
    small_mul(v, 2, t);
    fsub(Cp, x3, t, x3);
    fsub(Cp, v, x3, t);
    mont_mul(Cp, r, t, y3);
    mont_mul(Cp, s1, j, t);
    small_mul(t, 2, t);
    fsub(Cp, y3, t, y3);
    fadd(Cp, P->Z, Q->Z, z3);
    mont_mul(Cp, z3, z3, z3);
    fsub(Cp, z3, z1z1, z3);
    fsub(Cp, z3, z2z2, z3);
    mont_mul(Cp, z3, h, z3);
    memcpy(R->X, x3, 32); memcpy(R->Y, y3, 32); memcpy(R->Z, z3, 32);
}

static void scalarmult(const uint64_t k[4], const jpoint *P, jpoint *R) {
    jpoint acc; memset(&acc, 0, sizeof acc);
    int started = 0;
    for (int i = 255; i >= 0; i--) {
        if (started) p_double(&acc, &acc);
        if ((k[i >> 6] >> (i & 63)) & 1) {
            if (!started) { acc = *P; started = 1; } else p_add(&acc, P, &acc);
        }
    }
    *R = acc;
}

int ecdsa_p256_verify(const uint8_t *pub, size_t publen,
                      const uint8_t *sig_der, size_t siglen, const uint8_t *hash, size_t hlen) {
    static mont MP, MN;
    static int inited = 0;
    if (!inited) { mont_setup(&MP, P_BE); mont_setup(&MN, N_BE); inited = 1; }
    Cp = &MP;

    if (publen != 65 || pub[0] != 0x04) return -1;

    size_t pos = 0;
    if (pos >= siglen || sig_der[pos] != 0x30) return -1;
    pos++;
    if (pos >= siglen) return -1;
    size_t seqlen = sig_der[pos++];
    if (seqlen & 0x80) return -1;
    if (pos + seqlen > siglen) return -1;

    uint8_t rb[32], sb[32];
    if (pos >= siglen || sig_der[pos] != 0x02) return -1;
    pos++;
    size_t rl = sig_der[pos++]; if (rl & 0x80) return -1;
    if (pos + rl > siglen || rl > 33) return -1;
    { const uint8_t *v = sig_der + pos; size_t l = rl; while (l>0 && v[0]==0){v++;l--;} if (l>32) return -1; memset(rb,0,32); memcpy(rb+(32-l),v,l); }
    pos += rl;
    if (pos >= siglen || sig_der[pos] != 0x02) return -1;
    pos++;
    size_t sl = sig_der[pos++]; if (sl & 0x80) return -1;
    if (pos + sl > siglen || sl > 33) return -1;
    { const uint8_t *v = sig_der + pos; size_t l = sl; while (l>0 && v[0]==0){v++;l--;} if (l>32) return -1; memset(sb,0,32); memcpy(sb+(32-l),v,l); }

    uint64_t r[4], s[4];
    be_to_limbs(rb, r); be_to_limbs(sb, s);
    if (u256_iszero(r) || u256_iszero(s)) return -1;
    if (u256_cmp(r, MN.m) >= 0 || u256_cmp(s, MN.m) >= 0) return -1;

    uint8_t zb[32];
    memset(zb, 0, 32);
    if (hlen >= 32) memcpy(zb, hash, 32);
    else memcpy(zb + (32 - hlen), hash, hlen);
    uint64_t z[4]; be_to_limbs(zb, z);
    if (u256_cmp(z, MN.m) >= 0) u256_sub(z, MN.m);

    uint64_t zm[4], rm[4], sm[4], wm[4], u1m[4], u2m[4], u1[4], u2[4];
    mont_to(&MN, z, zm);
    mont_to(&MN, r, rm);
    mont_to(&MN, s, sm);
    finv(&MN, sm, wm);
    mont_mul(&MN, zm, wm, u1m);
    mont_mul(&MN, rm, wm, u2m);
    mont_from(&MN, u1m, u1);
    mont_from(&MN, u2m, u2);

    uint64_t gx[4], gy[4], one[4] = {1,0,0,0};
    jpoint G, Q, R1, R2, RR;
    be_to_limbs(GX_BE, gx); be_to_limbs(GY_BE, gy);
    mont_to(&MP, gx, G.X); mont_to(&MP, gy, G.Y); mont_to(&MP, one, G.Z);

    uint64_t qx[4], qy[4];
    be_to_limbs(pub + 1, qx); be_to_limbs(pub + 33, qy);
    mont_to(&MP, qx, Q.X); mont_to(&MP, qy, Q.Y); mont_to(&MP, one, Q.Z);

    scalarmult(u1, &G, &R1);
    scalarmult(u2, &Q, &R2);
    p_add(&R1, &R2, &RR);
    if (p_is_inf(&RR)) return -1;

    f256 z2, z2inv, xaff;
    mont_mul(&MP, RR.Z, RR.Z, z2);
    finv(&MP, z2, z2inv);
    mont_mul(&MP, RR.X, z2inv, xaff);
    uint64_t xa[4];
    mont_from(&MP, xaff, xa);
    if (u256_cmp(xa, MN.m) >= 0) u256_sub(xa, MN.m);
    return (u256_cmp(xa, r) == 0) ? 0 : -1;
}
