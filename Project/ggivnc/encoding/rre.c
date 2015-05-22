/*
******************************************************************************

   VNC viewer RRE encoding.

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

#include <stdio.h>
#include <string.h>
#include <ggi/ggi.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

struct rre {
	int32_t rects;
	ggi_visual_t stem;
};

static int
rre_stem_change(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;

	rre->stem = cx->wire_stem ? cx->wire_stem : cx->stem;
	return 0;
}

static int
vnc_rre_rect_8(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (rre->rects) {
		if (cx->input.wpos < cx->input.rpos + 9) {
			cx->action = vnc_rre_rect_8;
			return 0;
		}

		pixel = cx->input.data[cx->input.rpos++];
		x = get16_hilo(&cx->input.data[cx->input.rpos + 0]);
		y = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
		w = get16_hilo(&cx->input.data[cx->input.rpos + 4]);
		h = get16_hilo(&cx->input.data[cx->input.rpos + 6]);
		cx->input.rpos += 8;

		ggiSetGCForeground(rre->stem, pixel);
		ggiDrawBox(rre->stem, cx->x + x, cx->y + y, w, h);
		--rre->rects;
	}

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_rre_8(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;
	ggi_pixel pixel;

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = vnc_rre_8;
		return 0;
	}

	pixel = cx->input.data[cx->input.rpos++];

	ggiSetGCForeground(rre->stem, pixel);
	ggiDrawBox(rre->stem, cx->x, cx->y, cx->w, cx->h);

	return vnc_rre_rect_8(cx);
}

static int
vnc_rre_rect_16(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (rre->rects) {
		if (cx->input.wpos < cx->input.rpos + 10) {
			cx->action = vnc_rre_rect_16;
			return 0;
		}

		if (cx->wire_endian != cx->local_endian)
			pixel = get16_r(&cx->input.data[cx->input.rpos]);
		else
			pixel = get16(&cx->input.data[cx->input.rpos]);
		cx->input.rpos += 2;
		x = get16_hilo(&cx->input.data[cx->input.rpos + 0]);
		y = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
		w = get16_hilo(&cx->input.data[cx->input.rpos + 4]);
		h = get16_hilo(&cx->input.data[cx->input.rpos + 6]);
		cx->input.rpos += 8;

		ggiSetGCForeground(rre->stem, pixel);
		ggiDrawBox(rre->stem, cx->x + x, cx->y + y, w, h);
		--rre->rects;
	}

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_rre_16(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;
	ggi_pixel pixel;

	if (cx->input.wpos < cx->input.rpos + 2) {
		cx->action = vnc_rre_16;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get16_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get16(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 2;

	ggiSetGCForeground(rre->stem, pixel);
	ggiDrawBox(rre->stem, cx->x, cx->y, cx->w, cx->h);

	return vnc_rre_rect_16(cx);
}

static int
vnc_rre_rect_32(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (rre->rects) {
		if (cx->input.wpos < cx->input.rpos + 12) {
			cx->action = vnc_rre_rect_32;
			return 0;
		}

		if (cx->wire_endian != cx->local_endian)
			pixel = get32_r(&cx->input.data[cx->input.rpos]);
		else
			pixel = get32(&cx->input.data[cx->input.rpos]);
		cx->input.rpos += 4;
		x = get16_hilo(&cx->input.data[cx->input.rpos + 0]);
		y = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
		w = get16_hilo(&cx->input.data[cx->input.rpos + 4]);
		h = get16_hilo(&cx->input.data[cx->input.rpos + 6]);
		cx->input.rpos += 8;

		ggiSetGCForeground(rre->stem, pixel);
		ggiDrawBox(rre->stem, cx->x + x, cx->y + y, w, h);
		--rre->rects;
	}

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_rre_32(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;
	ggi_pixel pixel;

	if (cx->input.wpos < cx->input.rpos + 4) {
		cx->action = vnc_rre_32;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get32_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get32(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 4;

	debug(3, "rre rects %d bg=%08x\n", rre->rects, pixel);

	ggiSetGCForeground(rre->stem, pixel);
	ggiDrawBox(rre->stem, cx->x, cx->y, cx->w, cx->h);

	return vnc_rre_rect_32(cx);
}

static int
rre_rect(struct connection *cx)
{
	struct rre *rre = cx->encoding_def[rre_encoding].priv;

	debug(2, "rre\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	rre->rects = get32_hilo(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 4;

	cx->stem_change = rre_stem_change;
	cx->stem_change(cx);

	switch (GT_SIZE(cx->wire_mode.graphtype) / 8) {
	case 1: return vnc_rre_8(cx);
	case 2: return vnc_rre_16(cx);
	case 4: return vnc_rre_32(cx);
	}
	return 1;
}

static void
rre_end(struct connection *cx)
{
	if (!cx->encoding_def[rre_encoding].priv)
		return;

	debug(1, "rre_end\n");

	free(cx->encoding_def[rre_encoding].priv);
	cx->encoding_def[rre_encoding].priv = NULL;
	cx->encoding_def[rre_encoding].action = vnc_rre;
}

int
vnc_rre(struct connection *cx)
{
	struct rre *rre;

	debug(1, "rre init\n");

	rre = malloc(sizeof(*rre));
	if (!rre)
		return close_connection(cx, -1);
	memset(rre, 0, sizeof(*rre));

	cx->encoding_def[rre_encoding].priv = rre;
	cx->encoding_def[rre_encoding].end = rre_end;

	cx->action = cx->encoding_def[rre_encoding].action = rre_rect;
	return cx->action(cx);
}
