/*
******************************************************************************

   VNC viewer ZRLE encoding.

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
#include <zlib.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

struct zrle_t {
	uint32_t length;
	z_stream zstr;
	struct buffer tbuf;

	ggi_coord p;
	ggi_coord s;
	ggi_visual_t stem;

	uint8_t subencoding;
	uint8_t palette_size;
	ggi_pixel palette[127];
	uint8_t *unpacked;
	action_t *action;
	int rle;
	action_t *tile;
	action_t *raw;
	action_t *solid;
	action_t *packed_palette;
	action_t *plain_rle;
	int (*parse_palette)(action_t *action);
	uint32_t (*get24)(const uint8_t *buf);
};

static struct zrle_t zrle;

static void
zrle_stem_change(void)
{
	zrle.stem = g.wire_stem ? g.wire_stem : g.stem;
}

static int zrle_tile(void);

static int
zrle_done(void)
{
	debug(3, "zrle_done\n");

	g.work.rpos = 0;
	g.work.wpos = 0;

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	zrle.action = NULL;
	return 1;
}

static int
zrle_next(void)
{
	debug(3, "zrle_next\n");

	zrle.p.x += 64;
	if (zrle.p.x < g.x + g.w) {
		if (g.x + g.w - zrle.p.x < 64)
			zrle.s.x = g.x + g.w - zrle.p.x;
		else
			zrle.s.x = 64;
		return 1;
	}

	zrle.p.x = g.x;
	if (g.w < 64)
		zrle.s.x = g.w;
	else
		zrle.s.x = 64;
	zrle.p.y += 64;
	if (zrle.p.y >= g.y + g.h)
		return 0;
	if (g.y + g.h - zrle.p.y < 64)
		zrle.s.y = g.y + g.h - zrle.p.y;
	else
		zrle.s.y = 64;
	return 1;
}

static int
zrle_drain_inflate(void)
{
	int length;
	int res;
	int flush;
	const uint32_t max_length = 0x1000;
	int chunked;
	uint8_t tmp[128];

	debug(3, "zrle_drain_inflate\n");

	chunked = zrle.length > max_length;
	length = chunked ? max_length : zrle.length;

	if (g.input.wpos < g.input.rpos + length) {
		length = g.input.wpos - g.input.rpos;
		chunked = 0;
		flush = Z_NO_FLUSH;
	}
	else
		flush = chunked ? Z_NO_FLUSH : Z_SYNC_FLUSH;

	if (!length) {
		g.action = zrle_drain_inflate;
		return 0;
	}

	zrle.zstr.avail_in = length;
	zrle.zstr.next_in = &g.input.data[g.input.rpos];
	zrle.zstr.avail_out = sizeof(tmp);
	zrle.zstr.next_out = tmp;
	res = inflate(&zrle.zstr, flush);
	switch (res) {
	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
		debug(1, "inflate result %d\n", res);
		inflateEnd(&zrle.zstr);
		exit(1);
	}

	zrle.length -= length - zrle.zstr.avail_in;
	g.input.rpos += length - zrle.zstr.avail_in;

	remove_dead_data();

	if (zrle.length) {
		g.action = zrle_drain_inflate;
		return chunked;
	}

	g.action = vnc_update_rect;
	return 1;
}

static int
zrle_raw_8(void)
{
	int bytes = zrle.s.x * zrle.s.y;

	debug(3, "zrle_raw_8\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		zrle.action = zrle.raw;
		return 0;
	}

	ggiPutBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y,
		g.work.data + g.work.rpos);
	g.work.rpos += bytes;

	if (zrle.action == zrle.raw) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_raw_16(void)
{
	int bytes = 2 * zrle.s.x * zrle.s.y;
	uint16_t *buf;

	debug(3, "zrle_raw_16\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		zrle.action = zrle.raw;
		return 0;
	}

	if (g.wire_endian != g.local_endian) {
		buf = (uint16_t *)zrle.unpacked;
		for (; bytes; bytes -= 2) {
			*buf++ = get16_r(&g.work.data[g.work.rpos]);
			g.work.rpos += 2;
		}
	}
	else {
		memcpy(zrle.unpacked, &g.work.data[g.work.rpos], bytes);
		g.work.rpos += bytes;
	}
	ggiPutBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y,
		zrle.unpacked);

	if (zrle.action == zrle.raw) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_raw_24(void)
{
	int bytes = 3 * zrle.s.x * zrle.s.y;
	uint32_t *buf;

	debug(3, "zrle_raw_24\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		zrle.action = zrle.raw;
		return 0;
	}

	buf = (uint32_t *)zrle.unpacked;
	for (; bytes; bytes -= 3) {
		*buf++ = zrle.get24(&g.work.data[g.work.rpos]);
		g.work.rpos += 3;
	}
	ggiPutBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y,
		zrle.unpacked);

	if (zrle.action == zrle.raw) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_raw_32(void)
{
	int bytes = 4 * zrle.s.x * zrle.s.y;
	uint32_t *buf;

	debug(3, "zrle_raw_32\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		zrle.action = zrle.raw;
		return 0;
	}

	if (g.wire_endian != g.local_endian) {
		buf = (uint32_t *)zrle.unpacked;
		for (; bytes; bytes -= 4) {
			*buf++ = get32_r(&g.work.data[g.work.rpos]);
			g.work.rpos += 4;
		}
	}
	else {
		memcpy(zrle.unpacked, &g.work.data[g.work.rpos], bytes);
		g.work.rpos += bytes;
	}
	ggiPutBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y,
		zrle.unpacked);

	if (zrle.action == zrle.raw) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_8(void)
{
	debug(3, "zrle_solid_8\n");

	if (g.work.wpos < g.work.rpos + 1) {
		zrle.action = zrle.solid;
		return 0;
	}

	ggiSetGCForeground(zrle.stem, g.work.data[g.work.rpos++]);
	ggiDrawBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y);

	if (zrle.action == zrle.solid) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_16(void)
{
	uint16_t pixel;
	debug(3, "zrle_solid_16\n");

	if (g.work.wpos < g.work.rpos + 2) {
		zrle.action = zrle.solid;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		pixel = get16_r(&g.work.data[g.work.rpos]);
	else
		pixel = get16(&g.work.data[g.work.rpos]);
	g.work.rpos += 2;
	ggiSetGCForeground(zrle.stem, pixel);
	ggiDrawBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y);

	if (zrle.action == zrle.solid) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_24(void)
{
	uint32_t pixel;
	debug(3, "zrle_solid_24\n");

	if (g.work.wpos < g.work.rpos + 3) {
		zrle.action = zrle.solid;
		return 0;
	}

	pixel = zrle.get24(&g.work.data[g.work.rpos]);
	g.work.rpos += 3;
	ggiSetGCForeground(zrle.stem, pixel);
	ggiDrawBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y);

	if (zrle.action == zrle.solid) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_32(void)
{
	uint32_t pixel;
	debug(3, "zrle_solid_32\n");

	if (g.work.wpos < g.work.rpos + 4) {
		zrle.action = zrle.solid;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		pixel = get32_r(&g.work.data[g.work.rpos]);
	else
		pixel = get32(&g.work.data[g.work.rpos]);
	g.work.rpos += 4;
	ggiSetGCForeground(zrle.stem, pixel);
	ggiDrawBox(zrle.stem, zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y);

	if (zrle.action == zrle.solid) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_packed_palette_8(void)
{
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint8_t *dst;

	debug(3, "zrle_packed_palette_8\n");

	if (zrle.subencoding == 2) {
		step = 1;
		extra = (zrle.s.x + 7) / 8 * zrle.s.y;
	}
	else if (zrle.subencoding <= 4) {
		step = 2;
		extra = (zrle.s.x + 3) / 4 * zrle.s.y;
	}
	else {
		step = 4;
		extra = (zrle.s.x + 1) / 2 * zrle.s.y;
	}

	if (g.work.wpos < g.work.rpos + extra) {
		zrle.action = zrle.packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &g.work.data[g.work.rpos];
	dst = zrle.unpacked;

	for (y = 0; y < zrle.s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < zrle.s.x; ++x) {
			*dst++ = zrle.palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(zrle.stem,
		zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y,
		zrle.unpacked);

	g.work.rpos += extra;

	if (zrle.action == zrle.packed_palette) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_packed_palette_16(void)
{
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint16_t *dst;

	debug(3, "zrle_packed_palette_16\n");

	if (zrle.subencoding == 2) {
		step = 1;
		extra = (zrle.s.x + 7) / 8 * zrle.s.y;
	}
	else if (zrle.subencoding <= 4) {
		step = 2;
		extra = (zrle.s.x + 3) / 4 * zrle.s.y;
	}
	else {
		step = 4;
		extra = (zrle.s.x + 1) / 2 * zrle.s.y;
	}

	if (g.work.wpos < g.work.rpos + extra) {
		zrle.action = zrle.packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &g.work.data[g.work.rpos];
	dst = (uint16_t *)zrle.unpacked;

	for (y = 0; y < zrle.s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < zrle.s.x; ++x) {
			*dst++ = zrle.palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(zrle.stem,
		zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y,
		zrle.unpacked);

	g.work.rpos += extra;

	if (zrle.action == zrle.packed_palette) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_packed_palette_32(void)
{
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint32_t *dst;

	debug(3, "zrle_packed_palette_32\n");

	if (zrle.subencoding == 2) {
		step = 1;
		extra = (zrle.s.x + 7) / 8 * zrle.s.y;
	}
	else if (zrle.subencoding <= 4) {
		step = 2;
		extra = (zrle.s.x + 3) / 4 * zrle.s.y;
	}
	else {
		step = 4;
		extra = (zrle.s.x + 1) / 2 * zrle.s.y;
	}

	if (g.work.wpos < g.work.rpos + extra) {
		zrle.action = zrle.packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &g.work.data[g.work.rpos];
	dst = (uint32_t *)zrle.unpacked;

	for (y = 0; y < zrle.s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < zrle.s.x; ++x) {
			*dst++ = zrle.palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(zrle.stem,
		zrle.p.x, zrle.p.y, zrle.s.x, zrle.s.y,
		zrle.unpacked);

	g.work.rpos += extra;

	if (zrle.action == zrle.packed_palette) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_8(void)
{
	int run_length;
	int rpos;
	int start_x;

	debug(3, "zrle_plain_rle_8\n");

	do {
		if (g.work.wpos < g.work.rpos + 2) {
			zrle.action = zrle.plain_rle;
			return 0;
		}

		rpos = g.work.rpos + 1;
		run_length = 0;
		while (g.work.data[rpos] == 255) {
			run_length += 255;
			if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
				exit(1);
			if (g.work.wpos < ++rpos + 1) {
				zrle.action = zrle.plain_rle;
				return 0;
			}
		}
		run_length += g.work.data[rpos++] + 1;
		if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
			exit(1);
		ggiSetGCForeground(zrle.stem, g.work.data[g.work.rpos]);
		g.work.rpos = rpos;

		if (zrle.rle / zrle.s.x == (zrle.rle + run_length) / zrle.s.x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + zrle.rle % zrle.s.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);
			zrle.rle += run_length;
			continue;
		}

		start_x = zrle.rle % zrle.s.x;
		if (start_x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + start_x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x - start_x);
			zrle.rle += zrle.s.x - start_x;
			run_length -= zrle.s.x - start_x;
		}
		if (run_length > zrle.s.x) {
			ggiDrawBox(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x,
				run_length / zrle.s.x);
			zrle.rle += run_length / zrle.s.x * zrle.s.x;
			run_length %= zrle.s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);

		zrle.rle += run_length;
	} while (zrle.rle < zrle.s.x * zrle.s.y);
	
	if (zrle.action == zrle.plain_rle) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_16(void)
{
	int run_length;
	int rpos;
	int start_x;
	uint16_t pixel;

	debug(3, "zrle_plain_rle_16\n");

	do {
		if (g.work.wpos < g.work.rpos + 3) {
			zrle.action = zrle.plain_rle;
			return 0;
		}

		rpos = g.work.rpos + 2;
		run_length = 0;
		while (g.work.data[rpos] == 255) {
			run_length += 255;
			if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
				exit(1);
			if (g.work.wpos < ++rpos + 1) {
				zrle.action = zrle.plain_rle;
				return 0;
			}
		}
		run_length += g.work.data[rpos++] + 1;
		if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
			exit(1);
		if (g.wire_endian != g.local_endian)
			pixel = get16_r(&g.work.data[g.work.rpos]);
		else
			pixel = get16(&g.work.data[g.work.rpos]);
		ggiSetGCForeground(zrle.stem, pixel);
		g.work.rpos = rpos;

		if (zrle.rle / zrle.s.x == (zrle.rle + run_length) / zrle.s.x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + zrle.rle % zrle.s.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);
			zrle.rle += run_length;
			continue;
		}

		start_x = zrle.rle % zrle.s.x;
		if (start_x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + start_x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x - start_x);
			zrle.rle += zrle.s.x - start_x;
			run_length -= zrle.s.x - start_x;
		}
		if (run_length > zrle.s.x) {
			ggiDrawBox(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x,
				run_length / zrle.s.x);
			zrle.rle += run_length / zrle.s.x * zrle.s.x;
			run_length %= zrle.s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);

		zrle.rle += run_length;
	} while (zrle.rle < zrle.s.x * zrle.s.y);
	
	if (zrle.action == zrle.plain_rle) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_24(void)
{
	int run_length;
	int rpos;
	int start_x;
	uint32_t pixel;

	debug(3, "zrle_plain_rle_24\n");

	do {
		if (g.work.wpos < g.work.rpos + 4) {
			zrle.action = zrle.plain_rle;
			return 0;
		}

		rpos = g.work.rpos + 3;
		run_length = 0;
		while (g.work.data[rpos] == 255) {
			run_length += 255;
			if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
				exit(1);
			if (g.work.wpos < ++rpos + 1) {
				zrle.action = zrle.plain_rle;
				return 0;
			}
		}
		run_length += g.work.data[rpos++] + 1;
		if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
			exit(1);
		pixel = zrle.get24(&g.work.data[g.work.rpos]);
		ggiSetGCForeground(zrle.stem, pixel);
		g.work.rpos = rpos;

		if (zrle.rle / zrle.s.x == (zrle.rle + run_length) / zrle.s.x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + zrle.rle % zrle.s.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);
			zrle.rle += run_length;
			continue;
		}

		start_x = zrle.rle % zrle.s.x;
		if (start_x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + start_x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x - start_x);
			zrle.rle += zrle.s.x - start_x;
			run_length -= zrle.s.x - start_x;
		}
		if (run_length > zrle.s.x) {
			ggiDrawBox(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x,
				run_length / zrle.s.x);
			zrle.rle += run_length / zrle.s.x * zrle.s.x;
			run_length %= zrle.s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);

		zrle.rle += run_length;
	} while (zrle.rle < zrle.s.x * zrle.s.y);
	
	if (zrle.action == zrle.plain_rle) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_32(void)
{
	int run_length;
	int rpos;
	int start_x;
	uint32_t pixel;

	debug(3, "zrle_plain_rle_32\n");

	do {
		if (g.work.wpos < g.work.rpos + 5) {
			zrle.action = zrle.plain_rle;
			return 0;
		}

		rpos = g.work.rpos + 4;
		run_length = 0;
		while (g.work.data[rpos] == 255) {
			run_length += 255;
			if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
				exit(1);
			if (g.work.wpos < ++rpos + 1) {
				zrle.action = zrle.plain_rle;
				return 0;
			}
		}
		run_length += g.work.data[rpos++] + 1;
		if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
			exit(1);
		if (g.wire_endian != g.local_endian)
			pixel = get32_r(&g.work.data[g.work.rpos]);
		else
			pixel = get32(&g.work.data[g.work.rpos]);
		ggiSetGCForeground(zrle.stem, pixel);
		g.work.rpos = rpos;

		if (zrle.rle / zrle.s.x == (zrle.rle + run_length) / zrle.s.x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + zrle.rle % zrle.s.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);
			zrle.rle += run_length;
			continue;
		}

		start_x = zrle.rle % zrle.s.x;
		if (start_x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + start_x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x - start_x);
			zrle.rle += zrle.s.x - start_x;
			run_length -= zrle.s.x - start_x;
		}
		if (run_length > zrle.s.x) {
			ggiDrawBox(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x,
				run_length / zrle.s.x);
			zrle.rle += run_length / zrle.s.x * zrle.s.x;
			run_length %= zrle.s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);

		zrle.rle += run_length;
	} while (zrle.rle < zrle.s.x * zrle.s.y);
	
	if (zrle.action == zrle.plain_rle) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_palette_rle(void)
{
	int run_length;
	int rpos;
	int start_x;
	uint8_t color;

	debug(3, "zrle_palette_rle\n");

	do {
		if (g.work.wpos < g.work.rpos + 1) {
			zrle.action = zrle_palette_rle;
			return 0;
		}

		color = g.work.data[g.work.rpos] & 0x7f;
		if (color >= zrle.palette_size) {
			debug(1, "zrle color %d outside %d\n",
				color, zrle.palette_size);
			exit(1);
		}

		if (!(g.work.data[g.work.rpos] & 0x80)) {
			++g.work.rpos;
			ggiPutPixel(zrle.stem,
				zrle.p.x + zrle.rle % zrle.s.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.palette[color]);
			++zrle.rle;
			continue;
		}

		if (g.work.wpos < g.work.rpos + 2) {
			zrle.action = zrle_palette_rle;
			return 0;
		}

		rpos = g.work.rpos + 1;
		run_length = 0;
		while (g.work.data[rpos] == 255) {
			run_length += 255;
			if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
				exit(1);
			if (g.work.wpos < ++rpos + 1) {
				zrle.action = zrle_palette_rle;
				return 0;
			}
		}
		run_length += g.work.data[rpos++] + 1;
		if (zrle.rle + run_length > zrle.s.x * zrle.s.y)
			exit(1);
		ggiSetGCForeground(zrle.stem, zrle.palette[color]);
		g.work.rpos = rpos;

		if (zrle.rle / zrle.s.x == (zrle.rle + run_length) / zrle.s.x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + zrle.rle % zrle.s.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);
			zrle.rle += run_length;
			continue;
		}

		start_x = zrle.rle % zrle.s.x;
		if (start_x) {
			ggiDrawHLine(zrle.stem,
				zrle.p.x + start_x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x - start_x);
			zrle.rle += zrle.s.x - start_x;
			run_length -= zrle.s.x - start_x;
		}
		if (run_length > zrle.s.x) {
			ggiDrawBox(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				zrle.s.x,
				run_length / zrle.s.x);
			zrle.rle += run_length / zrle.s.x * zrle.s.x;
			run_length %= zrle.s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle.stem,
				zrle.p.x,
				zrle.p.y + zrle.rle / zrle.s.x,
				run_length);

		zrle.rle += run_length;
	} while (zrle.rle < zrle.s.x * zrle.s.y);
	
	if (zrle.action == zrle_palette_rle) {
		if (!zrle_next())
			return zrle_done();
		zrle.action = zrle_tile;
	}
	return 1;
}

static int
zrle_palette_8(action_t *action)
{
	int i;

	debug(3, "zrle_palette_8\n");

	if (g.work.wpos < g.work.rpos + zrle.palette_size) {
		--g.work.rpos;
		zrle.action = zrle_tile;
		return 0;
	}

	for (i = 0; i < zrle.palette_size; ++i)
		zrle.palette[i] = g.work.data[g.work.rpos++];

	return action();
}

static int
zrle_palette_16(action_t *action)
{
	int i;

	debug(3, "zrle_palette_16\n");

	if (g.work.wpos < g.work.rpos + 2 * zrle.palette_size) {
		--g.work.rpos;
		zrle.action = zrle_tile;
		return 0;
	}

	if (g.wire_endian != g.local_endian) {
		for (i = 0; i < zrle.palette_size; ++i) {
			zrle.palette[i] =
				get16_r(&g.work.data[g.work.rpos]);
			g.work.rpos += 2;
		}
	}
	else {
		for (i = 0; i < zrle.palette_size; ++i) {
			zrle.palette[i] =
				get16(&g.work.data[g.work.rpos]);
			g.work.rpos += 2;
		}
	}

	return action();
}

static int
zrle_palette_24(action_t *action)
{
	int i;

	debug(3, "zrle_palette_24\n");

	if (g.work.wpos < g.work.rpos + 3 * zrle.palette_size) {
		--g.work.rpos;
		zrle.action = zrle_tile;
		return 0;
	}

	for (i = 0; i < zrle.palette_size; ++i) {
		zrle.palette[i] =
			zrle.get24(&g.work.data[g.work.rpos]);
		g.work.rpos += 3;
	}

	return action();
}

static int
zrle_palette_32(action_t *action)
{
	int i;

	debug(3, "zrle_palette_32\n");

	if (g.work.wpos < g.work.rpos + 4 * zrle.palette_size) {
		--g.work.rpos;
		zrle.action = zrle_tile;
		return 0;
	}

	if (g.wire_endian != g.local_endian) {
		for (i = 0; i < zrle.palette_size; ++i) {
			zrle.palette[i] = get32_r(&g.work.data[g.work.rpos]);
			g.work.rpos += 4;
		}
	}
	else {
		for (i = 0; i < zrle.palette_size; ++i) {
			zrle.palette[i] = get32(&g.work.data[g.work.rpos]);
			g.work.rpos += 4;
		}
	}


	return action();
}

static int
zrle_tile(void)
{
	debug(3, "zrle_tile\n");

	do {
		if (g.work.wpos < g.work.rpos + 1) {
			zrle.action = zrle_tile;
			return 0;
		}
		zrle.subencoding = g.work.data[g.work.rpos++];

		if (zrle.subencoding == 0) {
			if (!zrle.raw())
				return 0;
			continue;
		}
		if (zrle.subencoding == 1) {
			if (!zrle.solid())
				return 0;
			continue;
		}
		if (zrle.subencoding <= 16) {
			zrle.palette_size = zrle.subencoding;
			if (!zrle.parse_palette(zrle.packed_palette))
				return 0;
			continue;
		}
		if (zrle.subencoding == 128) {
			zrle.rle = 0;
			if (!zrle.plain_rle())
				return 0;
			continue;
		}
		if (zrle.subencoding >= 130) {
			zrle.rle = 0;
			zrle.palette_size = zrle.subencoding - 128;
			if (!zrle.parse_palette(zrle_palette_rle))
				return 0;
			continue;
		}
		exit(1);
	} while (zrle_next());

	zrle_done();
	return 1;
}

static int
zrle_inflate(void)
{
	int length;
	int res;
	int flush;
	const uint32_t max_length = 0x100000;
	int chunked;

	debug(3, "zrle_inflate\n");

	chunked = zrle.length > max_length;
	length = chunked ? max_length : zrle.length;

	if (g.input.wpos < g.input.rpos + length) {
		length = g.input.wpos - g.input.rpos;
		chunked = 0;
		flush = Z_NO_FLUSH;
	}
	else
		flush = chunked ? Z_NO_FLUSH : Z_SYNC_FLUSH;

	zrle.zstr.avail_in = length;
	zrle.zstr.next_in = &g.input.data[g.input.rpos];

	do {
		if (g.work.wpos == g.work.size) {
			void *tmp;
			g.work.size += 65536;
			tmp = realloc(g.work.data, g.work.size);
			if (!tmp) {
				free(g.work.data);
				g.work.data = NULL;
				exit(1);
			}
			g.work.data = tmp;
		}
		zrle.zstr.avail_out = g.work.size - g.work.wpos;
		zrle.zstr.next_out = &g.work.data[g.work.wpos];

		res = inflate(&zrle.zstr, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			inflateEnd(&zrle.zstr);
			exit(1);
		}

		g.work.wpos = g.work.size - zrle.zstr.avail_out;
	} while (!zrle.zstr.avail_out);

	zrle.length -= length - zrle.zstr.avail_in;
	g.input.rpos += length - zrle.zstr.avail_in;

	while (zrle.action) {
		if (!zrle.action())
			break;
	}

	if (zrle.length) {
		if (zrle.p.y >= g.y + g.h)
			return zrle_drain_inflate();

		g.action = zrle_inflate;
		return chunked;
	}

	return 1;
}

int
vnc_zrle(void)
{
	ggi_pixel mask;
	const ggi_pixelformat *pf;

	debug(2, "zrle\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	zrle.length = get32_hilo(&g.input.data[g.input.rpos]);
	g.input.rpos += 4;

	zrle.p.x = g.x + g.w;
	zrle.p.y = g.y - 64;
	zrle_next();

	g.stem_change = zrle_stem_change;
	g.stem_change();

	switch (GT_SIZE(g.wire_mode.graphtype)) {
	case  8:
		zrle.raw            = zrle_raw_8;
		zrle.solid          = zrle_solid_8;
		zrle.packed_palette = zrle_packed_palette_8;
		zrle.plain_rle      = zrle_plain_rle_8;
		zrle.parse_palette  = zrle_palette_8;
		break;
	case 16:
		zrle.raw            = zrle_raw_16;
		zrle.solid          = zrle_solid_16;
		zrle.packed_palette = zrle_packed_palette_16;
		zrle.plain_rle      = zrle_plain_rle_16;
		zrle.parse_palette  = zrle_palette_16;
		break;
	case 32:
		pf = ggiGetPixelFormat(zrle.stem);
		mask = pf->red_mask | pf->green_mask | pf->blue_mask;
		if (!(mask & 0xff000000)) {
			if (g.wire_endian != g.local_endian) {
				if (g.wire_endian)
					zrle.get24 = get24bl_r;
				else
					zrle.get24 = get24ll_r;
			}
			else {
				if (g.wire_endian)
					zrle.get24 = get24bl;
				else
					zrle.get24 = get24ll;
			}
			zrle.raw            = zrle_raw_24;
			zrle.solid          = zrle_solid_24;
			zrle.plain_rle      = zrle_plain_rle_24;
			zrle.parse_palette  = zrle_palette_24;
		}
		else if (!(mask & 0xff)) {
			if (g.wire_endian != g.local_endian) {
				if (g.wire_endian)
					zrle.get24 = get24bh_r;
				else
					zrle.get24 = get24lh_r;
			}
			else {
				if (g.wire_endian)
					zrle.get24 = get24bh;
				else
					zrle.get24 = get24lh;
			}
			zrle.raw            = zrle_raw_24;
			zrle.solid          = zrle_solid_24;
			zrle.plain_rle      = zrle_plain_rle_24;
			zrle.parse_palette  = zrle_palette_24;
		}
		else {
			zrle.raw            = zrle_raw_32;
			zrle.solid          = zrle_solid_32;
			zrle.plain_rle      = zrle_plain_rle_32;
			zrle.parse_palette  = zrle_palette_32;
		}
		zrle.packed_palette = zrle_packed_palette_32;
		break;
	}
	zrle.action = zrle_tile;

	return zrle_inflate();
}

int
vnc_zrle_init(void)
{
	zrle.zstr.zalloc = Z_NULL;
	zrle.zstr.zfree = Z_NULL;
	zrle.zstr.opaque = Z_NULL;
	zrle.zstr.avail_in = 0;
	zrle.zstr.next_in = Z_NULL;
	zrle.zstr.avail_out = 0;

	if (inflateInit(&zrle.zstr) != Z_OK)
		return -1;

	zrle.unpacked = (uint8_t *)malloc(4 * 64 * 64);
	if (!zrle.unpacked) {
		inflateEnd(&zrle.zstr);
		return -1;
	}

	return 0;
}
