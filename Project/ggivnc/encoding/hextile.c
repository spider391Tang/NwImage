/*
******************************************************************************

   VNC viewer Hextile encoding.

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

struct hextile {
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

static int vnc_hextile_8(struct connection *cx);
static int vnc_hextile_16(struct connection *cx);
static int vnc_hextile_32(struct connection *cx);

static int
hextile_stem_change(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;

	hextile->stem = cx->wire_stem ? cx->wire_stem : cx->stem;
	return 0;
}

static inline int
tile_header_complete(struct connection *cx, int bpp)
{
	uint8_t subencoding;
	int extra;

	if (cx->input.wpos < cx->input.rpos + 1)
		return 0;

	subencoding = cx->input.data[cx->input.rpos];

	if (subencoding & 1)
		return 1;

	extra = 1;

	if (subencoding & 2)
		extra += bpp;

	if (subencoding & 4)
		extra += bpp;

	if (subencoding & 8)
		++extra;

	if (cx->input.wpos < cx->input.rpos + extra)
		return 0;

	return 1;
}

int
vnc_hextile_size(struct connection *cx, int bpp)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;
	uint8_t subencoding;
	int header;
	int subrects;
	int rect_size;

	if (!tile_header_complete(cx, bpp))
		return 0;

	header = 0;
	subencoding = cx->input.data[cx->input.rpos + header++];

	if (subencoding & 1)
		return header + bpp * hextile->w * hextile->h;

	if (subencoding & 2)
		header += bpp;

	if (subencoding & 4)
		header += bpp;

	rect_size = 2;
	if (subencoding & 8)
		subrects = cx->input.data[cx->input.rpos + header++];
	else
		subrects = 0;

	if (subencoding & 16)
		rect_size += bpp;

	return header + rect_size * subrects;
}

static int
vnc_hextile_done(struct connection *cx)
{
	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_hextile_next(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;

	hextile->x += 16;
	if (hextile->x < cx->x + cx->w) {
		if (cx->x + cx->w - hextile->x < 16)
			hextile->w = cx->x + cx->w - hextile->x;
		else
			hextile->w = 16;
		return 1;
	}

	hextile->x = cx->x;
	if (cx->w < 16)
		hextile->w = cx->w;
	else
		hextile->w = 16;
	hextile->y += 16;
	if (hextile->y >= cx->y + cx->h)
		return 0;
	if (cx->y + cx->h - hextile->y < 16)
		hextile->h = cx->y + cx->h - hextile->y;
	else
		hextile->h = 16;
	return 1;
}

static int
vnc_hextile_raw_8(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;
	int bytes = hextile->w * hextile->h;

	if (cx->input.wpos < cx->input.rpos + bytes) {
		cx->action = vnc_hextile_raw_8;
		return 0;
	}

	ggiPutBox(hextile->stem,
		hextile->x, hextile->y, hextile->w, hextile->h,
		cx->input.data + cx->input.rpos);
	cx->input.rpos += bytes;

	if (cx->action == vnc_hextile_raw_8) {
		if (!vnc_hextile_next(cx))
			return vnc_hextile_done(cx);
	}
	cx->action = vnc_hextile_8;
	return 1;
}

static int
vnc_hextile_subrects_8(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;
	int x, y, w, h;
	ggi_pixel pixel;
	int size = 2;

	if (hextile->subencoding & 16)
		++size;

	while (hextile->rects) {
		if (cx->input.wpos < cx->input.rpos + size) {
			cx->action = vnc_hextile_subrects_8;
			return 0;
		}

		if (hextile->subencoding & 16) {
			pixel = cx->input.data[cx->input.rpos++];
			ggiSetGCForeground(hextile->stem, pixel);
		}

		x =  cx->input.data[cx->input.rpos] >> 4;
		y =  cx->input.data[cx->input.rpos++] & 0xf;
		w = (cx->input.data[cx->input.rpos] >> 4) + 1;
		h = (cx->input.data[cx->input.rpos++] & 0xf) + 1;

		ggiDrawBox(hextile->stem,
			hextile->x + x, hextile->y + y, w, h);

		--hextile->rects;
	}

	/* Restore fg for coming tiles, does not seem to
	 * happen for the RealVNC codebase, but it is correct
	 * to do so, at least the way I read the spec.
	 */
	ggiSetGCForeground(hextile->stem, hextile->fg);

	if (cx->action == vnc_hextile_subrects_8) {
		if (!vnc_hextile_next(cx))
			return vnc_hextile_done(cx);
	}

	cx->action = vnc_hextile_8;
	return 1;
}

static int
vnc_hextile_8(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;

	do {
		if (!tile_header_complete(cx, 1)) {
			if (cx->action == vnc_hextile)
				cx->action = vnc_hextile_8;
			return 0;
		}
		hextile->subencoding = cx->input.data[cx->input.rpos++];
		if (hextile->subencoding & 1) {
			if (!vnc_hextile_raw_8(cx))
				return 0;
			continue;
		}
		if (hextile->subencoding & 2)
			hextile->bg = cx->input.data[cx->input.rpos++];
		ggiSetGCForeground(hextile->stem, hextile->bg);
		ggiDrawBox(hextile->stem,
			hextile->x, hextile->y,
			hextile->w, hextile->h);
		if (hextile->subencoding & 4)
			hextile->fg = cx->input.data[cx->input.rpos++];
		ggiSetGCForeground(hextile->stem, hextile->fg);
		if (hextile->subencoding & 8) {
			hextile->rects = cx->input.data[cx->input.rpos++];
			if (!vnc_hextile_subrects_8(cx))
				return 0;
			continue;
		}
	} while (vnc_hextile_next(cx));

	vnc_hextile_done(cx);
	return 1;
}

static int
vnc_hextile_raw_16(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;
	int bytes = 2 * hextile->w * hextile->h;
	uint16_t buf[256];

	if (cx->input.wpos < cx->input.rpos + bytes) {
		cx->action = vnc_hextile_raw_16;
		return 0;
	}

	memcpy(buf, cx->input.data + cx->input.rpos, bytes);
	if (cx->wire_endian != cx->local_endian)
		buffer_reverse_16((uint8_t *)buf, bytes);
	ggiPutBox(hextile->stem,
		hextile->x, hextile->y, hextile->w, hextile->h, buf);
	cx->input.rpos += bytes;

	if (cx->action == vnc_hextile_raw_16) {
		if (!vnc_hextile_next(cx))
			return vnc_hextile_done(cx);
	}
	cx->action = vnc_hextile_16;
	return 1;
}

static int
vnc_hextile_subrects_16(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;
	int x, y, w, h;
	ggi_pixel pixel;
	int size = 2;

	if (hextile->subencoding & 16)
		size += 2;

	while (hextile->rects) {
		if (cx->input.wpos < cx->input.rpos + size) {
			cx->action = vnc_hextile_subrects_16;
			return 0;
		}

		if (hextile->subencoding & 16) {
			if (cx->wire_endian != cx->local_endian)
				pixel = get16_r(
					&cx->input.data[cx->input.rpos]);
			else
				pixel = get16(
					&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
			ggiSetGCForeground(hextile->stem, pixel);
		}

		x =  cx->input.data[cx->input.rpos] >> 4;
		y =  cx->input.data[cx->input.rpos++] & 0xf;
		w = (cx->input.data[cx->input.rpos] >> 4) + 1;
		h = (cx->input.data[cx->input.rpos++] & 0xf) + 1;

		ggiDrawBox(hextile->stem,
			hextile->x + x, hextile->y + y, w, h);

		--hextile->rects;
	}

	/* Restore fg for coming tiles, does not seem to
	 * happen for the RealVNC codebase, but it is correct
	 * to do so, at least the way I read the spec.
	 */
	ggiSetGCForeground(hextile->stem, hextile->fg);

	if (cx->action == vnc_hextile_subrects_16) {
		if (!vnc_hextile_next(cx))
			return vnc_hextile_done(cx);
	}

	cx->action = vnc_hextile_16;
	return 1;
}

static int
vnc_hextile_16(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;

	do {
		if (!tile_header_complete(cx, 2)) {
			if (cx->action == vnc_hextile)
				cx->action = vnc_hextile_16;
			return 0;
		}
		hextile->subencoding = cx->input.data[cx->input.rpos++];
		if (hextile->subencoding & 1) {
			if (!vnc_hextile_raw_16(cx))
				return 0;
			continue;
		}
		if (hextile->subencoding & 2) {
			if (cx->wire_endian != cx->local_endian)
				hextile->bg = get16_r(
					&cx->input.data[cx->input.rpos]);
			else
				hextile->bg = get16(
					&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
		}
		ggiSetGCForeground(hextile->stem, hextile->bg);
		ggiDrawBox(hextile->stem,
			hextile->x, hextile->y,
			hextile->w, hextile->h);
		if (hextile->subencoding & 4) {
			if (cx->wire_endian != cx->local_endian)
				hextile->fg = get16_r(
					&cx->input.data[cx->input.rpos]);
			else
				hextile->fg = get16(
					&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
		}
		ggiSetGCForeground(hextile->stem, hextile->fg);
		if (hextile->subencoding & 8) {
			hextile->rects = cx->input.data[cx->input.rpos++];
			if (!vnc_hextile_subrects_16(cx))
				return 0;
			continue;
		}
	} while (vnc_hextile_next(cx));

	vnc_hextile_done(cx);
	return 1;
}

static int
vnc_hextile_raw_32(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;
	int bytes = 4 * hextile->w * hextile->h;
	uint32_t buf[256];

	if (cx->input.wpos < cx->input.rpos + bytes) {
		cx->action = vnc_hextile_raw_32;
		return 0;
	}

	memcpy(buf, cx->input.data + cx->input.rpos, bytes);
	if (cx->wire_endian != cx->local_endian)
		buffer_reverse_32((uint8_t *)buf, bytes);
	ggiPutBox(hextile->stem,
		hextile->x, hextile->y, hextile->w, hextile->h,	buf);
	cx->input.rpos += bytes;

	if (cx->action == vnc_hextile_raw_32) {
		if (!vnc_hextile_next(cx))
			return vnc_hextile_done(cx);
	}
	cx->action = vnc_hextile_32;
	return 1;
}

static int
vnc_hextile_subrects_32(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;
	int x, y, w, h;
	ggi_pixel pixel;
	int size = 2;

	if (hextile->subencoding & 16)
		size += 4;

	while (hextile->rects) {
		if (cx->input.wpos < cx->input.rpos + size) {
			cx->action = vnc_hextile_subrects_32;
			return 0;
		}

		if (hextile->subencoding & 16) {
			if (cx->wire_endian != cx->local_endian)
				pixel = get32_r(
					&cx->input.data[cx->input.rpos]);
			else
				pixel = get32(
					&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
			ggiSetGCForeground(hextile->stem, pixel);
		}

		x =  cx->input.data[cx->input.rpos] >> 4;
		y =  cx->input.data[cx->input.rpos++] & 0xf;
		w = (cx->input.data[cx->input.rpos] >> 4) + 1;
		h = (cx->input.data[cx->input.rpos++] & 0xf) + 1;

		ggiDrawBox(hextile->stem,
			hextile->x + x, hextile->y + y, w, h);

		--hextile->rects;
	}

	/* Restore fg for coming tiles, does not seem to
	 * happen for the RealVNC codebase, but it is correct
	 * to do so, at least the way I read the spec.
	 */
	ggiSetGCForeground(hextile->stem, hextile->fg);

	if (cx->action == vnc_hextile_subrects_32) {
		if (!vnc_hextile_next(cx))
			return vnc_hextile_done(cx);
	}

	cx->action = vnc_hextile_32;
	return 1;
}

static int
vnc_hextile_32(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;

	do {
		if (!tile_header_complete(cx, 4)) {
			if (cx->action == vnc_hextile)
				cx->action = vnc_hextile_32;
			return 0;
		}
		hextile->subencoding = cx->input.data[cx->input.rpos++];
		if (hextile->subencoding & 1) {
			if (!vnc_hextile_raw_32(cx))
				return 0;
			continue;
		}
		if (hextile->subencoding & 2) {
			if (cx->wire_endian != cx->local_endian)
				hextile->bg = get32_r(
					&cx->input.data[cx->input.rpos]);
			else
				hextile->bg = get32(
					&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
		}
		ggiSetGCForeground(hextile->stem, hextile->bg);
		ggiDrawBox(hextile->stem,
			hextile->x, hextile->y,
			hextile->w, hextile->h);
		if (hextile->subencoding & 4) {
			if (cx->wire_endian != cx->local_endian)
				hextile->fg = get32_r(
					&cx->input.data[cx->input.rpos]);
			else
				hextile->fg = get32(
					&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
		}
		ggiSetGCForeground(hextile->stem, hextile->fg);
		if (hextile->subencoding & 8) {
			hextile->rects = cx->input.data[cx->input.rpos++];
			if (!vnc_hextile_subrects_32(cx))
				return 0;
			continue;
		}
	} while (vnc_hextile_next(cx));

	vnc_hextile_done(cx);
	return 1;
}

static int
hextile_rect(struct connection *cx)
{
	struct hextile *hextile = cx->encoding_def[hextile_encoding].priv;

	debug(2, "hextile\n");

	hextile->x = cx->x + cx->w;
	hextile->y = cx->y - 16;
	vnc_hextile_next(cx);

	cx->stem_change = hextile_stem_change;
	cx->stem_change(cx);

	cx->action = vnc_hextile;

	switch (GT_SIZE(cx->wire_mode.graphtype)) {
	case  8: return vnc_hextile_8(cx);
	case 16: return vnc_hextile_16(cx);
	case 32: return vnc_hextile_32(cx);
	}
	return 1;
}

static void
hextile_end(struct connection *cx)
{
	debug(1, "hextile_end\n");

	free(cx->encoding_def[hextile_encoding].priv);
	cx->encoding_def[hextile_encoding].priv = NULL;
	cx->encoding_def[hextile_encoding].action = vnc_hextile;
}

int
vnc_hextile(struct connection *cx)
{
	struct hextile *hextile;

	debug(1, "hextile init\n");

	hextile = malloc(sizeof(*hextile));
	if (!hextile)
		return close_connection(cx, -1);
	memset(hextile, 0, sizeof(*hextile));

	cx->encoding_def[hextile_encoding].priv = hextile;
	cx->encoding_def[hextile_encoding].end = hextile_end;

	cx->action = cx->encoding_def[hextile_encoding].action = hextile_rect;
	return cx->action(cx);
}
