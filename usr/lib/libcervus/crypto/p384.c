#include <crypto.h>
#include <string.h>

typedef unsigned __int128 u128;
#define NL 6
typedef uint64_t f384[NL];

typedef struct { uint64_t m[NL]; uint64_t n0; uint64_t r2[NL]; } mont;

static const uint8_t P_BE[48] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff };
static const uint8_t N_BE[48] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xc7,0x63,0x4d,0x81,0xf4,0x37,0x2d,0xdf,
    0x58,0x1a,0x0d,0xb2,0x48,0xb0,0xa7,0x7a,0xec,0xec,0x19,0x6a,0xcc,0xc5,0x29,0x73 };
static const uint8_t GX_BE[48] = {
    0xaa,0x87,0xca,0x22,0xbe,0x8b,0x05,0x37,0x8e,0xb1,0xc7,0x1e,0xf3,0x20,0xad,0x74,
    0x6e,0x1d,0x3b,0x62,0x8b,0xa7,0x9b,0x98,0x59,0xf7,0x41,0xe0,0x82,0x54,0x2a,0x38,
    0x55,0x02,0xf2,0x5d,0xbf,0x55,0x29,0x6c,0x3a,0x54,0x5e,0x38,0x72,0x76,0x0a,0xb7 };
static const uint8_t GY_BE[48] = {
    0x36,0x17,0xde,0x4a,0x96,0x26,0x2c,0x6f,0x5d,0x9e,0x98,0xbf,0x92,0x92,0xdc,0x29,
    0xf8,0xf4,0x1d,0xbd,0x28,0x9a,0x14,0x7c,0xe9,0xda,0x31,0x13,0xb5,0xf0,0xb8,0xc0,
    0x0a,0x60,0xb1,0xce,0x1d,0x7e,0x81,0x9d,0x7a,0x43,0x1d,0x7c,0x90,0xea,0x0e,0x5f };

static void be_to_limbs(const uint8_t *b, uint64_t o[NL]) {
    for (int i = 0; i < NL; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = (v << 8) | b[i*8 + j];
        o[NL-1 - i] = v;
    }
}
static void limbs_to_be(const uint64_t a[NL], uint8_t *b) {
    for (int i = 0; i < NL; i++) {
        uint64_t v = a[NL-1 - i];
        for (int j = 0; j < 8; j++) b[i*8 + j] = (uint8_t)(v >> (56 - j*8));
    }
}
static int u_cmp(const uint64_t a[NL], const uint64_t b[NL]) {
    for (int i = NL-1; i >= 0; i--) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
static int u_sub(uint64_t a[NL], const uint64_t b[NL]) {
    u128 br = 0;
    for (int i = 0; i < NL; i++) { u128 t = (u128)a[i] - b[i] - br; a[i] = (uint64_t)t; br = (t >> 127) & 1; }
    return (int)br;
}
static int u_add(uint64_t a[NL], const uint64_t b[NL]) {
    u128 c = 0;
    for (int i = 0; i < NL; i++) { u128 t = (u128)a[i] + b[i] + c; a[i] = (uint64_t)t; c = t >> 64; }
    return (int)c;
}
static int u_zero(const uint64_t a[NL]) { uint64_t x = 0; for (int i = 0; i < NL; i++) x |= a[i]; return x == 0; }

static void mont_setup(mont *C, const uint8_t *m_be) {
    be_to_limbs(m_be, C->m);
    uint64_t m0 = C->m[0], inv = m0;
    for (int i = 0; i < 6; i++) inv = inv * (2 - m0 * inv);
    C->n0 = (uint64_t)(-inv);
    uint64_t t[NL]; memset(t, 0, sizeof t); t[0] = 1;
    for (int i = 0; i < NL*128; i++) {
        int carry = u_add(t, t);
        if (carry || u_cmp(t, C->m) >= 0) u_sub(t, C->m);
    }
    memcpy(C->r2, t, sizeof t);
}

static void mont_mul(const mont *C, const uint64_t a[NL], const uint64_t b[NL], uint64_t out[NL]) {
    uint64_t t[NL+2]; memset(t, 0, sizeof t);
    for (int i = 0; i < NL; i++) {
        u128 c = 0;
        for (int j = 0; j < NL; j++) { u128 x = (u128)a[j]*b[i] + t[j] + c; t[j] = (uint64_t)x; c = x >> 64; }
        u128 s = (u128)t[NL] + c; t[NL] = (uint64_t)s; t[NL+1] = (uint64_t)(s >> 64);
        uint64_t mm = t[0] * C->n0;
        u128 x0 = (u128)mm*C->m[0] + t[0]; c = x0 >> 64;
        for (int j = 1; j < NL; j++) { u128 x = (u128)mm*C->m[j] + t[j] + c; t[j-1] = (uint64_t)x; c = x >> 64; }
        u128 s2 = (u128)t[NL] + c; t[NL-1] = (uint64_t)s2; c = s2 >> 64;
        t[NL] = t[NL+1] + (uint64_t)c; t[NL+1] = 0;
    }
    if (t[NL] || u_cmp(t, C->m) >= 0) u_sub(t, C->m);
    memcpy(out, t, NL*8);
}

static void mont_to(const mont *C, const uint64_t a[NL], uint64_t o[NL]) { mont_mul(C, a, C->r2, o); }
static void mont_from(const mont *C, const uint64_t a[NL], uint64_t o[NL]) { uint64_t one[NL]={1}; mont_mul(C, a, one, o); }
static void fadd(const mont *C, const uint64_t a[NL], const uint64_t b[NL], uint64_t o[NL]) {
    memcpy(o, a, NL*8); int c = u_add(o, b); if (c || u_cmp(o, C->m) >= 0) u_sub(o, C->m);
}
static void fsub(const mont *C, const uint64_t a[NL], const uint64_t b[NL], uint64_t o[NL]) {
    memcpy(o, a, NL*8); if (u_sub(o, b)) u_add(o, C->m);
}
static void finv(const mont *C, const uint64_t a[NL], uint64_t o[NL]) {
    uint8_t exp[48]; uint64_t e[NL]; memcpy(e, C->m, NL*8);
    uint64_t two[NL]={2}; u_sub(e, two);
    limbs_to_be(e, exp);
    uint64_t r[NL], one[NL]={1}; mont_to(C, one, r);
    int started = 0;
    for (int i = 0; i < 48; i++)
        for (int b = 7; b >= 0; b--) {
            if (started) mont_mul(C, r, r, r);
            if ((exp[i] >> b) & 1) { if (!started) { memcpy(r, a, NL*8); started = 1; } else mont_mul(C, r, a, r); }
        }
    memcpy(o, r, NL*8);
}

typedef struct { f384 X, Y, Z; } jpoint;
static const mont *Cp;

static void small_mul(const uint64_t a[NL], int k, uint64_t o[NL]) {
    uint64_t acc[NL] = {0};
    for (int i = 0; i < k; i++) fadd(Cp, acc, a, acc);
    memcpy(o, acc, NL*8);
}
static int p_is_inf(const jpoint *P) { return u_zero(P->Z); }

static void p_double(const jpoint *P, jpoint *R) {
    if (p_is_inf(P)) { memset(R, 0, sizeof *R); return; }
    f384 delta, gamma, beta, alpha, t1, t2, x3, y3, z3;
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
    memcpy(R->X, x3, NL*8); memcpy(R->Y, y3, NL*8); memcpy(R->Z, z3, NL*8);
}

static void p_add(const jpoint *P, const jpoint *Q, jpoint *R) {
    if (p_is_inf(P)) { *R = *Q; return; }
    if (p_is_inf(Q)) { *R = *P; return; }
    f384 z1z1, z2z2, u1, u2, s1, s2, h, i, j, r, v, t, x3, y3, z3;
    mont_mul(Cp, P->Z, P->Z, z1z1);
    mont_mul(Cp, Q->Z, Q->Z, z2z2);
    mont_mul(Cp, P->X, z2z2, u1);
    mont_mul(Cp, Q->X, z1z1, u2);
    mont_mul(Cp, P->Y, Q->Z, s1); mont_mul(Cp, s1, z2z2, s1);
    mont_mul(Cp, Q->Y, P->Z, s2); mont_mul(Cp, s2, z1z1, s2);
    fsub(Cp, u2, u1, h);
    fsub(Cp, s2, s1, r);
    if (u_zero(h)) { if (u_zero(r)) { p_double(P, R); return; } memset(R, 0, sizeof *R); return; }
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
    memcpy(R->X, x3, NL*8); memcpy(R->Y, y3, NL*8); memcpy(R->Z, z3, NL*8);
}

static void scalarmult(const uint64_t k[NL], const jpoint *P, jpoint *R) {
    jpoint acc; memset(&acc, 0, sizeof acc);
    int started = 0;
    for (int i = NL*64 - 1; i >= 0; i--) {
        if (started) p_double(&acc, &acc);
        if ((k[i >> 6] >> (i & 63)) & 1) { if (!started) { acc = *P; started = 1; } else p_add(&acc, P, &acc); }
    }
    *R = acc;
}

int ecdsa_p384_verify(const uint8_t *pub, size_t publen,
                      const uint8_t *sig_der, size_t siglen, const uint8_t *hash, size_t hlen) {
    static mont MP, MN;
    static int inited = 0;
    if (!inited) { mont_setup(&MP, P_BE); mont_setup(&MN, N_BE); inited = 1; }
    Cp = &MP;
    if (publen != 97 || pub[0] != 0x04) return -1;

    size_t pos = 0;
    if (pos >= siglen || sig_der[pos++] != 0x30) return -1;
    if (pos >= siglen) return -1;
    size_t sl0 = sig_der[pos++]; if (sl0 & 0x80) return -1;
    uint8_t rb[48], sb[48];
    if (pos >= siglen || sig_der[pos++] != 0x02) return -1;
    size_t rl = sig_der[pos++]; if (rl & 0x80 || pos + rl > siglen || rl > 49) return -1;
    { const uint8_t *v = sig_der + pos; size_t l = rl; while (l>0 && v[0]==0){v++;l--;} if (l>48) return -1; memset(rb,0,48); memcpy(rb+(48-l),v,l); }
    pos += rl;
    if (pos >= siglen || sig_der[pos++] != 0x02) return -1;
    size_t sl = sig_der[pos++]; if (sl & 0x80 || pos + sl > siglen || sl > 49) return -1;
    { const uint8_t *v = sig_der + pos; size_t l = sl; while (l>0 && v[0]==0){v++;l--;} if (l>48) return -1; memset(sb,0,48); memcpy(sb+(48-l),v,l); }

    uint64_t r[NL], s[NL];
    be_to_limbs(rb, r); be_to_limbs(sb, s);
    if (u_zero(r) || u_zero(s) || u_cmp(r, MN.m) >= 0 || u_cmp(s, MN.m) >= 0) return -1;

    uint8_t zb[48]; memset(zb, 0, 48);
    if (hlen >= 48) memcpy(zb, hash, 48);
    else memcpy(zb + (48 - hlen), hash, hlen);
    uint64_t z[NL]; be_to_limbs(zb, z);
    if (u_cmp(z, MN.m) >= 0) u_sub(z, MN.m);

    uint64_t zm[NL], rm[NL], sm[NL], wm[NL], u1m[NL], u2m[NL], u1[NL], u2[NL];
    mont_to(&MN, z, zm); mont_to(&MN, r, rm); mont_to(&MN, s, sm);
    finv(&MN, sm, wm);
    mont_mul(&MN, zm, wm, u1m);
    mont_mul(&MN, rm, wm, u2m);
    mont_from(&MN, u1m, u1);
    mont_from(&MN, u2m, u2);

    uint64_t gx[NL], gy[NL], one[NL]={1};
    jpoint G, Q, R1, R2, RR;
    be_to_limbs(GX_BE, gx); be_to_limbs(GY_BE, gy);
    mont_to(&MP, gx, G.X); mont_to(&MP, gy, G.Y); mont_to(&MP, one, G.Z);
    uint64_t qx[NL], qy[NL];
    be_to_limbs(pub + 1, qx); be_to_limbs(pub + 49, qy);
    mont_to(&MP, qx, Q.X); mont_to(&MP, qy, Q.Y); mont_to(&MP, one, Q.Z);

    scalarmult(u1, &G, &R1);
    scalarmult(u2, &Q, &R2);
    p_add(&R1, &R2, &RR);
    if (p_is_inf(&RR)) return -1;

    f384 z2, z2inv, xaff;
    mont_mul(&MP, RR.Z, RR.Z, z2);
    finv(&MP, z2, z2inv);
    mont_mul(&MP, RR.X, z2inv, xaff);
    uint64_t xa[NL]; mont_from(&MP, xaff, xa);
    if (u_cmp(xa, MN.m) >= 0) u_sub(xa, MN.m);
    return (u_cmp(xa, r) == 0) ? 0 : -1;
}
