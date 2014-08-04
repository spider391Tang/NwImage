/*
******************************************************************************

   Bandwidth estimation.

   Copyright (C) 2007, 2008 Peter Rosin  [peda@lysator.liu.se]

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

******************************************************************************
*/

#include "config.h"

#include "vnc.h"
#include "vnc-debug.h"

struct bw_table {
	int bandwidth;
	const int32_t *encoding;
	int encoding_count;
};

static const int32_t low_bw_enc[] = {
	1,	/* copyrect */
#ifdef HAVE_ZLIB
	7,	/* tight */
	16,	/* zrle */
	8,	/* zlibhex */
	6,	/* zlib */
#endif
	5,	/* hextile */
	2,	/* rre */
	4,	/* corre */
	0,	/* raw */
#ifdef HAVE_ZLIB
#ifdef HAVE_JPEG
	-32,	/* tight quality 0 */
#endif
	-247,   /* compress 9 */
#endif
#if defined(HAVE_ZLIB) && defined(HAVE_JPEG)
	-23,	/* tight quality 9 */
#endif
	-223,	/* desksize */
	-224,	/* lastrect */
#if defined(HAVE_WMH)
	-307,	/* deskname */
#endif
	0x574d5669 /* wmvi */
};

static const int32_t mid_bw_enc[] = {
	1,	/* copyrect */
#ifdef HAVE_ZLIB
	16,	/* zrle */
	7,	/* tight */
	8,	/* zlibhex */
	6,	/* zlib */
#endif
	5,	/* hextile */
	2,	/* rre */
	4,	/* corre */
	0,	/* raw */
#ifdef HAVE_ZLIB
#ifdef HAVE_JPEG
	-25,	/* tight quality 7 */
#endif
	-247,   /* compress 9 */
#endif
#if defined(HAVE_ZLIB) && defined(HAVE_JPEG)
	-23,	/* tight quality 9 */
#endif
	-223,	/* desksize */
	-224,	/* lastrect */
#if defined(HAVE_WMH)
	-307,	/* deskname */
#endif
	0x574d5669 /* wmvi */
};

static const int32_t high_bw_enc[] = {
	1,	/* copyrect */
	5,	/* hextile */
	2,	/* rre */
	4,	/* corre */
#ifdef HAVE_ZLIB
	16,	/* zrle */
	7,	/* tight */
#endif
	0,	/* raw */
#ifdef HAVE_ZLIB
#ifdef HAVE_JPEG
	-23,	/* tight quality 9 */
#endif
	-255,   /* compress 1 */
#endif
	-223,	/* desksize */
	-224,	/* lastrect */
#if defined(HAVE_WMH)
	-307,	/* deskname */
#endif
	0x574d5669 /* wmvi */
};

#define COUNTOF(x) (int)(sizeof(x) / sizeof(x[0]))

/* The following bandwidth boundaries are the result of hand waving... */
struct bw_table bw_table[] = {
	{ 10000,  low_bw_enc,  COUNTOF(low_bw_enc) },
	{ 100000, mid_bw_enc,  COUNTOF(mid_bw_enc) },
	{ 0,      high_bw_enc, COUNTOF(high_bw_enc) }
};

static inline void
tv_diff(struct timeval *tv2, struct timeval *tv1)
{
	tv2->tv_sec -= tv1->tv_sec;
	if (tv2->tv_usec < tv1->tv_usec) {
		--tv2->tv_sec;
		tv2->tv_usec += 1000000 - tv1->tv_usec;
	}
	else
		tv2->tv_usec -= tv1->tv_usec;
}

static int
match_bw(int bw)
{
	int i;

	for (i = 0; i < COUNTOF(bw_table); ++i) {
		if (!bw_table[i].bandwidth)
			break;
		if (bw < bw_table[i].bandwidth)
			break;
	}

	return i;
}

void
bandwidth_start(ssize_t len)
{
	ggCurTime(&g.bw.start);
	g.bw.count += len;
}

void
bandwidth_update(ssize_t len)
{
	g.bw.count += len;
}

void
bandwidth_end(void)
{
	struct timeval tv;
	double t;
	int idx;

	ggCurTime(&tv);
	tv_diff(&tv, &g.bw.start);
	t = tv.tv_sec + tv.tv_usec / 1000000.0;

	if (g.bw.count > 20000 ||
		g.history.sample[g.history.index].count + g.bw.count > 20000)
	{
		g.history.index =
			(g.history.index + 1) % COUNTOF(g.history.sample);
		g.history.total.count -=
			g.history.sample[g.history.index].count;
		g.history.total.interval -=
			g.history.sample[g.history.index].interval;
		g.history.sample[g.history.index].count = 0.0;
		g.history.sample[g.history.index].interval = 0.0;
	}

	g.history.sample[g.history.index].count += g.bw.count;
	g.history.sample[g.history.index].interval += t;
	g.history.total.count += g.bw.count;
	g.history.total.interval += t;

	g.bw.estimate = g.history.total.count / g.history.total.interval;
	idx = match_bw(g.bw.estimate);

	debug(2, "%d Bps (last %.0f Bps, bytes %d, time %.3f)\n",
		g.bw.estimate, g.bw.count / t, g.bw.count, t);

	if (g.encoding != bw_table[idx].encoding) {
		g.encoding = bw_table[idx].encoding;
		g.encoding_count = bw_table[idx].encoding_count;
		vnc_set_encodings();
	}
}
