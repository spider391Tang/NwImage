/*
******************************************************************************

   Bandwidth estimation.

   The MIT License

   Copyright (C) 2007-2010 Peter Rosin  [peda@lysator.liu.se]

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
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

******************************************************************************
*/

#include "config.h"

#include <string.h>

#include "vnc.h"
#include "vnc-debug.h"

static int32_t low_bw_enc[] = {
	1,	/* copyrect */
#ifdef HAVE_ZLIB
	7,	/* tight */
	16,	/* zrle */
	8,	/* zlibhex */
	6,	/* zlib */
#endif
	15,	/* trle */
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
	-223,	/* desksize */
	-224,	/* lastrect */
#if defined(HAVE_WMH)
	-307,	/* deskname */
#endif
	0x574d5669 /* wmvi */
};

static int32_t mid_bw_enc[] = {
	1,	/* copyrect */
#ifdef HAVE_ZLIB
	16,	/* zrle */
	7,	/* tight */
	8,	/* zlibhex */
	6,	/* zlib */
#endif
	15,	/* trle */
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
	-223,	/* desksize */
	-224,	/* lastrect */
#if defined(HAVE_WMH)
	-307,	/* deskname */
#endif
	0x574d5669 /* wmvi */
};

static int32_t high_bw_enc[] = {
	1,	/* copyrect */
	15,	/* trle */
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
static const struct bw_table def_bw_table[] = {
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
match_bw(struct connection *cx, int bw)
{
	int i;

	for (i = 0; cx->bw_table[i].bandwidth; ++i)
		if (bw < cx->bw_table[i].bandwidth)
			break;

	return i;
}

void
bandwidth_start(struct connection *cx, ssize_t len)
{
	ggCurTime(&cx->bw.start);
	cx->bw.count += len;
}

void
bandwidth_update(struct connection *cx, ssize_t len)
{
	cx->bw.count += len;
}

int
bandwidth_end(struct connection *cx)
{
	struct timeval tv;
	double t;
	int idx;

	ggCurTime(&tv);
	tv_diff(&tv, &cx->bw.start);
	t = tv.tv_sec + tv.tv_usec / 1000000.0;

	if (cx->bw.count > 20000 ||
		cx->history.sample[cx->history.index].count + cx->bw.count > 20000)
	{
		cx->history.index =
			(cx->history.index + 1) % COUNTOF(cx->history.sample);
		cx->history.total.count -=
			cx->history.sample[cx->history.index].count;
		cx->history.total.interval -=
			cx->history.sample[cx->history.index].interval;
		cx->history.sample[cx->history.index].count = 0.0;
		cx->history.sample[cx->history.index].interval = 0.0;
	}

	cx->history.sample[cx->history.index].count += cx->bw.count;
	cx->history.sample[cx->history.index].interval += t;
	cx->history.total.count += cx->bw.count;
	cx->history.total.interval += t;

	cx->bw.estimate = cx->history.total.count / cx->history.total.interval;
	idx = match_bw(cx, cx->bw.estimate);

	debug(2, "%d Bps (last %.0f Bps, bytes %d, time %.3f)\n",
		cx->bw.estimate, cx->bw.count / t, cx->bw.count, t);

	if (cx->bw.idx != idx + 1) {
		cx->encoding_count = cx->bw_table[idx].count;
		cx->encoding = cx->bw_table[idx].encoding;
		cx->bw.idx = idx + 1;
		return vnc_set_encodings(cx);
	}

	return 0;
}

int
bandwidth_init(struct connection *cx)
{
	int i = 0;

	cx->bw_table = malloc(sizeof(def_bw_table));
	if (!cx->bw_table)
		return -1;
	memset(cx->bw_table, 0, sizeof(def_bw_table));

	do {
		int j;
		cx->bw_table[i].encoding = malloc(
			def_bw_table[i].count *
				sizeof(*cx->bw_table[i].encoding));
		if (!cx->bw_table[i].encoding)
			goto err;

		cx->bw_table[i].bandwidth = def_bw_table[i].bandwidth;
		for (j = 0; j < def_bw_table[i].count; ++j) {
			int32_t encoding = def_bw_table[i].encoding[j];
			int k;
			for (k = 0; k < cx->allowed_encodings; ++k) {
				if (encoding == cx->allow_encoding[k])
					break;
				if (-32 <= encoding && encoding <= -23 &&
					-32 <= cx->allow_encoding[k] &&
					cx->allow_encoding[k] <= -23)
				{
					if (encoding < cx->allow_encoding[k])
						encoding =
							cx->allow_encoding[k];
					break;
				}
				if (-256 <= encoding && encoding <= -247 &&
					-256 <= cx->allow_encoding[k] &&
					cx->allow_encoding[k] <= -247)
				{
					break;
				}
			}

			if (k >= cx->allowed_encodings)
				continue;

			cx->bw_table[i].encoding[cx->bw_table[i].count++] =
				encoding;
		}
	} while(def_bw_table[i++].bandwidth);

	return 0;

err:
	while (i)
		free(cx->bw_table[--i].encoding);
	free(cx->bw_table);
	cx->bw_table = NULL;
	return -1;
}

void
bandwidth_fini(struct connection *cx)
{
	int i = 0;

	if (!cx->bw_table)
		return;

	do
		free(cx->bw_table[i].encoding);
	while(cx->bw_table[i++].bandwidth);

	free(cx->bw_table);
	cx->bw_table = NULL;
}
