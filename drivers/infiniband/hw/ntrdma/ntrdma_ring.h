/*
 * Copyright (c) 2014, 2015 EMC Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef NTRDMA_RING_H
#define NTRDMA_RING_H

#include "linux/types.h"

/* Initialize a range of indices for production */
static inline void ntrdma_ring_produce(u32 prod, u32 cons, u32 cap,
				       u32 *prod_idx, u32 *prod_end,
				       u32 *base)
{
	if (prod < cap) {
		*base = 0;
		*prod_idx = prod;
		if (cons < cap)
			*prod_end = cap;
		else
			*prod_end = cons - cap;
	} else {
		*base = cap;
		*prod_idx = prod - cap;
		if (cons < cap)
			*prod_end = cons;
		else
			*prod_end = cap;
	}

	WARN((prod >= (u32)(cap << 1)) ||
			(cons >= (u32)(cap << 1)),
			"prod %u and cons %u must be < cap %u",
			prod, cons, cap);
	WARN(((int)prod - (int)cons > (int)cap) ||
			(((int)prod - (int)cons < 0) && ((int)cons - (int)prod < (int)cap)),
			"the distance between prod %u and cons %u must be < cap %u",
			prod, cons, cap);
}

/* Initialize a range of indices for consumption */
static inline void ntrdma_ring_consume(u32 prod, u32 cons, u32 cap,
				       u32 *cons_idx, u32 *cons_end,
				       u32 *base)
{
	if (cons < cap) {
		*base = 0;
		*cons_idx = cons;
		if (prod < cap)
			*cons_end = prod;
		else
			*cons_end = cap;
	} else {
		*base = cap;
		*cons_idx = cons - cap;
		if (prod < cap)
			*cons_end = cap;
		else
			*cons_end = prod - cap;
	}
}

/* Compute a new index after producing or consuming */
static inline u32 ntrdma_ring_update(u32 idx, u32 base, u32 cap)
{
	if (idx == cap)
		return cap - base;

	return base + idx;
}

/* Calculate the number of entries produced */
static inline u32 ntrdma_ring_count(u32 prod, u32 cons, u32 cap)
{
	if (prod < cons)
		prod += cap << 1;

	return prod - cons;
}

/* Calculate the number of entries available */
static inline u32 ntrdma_ring_space(u32 prod, u32 cons, u32 cap)
{
	if (prod < cons)
		prod += cap << 1;

	return cap + cons - prod;
}

/* Calculate the number of contiguous entries produced */
static inline u32 ntrdma_ring_count_ctg(u32 prod, u32 cons, u32 cap)
{
	int idx, end, base;

	ntrdma_ring_consume(prod, cons, cap, &idx, &end, &base);

	return end - idx;
}

/* Calculate the number of contiguous entries available */
static inline u32 ntrdma_ring_space_ctg(u32 prod, u32 cons, u32 cap)
{
	int idx, end, base;

	ntrdma_ring_produce(prod, cons, cap, &idx, &end, &base);

	return end - idx;
}

/* The ring is valid if prod is no more than cap entries ahead of cons */
static inline bool ntrdma_ring_valid(u32 prod, u32 cons, u32 cap)
{
	if ((cap << 1) <= prod)
		return 0;

	if ((cap << 1) <= cons)
		return 0;

	return ntrdma_ring_count(prod, cons, cap) <= cap;
}

#endif
