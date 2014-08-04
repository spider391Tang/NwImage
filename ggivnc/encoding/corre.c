/*
******************************************************************************

   VNC viewer CoRRE encoding.

   Copyright (C) 2007 Peter Rosin  [peda@lysator.liu.se]

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

#include <stdio.h>
#include <ggi/ggi.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

struct corre_t {
	int32_t rects;
	ggi_visual_t stem;
};

static struct corre_t corre;

static void
corre_stem_change(void)
{
	corre.stem = g.wire_stem ? g.wire_stem : g.stem;
}

static int
vnc_corre_rect_8(void)
{
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (corre.rects) {
		if (g.input.wpos < g.input.rpos + 5) {
			g.action = vnc_corre_rect_8;
			return 0;
		}

		pixel = g.input.data[g.input.rpos++];
		x = g.input.data[g.input.rpos++];
		y = g.input.data[g.input.rpos++];
		w = g.input.data[g.input.rpos++];
		h = g.input.data[g.input.rpos++];

		ggiSetGCForeground(corre.stem, pixel);
		ggiDrawBox(corre.stem, g.x + x, g.y + y, w, h);
		--corre.rects;
	}

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
vnc_corre_8(void)
{
	ggi_pixel pixel;

	if (g.input.wpos < g.input.rpos + 1) {
		g.action = vnc_corre_8;
		return 0;
	}

	pixel = g.input.data[g.input.rpos++];

	ggiSetGCForeground(corre.stem, pixel);
	ggiDrawBox(corre.stem, g.x, g.y, g.w, g.h);

	return vnc_corre_rect_8();
}

static int
vnc_corre_rect_16(void)
{
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (corre.rects) {
		if (g.input.wpos < g.input.rpos + 6) {
			g.action = vnc_corre_rect_16;
			return 0;
		}

		if (g.wire_endian != g.local_endian)
			pixel = get16_r(&g.input.data[g.input.rpos]);
		else
			pixel = get16(&g.input.data[g.input.rpos]);
		g.input.rpos += 2;
		x = g.input.data[g.input.rpos++];
		y = g.input.data[g.input.rpos++];
		w = g.input.data[g.input.rpos++];
		h = g.input.data[g.input.rpos++];

		ggiSetGCForeground(corre.stem, pixel);
		ggiDrawBox(corre.stem, g.x + x, g.y + y, w, h);
		--corre.rects;
	}

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
vnc_corre_16(void)
{
	ggi_pixel pixel;

	if (g.input.wpos < g.input.rpos + 2) {
		g.action = vnc_corre_16;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		pixel = get16_r(&g.input.data[g.input.rpos]);
	else
		pixel = get16(&g.input.data[g.input.rpos]);
	g.input.rpos += 2;

	ggiSetGCForeground(corre.stem, pixel);
	ggiDrawBox(corre.stem, g.x, g.y, g.w, g.h);

	return vnc_corre_rect_16();
}

static int
vnc_corre_rect_32(void)
{
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (corre.rects) {
		if (g.input.wpos < g.input.rpos + 8) {
			g.action = vnc_corre_rect_32;
			return 0;
		}

		if (g.wire_endian != g.local_endian)
			pixel = get32_r(&g.input.data[g.input.rpos]);
		else
			pixel = get32(&g.input.data[g.input.rpos]);
		g.input.rpos += 4;
		x = g.input.data[g.input.rpos++];
		y = g.input.data[g.input.rpos++];
		w = g.input.data[g.input.rpos++];
		h = g.input.data[g.input.rpos++];

		ggiSetGCForeground(corre.stem, pixel);
		ggiDrawBox(corre.stem, g.x + x, g.y + y, w, h);
		--corre.rects;
	}

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
vnc_corre_32(void)
{
	ggi_pixel pixel;

	if (g.input.wpos < g.input.rpos + 4) {
		g.action = vnc_corre_32;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		pixel = get32_r(&g.input.data[g.input.rpos]);
	else
		pixel = get32(&g.input.data[g.input.rpos]);
	g.input.rpos += 4;

	ggiSetGCForeground(corre.stem, pixel);
	ggiDrawBox(corre.stem, g.x, g.y, g.w, g.h);

	return vnc_corre_rect_32();
}

int
vnc_corre(void)
{
	debug(2, "corre\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	corre.rects = get32_hilo(&g.input.data[g.input.rpos]);

	g.input.rpos += 4;

	g.stem_change = corre_stem_change;
	g.stem_change();

	switch (GT_SIZE(g.wire_mode.graphtype)) {
	case  8: return vnc_corre_8();
	case 16: return vnc_corre_16();
	case 32: return vnc_corre_32();
	}
	return 1;
}
