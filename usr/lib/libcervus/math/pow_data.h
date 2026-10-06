#ifndef _POW_DATA_H
#define _POW_DATA_H

#include <features.h>

#define POW_LOG_TABLE_BITS 7
#define POW_LOG_POLY_ORDER 8
extern hidden const struct pow_log_data {
	double ln2hi;
	double ln2lo;
	double poly[POW_LOG_POLY_ORDER - 1];

	struct {
		double invc, pad, logc, logctail;
	} tab[1 << POW_LOG_TABLE_BITS];
} __pow_log_data;

#endif
