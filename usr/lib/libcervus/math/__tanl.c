#include "libm.h"

#if (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
#if LDBL_MANT_DIG == 64

static const long double
T3 =  0.333333333333333333180L,
T5 =  0.133333333333333372290L,
T7 =  0.0539682539682504975744L,
pio4   =  0.785398163397448309628L,
pio4lo = -1.25413940316708300586e-20L;
static const double
T9  =  0.021869488536312216,
T11 =  0.0088632355256619590,
T13 =  0.0035921281113786528,
T15 =  0.0014558334756312418,
T17 =  0.00059003538700862256,
T19 =  0.00023907843576635544,
T21 =  0.000097154625656538905,
T23 =  0.000038440165747303162,
T25 =  0.000018082171885432524,
T27 =  0.0000024196006108814377,
T29 =  0.0000078293456938132840,
T31 = -0.0000032609076735050182,
T33 =  0.0000023261313142559411;
#define RPOLY(w) (T5 + w * (T9 + w * (T13 + w * (T17 + w * (T21 + \
	w * (T25 + w * (T29 + w * T33)))))))
#define VPOLY(w) (T7 + w * (T11 + w * (T15 + w * (T19 + w * (T23 + \
	w * (T27 + w * T31))))))
#elif LDBL_MANT_DIG == 113

static const long double
T3 = 0x1.5555555555555555555555555553p-2L,
T5 = 0x1.1111111111111111111111111eb5p-3L,
T7 = 0x1.ba1ba1ba1ba1ba1ba1ba1b694cd6p-5L,
T9 = 0x1.664f4882c10f9f32d6bbe09d8bcdp-6L,
T11 = 0x1.226e355e6c23c8f5b4f5762322eep-7L,
T13 = 0x1.d6d3d0e157ddfb5fed8e84e27b37p-9L,
T15 = 0x1.7da36452b75e2b5fce9ee7c2c92ep-10L,
T17 = 0x1.355824803674477dfcf726649efep-11L,
T19 = 0x1.f57d7734d1656e0aceb716f614c2p-13L,
T21 = 0x1.967e18afcb180ed942dfdc518d6cp-14L,
T23 = 0x1.497d8eea21e95bc7e2aa79b9f2cdp-15L,
T25 = 0x1.0b132d39f055c81be49eff7afd50p-16L,
T27 = 0x1.b0f72d33eff7bfa2fbc1059d90b6p-18L,
T29 = 0x1.5ef2daf21d1113df38d0fbc00267p-19L,
T31 = 0x1.1c77d6eac0234988cdaa04c96626p-20L,
T33 = 0x1.cd2a5a292b180e0bdd701057dfe3p-22L,
T35 = 0x1.75c7357d0298c01a31d0a6f7d518p-23L,
T37 = 0x1.2f3190f4718a9a520f98f50081fcp-24L,
pio4 = 0x1.921fb54442d18469898cc51701b8p-1L,
pio4lo = 0x1.cd129024e088a67cc74020bbea60p-116L;
static const double
T39 =  0.000000028443389121318352,
T41 =  0.000000011981013102001973,
T43 =  0.0000000038303578044958070,
T45 =  0.0000000034664378216909893,
T47 = -0.0000000015090641701997785,
T49 =  0.0000000029449552300483952,
T51 = -0.0000000022006995706097711,
T53 =  0.0000000015468200913196612,
T55 = -0.00000000061311613386849674,
T57 =  1.4912469681508012e-10;
#define RPOLY(w) (T5 + w * (T9 + w * (T13 + w * (T17 + w * (T21 + \
	w * (T25 + w * (T29 + w * (T33 + w * (T37 + w * (T41 + \
	w * (T45 + w * (T49 + w * (T53 + w * T57)))))))))))))
#define VPOLY(w) (T7 + w * (T11 + w * (T15 + w * (T19 + w * (T23 + \
	w * (T27 + w * (T31 + w * (T35 + w * (T39 + w * (T43 + \
	w * (T47 + w * (T51 + w * T55))))))))))))
#endif

long double __tanl(long double x, long double y, int odd) {
	long double z, r, v, w, s, a, t;
	int big, sign;

	big = fabsl(x) >= 0.67434;
	if (big) {
		sign = 0;
		if (x < 0) {
			sign = 1;
			x = -x;
			y = -y;
		}
		x = (pio4 - x) + (pio4lo - y);
		y = 0.0;
	}
	z = x * x;
	w = z * z;
	r = RPOLY(w);
	v = z * VPOLY(w);
	s = z * x;
	r = y + z * (s * (r + v) + y) + T3 * s;
	w = x + r;
	if (big) {
		s = 1 - 2*odd;
		v = s - 2.0 * (x + (r - w * w / (w + s)));
		return sign ? -v : v;
	}
	if (!odd)
		return w;

	z = w;
	z = z + 0x1p32 - 0x1p32;
	v = r - (z - x);
	t = a = -1.0 / w;
	t = t + 0x1p32 - 0x1p32;
	s = 1.0 + t * z;
	return t + a * (s + t * v);
}
#endif
