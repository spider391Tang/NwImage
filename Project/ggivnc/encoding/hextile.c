/*
******************************************************************************

   VNC viewer Hextile encoding.

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
#include <string.h>
#include <ggi/ggi.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

struct hextile_t {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	ggi_visual_t stem;
	uint8_t subencoding;
	ggi_pixel bg;
	ggi_pixel fg;
	int rects;
};

static struct hextile_t hextile;

static int vnc_hextile_8(void);
static int vnc_hextile_16(void);
static int vnc_hextile_32(void);

static void
hextile_stem_change(void)
{
	hextile.stem = g.wire_stem ? g.wire_stem : g.stem;
}

static inline int
tile_header_complete(int bpp)
{
	uint8_t subencoding;
	int extra;

	if (g.input.wpos < g.input.rpos + 1)
		return 0;

	subencoding = g.input.data[g.input.rpos];

	if (subencoding & 1)
		return 1;

	extra = 1;

	if (subencoding & 2)
		extra += bpp;

	if (subencoding & 4)
		extra += bpp;

	if (subencoding & 8)
		++extra;

	if (g.input.wpos < g.input.rpos + extra)
		return 0;

	return 1;
}

int
vnc_hextile_size(int bpp)
{
	uint8_t subencoding;
	int header;
	int subrects;
	int rect_size;

	if (!tile_header_complete(bpp))
		return 0;

	header = 0;
	subencoding = g.input.data[g.input.rpos + header++];

	if (subencoding & 1)
		return header + bpp * hextile.w * hextile.h;

	if (subencoding & 2)
		header += bpp;

	if (subencoding & 4)
		header += bpp;

	rect_size = 2;
	if (subencoding & 8)
		subrects = g.input.data[g.input.rpos + header++];
	else
		subrects = 0;

	if (subencoding & 16)
		rect_size += bpp;

	return header + rect_size * subrects;
}

static int
vnc_hextile_done(void)
{
	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
vnc_hextile_next(void)
{
	hextile.x += 16;
	if (hextile.x < g.x + g.w) {
		if (g.x + g.w - hextile.x < 16)
			hextile.w = g.x + g.w - hextile.x;
		else
			hextile.w = 16;
		return 1;
	}

	hextile.x = g.x;
	if (g.w < 16)
		hextile.w = g.w;
	else
		hextile.w = 16;
	hextile.y += 16;
	if (hextile.y >= g.y + g.h)
		return 0;
	if (g.y + g.h - hextile.y < 16)
		hextile.h = g.y + g.h - hextile.y;
	else
		hextile.h = 16;
	return 1;
}

static int
vnc_hextile_raw_8(void)
{
	int bytes = hextile.w * hextile.h;

	if (g.input.wpos < g.input.rpos + bytes) {
		g.action = vnc_hextile_raw_8;
		return 0;
	}

	ggiPutBox(hextile.stem, hextile.x, hextile.y, hextile.w, hextile.h,
		g.input.data + g.input.rpos);
	g.input.rpos += bytes;

	if (g.action == vnc_hextile_raw_8) {
		if (!vnc_hextile_next())
			return vnc_hextile_done();
	}
	g.action = vnc_hextile_8;
	return 1;
}

static int
vnc_hextile_subrects_8(void)
{
	int x, y, w, h;
	ggi_pixel pixel;
	int size = 2;

	if (hextile.subencoding & 16)
		++size;

	while (hextile.rects) {
		if (g.input.wpos < g.input.rpos + size) {
			g.action = vnc_hextile_subrects_8;
			return 0;
		}

		if (hextile.subencoding & 16) {
			pixel = g.input.data[g.input.rpos++];
			ggiSetGCForeground(hextile.stem, pixel);
		}

		x =  g.input.data[g.input.rpos] >> 4;
		y =  g.input.data[g.input.rpos++] & 0xf;
		w = (g.input.data[g.input.rpos] >> 4) + 1;
		h = (g.input.data[g.input.rpos++] & 0xf) + 1;

		ggiDrawBox(hextile.stem,
			hextile.x + x, hextile.y + y, w, h);

		--hextile.rects;
	}

	/* Restore fg for coming tiles, does not seem to
	 * happen for the RealVNC codebase, but it is correct
	 * to do so, at least the way I read the spec.
	 */
	ggiSetGCForeground(hextile.stem, hextile.fg);

	if (g.action == vnc_hextile_subrects_8) {
		if (!vnc_hextile_next())
			return vnc_hextile_done();
	}

	g.action = vnc_hextile_8;
	return 1;
}

static int
vnc_hextile_8(void)
{
	do {
		if (!tile_header_complete(1)) {
			if (g.action == vnc_hextile)
				g.action = vnc_hextile_8;
			return 0;
		}
		hextile.subencoding = g.input.data[g.input.rpos++];
		if (hextile.subencoding & 1) {
			if (!vnc_hextile_raw_8())
				return 0;
			continue;
		}
		if (hextile.subencoding & 2)
			hextile.bg = g.input.data[g.input.rpos++];
		ggiSetGCForeground(hextile.stem, hextile.bg);
		ggiDrawBox(hextile.stem,
			hextile.x, hextile.y,
			hextile.w, hextile.h);
		if (hextile.subencoding & 4)
			hextile.fg = g.input.data[g.input.rpos++];
		ggiSetGCForeground(hextile.stem, hextile.fg);
		if (hextile.subencoding & 8) {
			hextile.rects = g.input.data[g.input.rpos++];
			if (!vnc_hextile_subrects_8())
				return 0;
			continue;
		}
	} while (vnc_hextile_next());

	vnc_hextile_done();
	return 1;
}

static int
vnc_hextile_raw_16(void)
{
	int bytes = 2 * hextile.w * hextile.h;
	uint16_t buf[256];

	if (g.input.wpos < g.input.rpos + bytes) {
		g.action = vnc_hextile_raw_16;
		return 0;
	}

	memcpy(buf, g.input.data + g.input.rpos, bytes);
	if (g.wire_endian != g.local_endian)
		buffer_reverse_16((uint8_t *)buf, bytes);
	ggiPutBox(hextile.stem, hextile.x, hextile.y, hextile.w, hextile.h,
		buf);
	g.input.rpos += bytes;

	if (g.action == vnc_hextile_raw_16) {
		if (!vnc_hextile_next())
			return vnc_hextile_done();
	}
	g.action = vnc_hextile_16;
	return 1;
}

static int
vnc_hextile_subrects_16(void)
{
	int x, y, w, h;
	ggi_pixel pixel;
	int size = 2;

	if (hextile.subencoding & 16)
		size += 2;

	while (hextile.rects) {
		if (g.input.wpos < g.input.rpos + size) {
			g.action = vnc_hextile_subrects_16;
			return 0;
		}

		if (hextile.subencoding & 16) {
			if (g.wire_endian != g.local_endian)
				pixel = get16_r(&g.input.data[g.input.rpos]);
			else
				pixel = get16(&g.input.data[g.input.rpos]);
			g.input.rpos += 2;
			ggiSetGCForeground(hextile.stem, pixel);
		}

		x =  g.input.data[g.input.rpos] >> 4;
		y =  g.input.data[g.input.rpos++] & 0xf;
		w = (g.input.data[g.input.rpos] >> 4) + 1;
		h = (g.input.data[g.input.rpos++] & 0xf) + 1;

		ggiDrawBox(hextile.stem,
			hextile.x + x, hextile.y + y, w, h);

		--hextile.rects;
	}

	/* Restore fg for coming tiles, does not seem to
	 * happen for the RealVNC codebase, but it is correct
	 * to do so, at least the way I read the spec.
	 */
	ggiSetGCForeground(hextile.stem, hextile.fg);

	if (g.action == vnc_hextile_subrects_16) {
		if (!vnc_hextile_next())
			return vnc_hextile_done();
	}

	g.action = vnc_hextile_16;
	return 1;
}

static int
vnc_hextile_16(void)
{
	do {
		if (!tile_header_complete(2)) {
			if (g.action == vnc_hextile)
				g.action = vnc_hextile_16;
			return 0;
		}
		hextile.subencoding = g.input.data[g.input.rpos++];
		if (hextile.subencoding & 1) {
			if (!vnc_hextile_raw_16())
				return 0;
			continue;
		}
		if (hextile.subencoding & 2) {
			if (g.wire_endian != g.local_endian)
				hextile.bg =
					get16_r(&g.input.data[g.input.rpos]);
			else
				hextile.bg =
					get16(&g.input.data[g.input.rpos]);
			g.input.rpos += 2;
		}
		ggiSetGCForeground(hextile.stem, hextile.bg);
		ggiDrawBox(hextile.stem,
			hextile.x, hextile.y,
			hextile.w, hextile.h);
		if (hextile.subencoding & 4) {
			if (g.wire_endian != g.local_endian)
				hextile.fg =
					get16_r(&g.input.data[g.input.rpos]);
			else
				hextile.fg =
					get16(&g.input.data[g.input.rpos]);
			g.input.rpos += 2;
		}
		ggiSetGCForeground(hextile.stem, hextile.fg);
		if (hextile.subencoding & 8) {
			hextile.rects = g.input.data[g.input.rpos++];
			if (!vnc_hextile_subrects_16())
				return 0;
			continue;
		}
	} while (vnc_hextile_next());

	vnc_hextile_done();
	return 1;
}

static int
vnc_hextile_raw_32(void)
{
	int bytes = 4 * hextile.w * hextile.h;
	uint32_t buf[256];

	if (g.input.wpos < g.input.rpos + bytes) {
		g.action = vnc_hextile_raw_32;
		return 0;
	}

	memcpy(buf, g.input.data + g.input.rpos, bytes);
	if (g.wire_endian != g.local_endian)
		buffer_reverse_32((uint8_t *)buf, bytes);
	ggiPutBox(hextile.stem, hextile.x, hextile.y, hextile.w, hextile.h,
		buf);
	g.input.rpos += bytes;

	if (g.action == vnc_hextile_raw_32) {
		if (!vnc_hextile_next())
			return vnc_hextile_done();
	}
	g.action = vnc_hextile_32;
	return 1;
}

static int
vnc_hextile_subrects_32(void)
{
	int x, y, w, h;
	ggi_pixel pixel;
	int size = 2;

	if (hextile.subencoding & 16)
		size += 4;

	while (hextile.rects) {
		if (g.input.wpos < g.input.rpos + size) {
			g.action = vnc_hextile_subrects_32;
			return 0;
		}

		if (hextile.subencoding & 16) {
			if (g.wire_endian != g.local_endian)
				pixel = get32_r(&g.input.data[g.input.rpos]);
			else
				pixel = get32(&g.input.data[g.input.rpos]);
			g.input.rpos += 4;
			ggiSetGCForeground(hextile.stem, pixel);
		}

		x =  g.input.data[g.input.rpos] >> 4;
		y =  g.input.data[g.input.rpos++] & 0xf;
		w = (g.input.data[g.input.rpos] >> 4) + 1;
		h = (g.input.data[g.input.rpos++] & 0xf) + 1;

		ggiDrawBox(hextile.stem,
			hextile.x + x, hextile.y + y, w, h);

		--hextile.rects;
	}

	/* Restore fg for coming tiles, does not seem to
	 * happen for the RealVNC codebase, but it is correct
	 * to do so, at least the way I read the spec.
	 */
	ggiSetGCForeground(hextile.stem, hextile.fg);

	if (g.action == vnc_hextile_subrects_32) {
		if (!vnc_hextile_next())
			return vnc_hextile_done();
	}

	g.action = vnc_hextile_32;
	return 1;
}

static int
vnc_hextile_32(void)
{
	do {
		if (!tile_header_complete(4)) {
			if (g.action == vnc_hextile)
				g.action = vnc_hextile_32;
			return 0;
		}
		hextile.subencoding = g.input.data[g.input.rpos++];
		if (hextile.subencoding & 1) {
			if (!vnc_hextile_raw_32())
				return 0;
			continue;
		}
		if (hextile.subencoding & 2) {
			if (g.wire_endian != g.local_endian)
				hextile.bg =
					get32_r(&g.input.data[g.input.rpos]);
			else
				hextile.bg =
					get32(&g.input.data[g.input.rpos]);
			g.input.rpos += 4;
		}
		ggiSetGCForeground(hextile.stem, hextile.bg);
		ggiDrawBox(hextile.stem,
			hextile.x, hextile.y,
			hextile.w, hextile.h);
		if (hextile.subencoding & 4) {
			if (g.wire_endian != g.local_endian)
				hextile.fg =
					get32_r(&g.input.data[g.input.rpos]);
			else
				hextile.fg =
					get32(&g.input.data[g.input.rpos]);
			g.input.rpos += 4;
		}
		ggiSetGCForeground(hextile.stem, hextile.fg);
		if (hextile.subencoding & 8) {
			hextile.rects = g.input.data[g.input.rpos++];
			if (!vnc_hextile_subrects_32())
				return 0;
			continue;
		}
	} while (vnc_hextile_next());

	vnc_hextile_done();
	return 1;
}

int
vnc_hextile(void)
{
	debug(2, "hextile\n");

	hextile.x = g.x + g.w;
	hextile.y = g.y - 16;
	vnc_hextile_next();

	g.stem_change = hextile_stem_change;
	g.stem_change();

	g.action = vnc_hextile;

	switch (GT_SIZE(g.wire_mode.graphtype)) {
	case  8: return vnc_hextile_8();
	case 16: return vnc_hextile_16();
	case 32: return vnc_hextile_32();
	}
	return 1;
}
