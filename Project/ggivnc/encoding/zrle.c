/*
******************************************************************************

   VNC viewer ZRLE encoding.

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
#include <zlib.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

struct zrle {
	uint32_t length;
	z_stream zstr;

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
	int (*parse_palette)(struct connection *cx, action_t *action);
	uint32_t (*get24)(const uint8_t *buf);
};

static int
zrle_stem_change(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;

	zrle->stem = cx->wire_stem ? cx->wire_stem : cx->stem;
	return 0;
}

static int zrle_tile(struct connection *cx);

static int
zrle_done(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;

	debug(3, "zrle_done\n");

	cx->work.rpos = 0;
	cx->work.wpos = 0;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	zrle->action = NULL;
	return 1;
}

static int
zrle_next(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;

	debug(3, "zrle_next\n");

	zrle->p.x += 64;
	if (zrle->p.x < cx->x + cx->w) {
		if (cx->x + cx->w - zrle->p.x < 64)
			zrle->s.x = cx->x + cx->w - zrle->p.x;
		else
			zrle->s.x = 64;
		return 1;
	}

	zrle->p.x = cx->x;
	if (cx->w < 64)
		zrle->s.x = cx->w;
	else
		zrle->s.x = 64;
	zrle->p.y += 64;
	if (zrle->p.y >= cx->y + cx->h)
		return 0;
	if (cx->y + cx->h - zrle->p.y < 64)
		zrle->s.y = cx->y + cx->h - zrle->p.y;
	else
		zrle->s.y = 64;
	return 1;
}

static int
zrle_drain_inflate(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int length;
	int res;
	int flush;
	const uint32_t max_length = 0x1000;
	int chunked;
	uint8_t tmp[128];

	debug(3, "zrle_drain_inflate\n");

	chunked = zrle->length > max_length;
	length = chunked ? max_length : zrle->length;

	if (cx->input.wpos < cx->input.rpos + length) {
		length = cx->input.wpos - cx->input.rpos;
		chunked = 0;
		flush = Z_NO_FLUSH;
	}
	else
		flush = chunked ? Z_NO_FLUSH : Z_SYNC_FLUSH;

	if (!length) {
		cx->action = zrle_drain_inflate;
		return 0;
	}

	zrle->zstr.avail_in = length;
	zrle->zstr.next_in = &cx->input.data[cx->input.rpos];
	zrle->zstr.avail_out = sizeof(tmp);
	zrle->zstr.next_out = tmp;
	res = inflate(&zrle->zstr, flush);
	switch (res) {
	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
		debug(1, "inflate result %d\n", res);
		inflateEnd(&zrle->zstr);
		return close_connection(cx, -1);
	}

	zrle->length -= length - zrle->zstr.avail_in;
	cx->input.rpos += length - zrle->zstr.avail_in;

	remove_dead_data(&cx->input);

	if (zrle->length) {
		cx->action = zrle_drain_inflate;
		return chunked;
	}

	cx->action = vnc_update_rect;
	return 1;
}

static int
zrle_raw_8(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int bytes = zrle->s.x * zrle->s.y;

	debug(3, "zrle_raw_8\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		zrle->action = zrle->raw;
		return 0;
	}

	ggiPutBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y,
		cx->work.data + cx->work.rpos);
	cx->work.rpos += bytes;

	if (zrle->action == zrle->raw) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_raw_16(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int bytes = 2 * zrle->s.x * zrle->s.y;
	uint16_t *buf;

	debug(3, "zrle_raw_16\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		zrle->action = zrle->raw;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		buf = (uint16_t *)zrle->unpacked;
		for (; bytes; bytes -= 2) {
			*buf++ = get16_r(&cx->work.data[cx->work.rpos]);
			cx->work.rpos += 2;
		}
	}
	else {
		memcpy(zrle->unpacked, &cx->work.data[cx->work.rpos], bytes);
		cx->work.rpos += bytes;
	}
	ggiPutBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y,
		zrle->unpacked);

	if (zrle->action == zrle->raw) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_raw_24(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int bytes = 3 * zrle->s.x * zrle->s.y;
	uint32_t *buf;

	debug(3, "zrle_raw_24\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		zrle->action = zrle->raw;
		return 0;
	}

	buf = (uint32_t *)zrle->unpacked;
	for (; bytes; bytes -= 3) {
		*buf++ = zrle->get24(&cx->work.data[cx->work.rpos]);
		cx->work.rpos += 3;
	}
	ggiPutBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y,
		zrle->unpacked);

	if (zrle->action == zrle->raw) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_raw_32(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int bytes = 4 * zrle->s.x * zrle->s.y;
	uint32_t *buf;

	debug(3, "zrle_raw_32\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		zrle->action = zrle->raw;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		buf = (uint32_t *)zrle->unpacked;
		for (; bytes; bytes -= 4) {
			*buf++ = get32_r(&cx->work.data[cx->work.rpos]);
			cx->work.rpos += 4;
		}
	}
	else {
		memcpy(zrle->unpacked, &cx->work.data[cx->work.rpos], bytes);
		cx->work.rpos += bytes;
	}
	ggiPutBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y,
		zrle->unpacked);

	if (zrle->action == zrle->raw) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_8(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;

	debug(3, "zrle_solid_8\n");

	if (cx->work.wpos < cx->work.rpos + 1) {
		zrle->action = zrle->solid;
		return 0;
	}

	ggiSetGCForeground(zrle->stem, cx->work.data[cx->work.rpos++]);
	ggiDrawBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y);

	if (zrle->action == zrle->solid) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_16(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	uint16_t pixel;
	debug(3, "zrle_solid_16\n");

	if (cx->work.wpos < cx->work.rpos + 2) {
		zrle->action = zrle->solid;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get16_r(&cx->work.data[cx->work.rpos]);
	else
		pixel = get16(&cx->work.data[cx->work.rpos]);
	cx->work.rpos += 2;
	ggiSetGCForeground(zrle->stem, pixel);
	ggiDrawBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y);

	if (zrle->action == zrle->solid) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_24(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	uint32_t pixel;
	debug(3, "zrle_solid_24\n");

	if (cx->work.wpos < cx->work.rpos + 3) {
		zrle->action = zrle->solid;
		return 0;
	}

	pixel = zrle->get24(&cx->work.data[cx->work.rpos]);
	cx->work.rpos += 3;
	ggiSetGCForeground(zrle->stem, pixel);
	ggiDrawBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y);

	if (zrle->action == zrle->solid) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_solid_32(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	uint32_t pixel;
	debug(3, "zrle_solid_32\n");

	if (cx->work.wpos < cx->work.rpos + 4) {
		zrle->action = zrle->solid;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get32_r(&cx->work.data[cx->work.rpos]);
	else
		pixel = get32(&cx->work.data[cx->work.rpos]);
	cx->work.rpos += 4;
	ggiSetGCForeground(zrle->stem, pixel);
	ggiDrawBox(zrle->stem, zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y);

	if (zrle->action == zrle->solid) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_packed_palette_8(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint8_t *dst;

	debug(3, "zrle_packed_palette_8\n");

	if (zrle->subencoding == 2) {
		step = 1;
		extra = (zrle->s.x + 7) / 8 * zrle->s.y;
	}
	else if (zrle->subencoding <= 4) {
		step = 2;
		extra = (zrle->s.x + 3) / 4 * zrle->s.y;
	}
	else {
		step = 4;
		extra = (zrle->s.x + 1) / 2 * zrle->s.y;
	}

	if (cx->work.wpos < cx->work.rpos + extra) {
		zrle->action = zrle->packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &cx->work.data[cx->work.rpos];
	dst = zrle->unpacked;

	for (y = 0; y < zrle->s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < zrle->s.x; ++x) {
			*dst++ = zrle->palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(zrle->stem,
		zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y,
		zrle->unpacked);

	cx->work.rpos += extra;

	if (zrle->action == zrle->packed_palette) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_packed_palette_16(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint16_t *dst;

	debug(3, "zrle_packed_palette_16\n");

	if (zrle->subencoding == 2) {
		step = 1;
		extra = (zrle->s.x + 7) / 8 * zrle->s.y;
	}
	else if (zrle->subencoding <= 4) {
		step = 2;
		extra = (zrle->s.x + 3) / 4 * zrle->s.y;
	}
	else {
		step = 4;
		extra = (zrle->s.x + 1) / 2 * zrle->s.y;
	}

	if (cx->work.wpos < cx->work.rpos + extra) {
		zrle->action = zrle->packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &cx->work.data[cx->work.rpos];
	dst = (uint16_t *)zrle->unpacked;

	for (y = 0; y < zrle->s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < zrle->s.x; ++x) {
			*dst++ = zrle->palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(zrle->stem,
		zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y,
		zrle->unpacked);

	cx->work.rpos += extra;

	if (zrle->action == zrle->packed_palette) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_packed_palette_32(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint32_t *dst;

	debug(3, "zrle_packed_palette_32\n");

	if (zrle->subencoding == 2) {
		step = 1;
		extra = (zrle->s.x + 7) / 8 * zrle->s.y;
	}
	else if (zrle->subencoding <= 4) {
		step = 2;
		extra = (zrle->s.x + 3) / 4 * zrle->s.y;
	}
	else {
		step = 4;
		extra = (zrle->s.x + 1) / 2 * zrle->s.y;
	}

	if (cx->work.wpos < cx->work.rpos + extra) {
		zrle->action = zrle->packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &cx->work.data[cx->work.rpos];
	dst = (uint32_t *)zrle->unpacked;

	for (y = 0; y < zrle->s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < zrle->s.x; ++x) {
			*dst++ = zrle->palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(zrle->stem,
		zrle->p.x, zrle->p.y, zrle->s.x, zrle->s.y,
		zrle->unpacked);

	cx->work.rpos += extra;

	if (zrle->action == zrle->packed_palette) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_8(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;

	debug(3, "zrle_plain_rle_8\n");

	do {
		if (cx->work.wpos < cx->work.rpos + 2) {
			zrle->action = zrle->plain_rle;
			return 0;
		}

		rpos = cx->work.rpos + 1;
		run_length = 0;
		while (cx->work.data[rpos] == 255) {
			run_length += 255;
			if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
				return close_connection(cx, -1);
			if (cx->work.wpos < ++rpos + 1) {
				zrle->action = zrle->plain_rle;
				return 0;
			}
		}
		run_length += cx->work.data[rpos++] + 1;
		if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
			return close_connection(cx, -1);
		ggiSetGCForeground(zrle->stem, cx->work.data[cx->work.rpos]);
		cx->work.rpos = rpos;

		if (zrle->rle / zrle->s.x ==
				(zrle->rle + run_length) / zrle->s.x)
		{
			ggiDrawHLine(zrle->stem,
				zrle->p.x + zrle->rle % zrle->s.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);
			zrle->rle += run_length;
			continue;
		}

		start_x = zrle->rle % zrle->s.x;
		if (start_x) {
			ggiDrawHLine(zrle->stem,
				zrle->p.x + start_x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x - start_x);
			zrle->rle += zrle->s.x - start_x;
			run_length -= zrle->s.x - start_x;
		}
		if (run_length > zrle->s.x) {
			ggiDrawBox(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x,
				run_length / zrle->s.x);
			zrle->rle += run_length / zrle->s.x * zrle->s.x;
			run_length %= zrle->s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);

		zrle->rle += run_length;
	} while (zrle->rle < zrle->s.x * zrle->s.y);
	
	if (zrle->action == zrle->plain_rle) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_16(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint16_t pixel;

	debug(3, "zrle_plain_rle_16\n");

	do {
		if (cx->work.wpos < cx->work.rpos + 3) {
			zrle->action = zrle->plain_rle;
			return 0;
		}

		rpos = cx->work.rpos + 2;
		run_length = 0;
		while (cx->work.data[rpos] == 255) {
			run_length += 255;
			if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
				return close_connection(cx, -1);
			if (cx->work.wpos < ++rpos + 1) {
				zrle->action = zrle->plain_rle;
				return 0;
			}
		}
		run_length += cx->work.data[rpos++] + 1;
		if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
			return close_connection(cx, -1);
		if (cx->wire_endian != cx->local_endian)
			pixel = get16_r(&cx->work.data[cx->work.rpos]);
		else
			pixel = get16(&cx->work.data[cx->work.rpos]);
		ggiSetGCForeground(zrle->stem, pixel);
		cx->work.rpos = rpos;

		if (zrle->rle / zrle->s.x ==
			(zrle->rle + run_length) / zrle->s.x)
		{
			ggiDrawHLine(zrle->stem,
				zrle->p.x + zrle->rle % zrle->s.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);
			zrle->rle += run_length;
			continue;
		}

		start_x = zrle->rle % zrle->s.x;
		if (start_x) {
			ggiDrawHLine(zrle->stem,
				zrle->p.x + start_x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x - start_x);
			zrle->rle += zrle->s.x - start_x;
			run_length -= zrle->s.x - start_x;
		}
		if (run_length > zrle->s.x) {
			ggiDrawBox(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x,
				run_length / zrle->s.x);
			zrle->rle += run_length / zrle->s.x * zrle->s.x;
			run_length %= zrle->s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);

		zrle->rle += run_length;
	} while (zrle->rle < zrle->s.x * zrle->s.y);
	
	if (zrle->action == zrle->plain_rle) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_24(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint32_t pixel;

	debug(3, "zrle_plain_rle_24\n");

	do {
		if (cx->work.wpos < cx->work.rpos + 4) {
			zrle->action = zrle->plain_rle;
			return 0;
		}

		rpos = cx->work.rpos + 3;
		run_length = 0;
		while (cx->work.data[rpos] == 255) {
			run_length += 255;
			if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
				return close_connection(cx, -1);
			if (cx->work.wpos < ++rpos + 1) {
				zrle->action = zrle->plain_rle;
				return 0;
			}
		}
		run_length += cx->work.data[rpos++] + 1;
		if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
			return close_connection(cx, -1);
		pixel = zrle->get24(&cx->work.data[cx->work.rpos]);
		ggiSetGCForeground(zrle->stem, pixel);
		cx->work.rpos = rpos;

		if (zrle->rle / zrle->s.x ==
			(zrle->rle + run_length) / zrle->s.x)
		{
			ggiDrawHLine(zrle->stem,
				zrle->p.x + zrle->rle % zrle->s.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);
			zrle->rle += run_length;
			continue;
		}

		start_x = zrle->rle % zrle->s.x;
		if (start_x) {
			ggiDrawHLine(zrle->stem,
				zrle->p.x + start_x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x - start_x);
			zrle->rle += zrle->s.x - start_x;
			run_length -= zrle->s.x - start_x;
		}
		if (run_length > zrle->s.x) {
			ggiDrawBox(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x,
				run_length / zrle->s.x);
			zrle->rle += run_length / zrle->s.x * zrle->s.x;
			run_length %= zrle->s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);

		zrle->rle += run_length;
	} while (zrle->rle < zrle->s.x * zrle->s.y);
	
	if (zrle->action == zrle->plain_rle) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_plain_rle_32(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint32_t pixel;

	debug(3, "zrle_plain_rle_32\n");

	do {
		if (cx->work.wpos < cx->work.rpos + 5) {
			zrle->action = zrle->plain_rle;
			return 0;
		}

		rpos = cx->work.rpos + 4;
		run_length = 0;
		while (cx->work.data[rpos] == 255) {
			run_length += 255;
			if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
				return close_connection(cx, -1);
			if (cx->work.wpos < ++rpos + 1) {
				zrle->action = zrle->plain_rle;
				return 0;
			}
		}
		run_length += cx->work.data[rpos++] + 1;
		if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
			return close_connection(cx, -1);
		if (cx->wire_endian != cx->local_endian)
			pixel = get32_r(&cx->work.data[cx->work.rpos]);
		else
			pixel = get32(&cx->work.data[cx->work.rpos]);
		ggiSetGCForeground(zrle->stem, pixel);
		cx->work.rpos = rpos;

		if (zrle->rle / zrle->s.x ==
			(zrle->rle + run_length) / zrle->s.x)
		{
			ggiDrawHLine(zrle->stem,
				zrle->p.x + zrle->rle % zrle->s.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);
			zrle->rle += run_length;
			continue;
		}

		start_x = zrle->rle % zrle->s.x;
		if (start_x) {
			ggiDrawHLine(zrle->stem,
				zrle->p.x + start_x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x - start_x);
			zrle->rle += zrle->s.x - start_x;
			run_length -= zrle->s.x - start_x;
		}
		if (run_length > zrle->s.x) {
			ggiDrawBox(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x,
				run_length / zrle->s.x);
			zrle->rle += run_length / zrle->s.x * zrle->s.x;
			run_length %= zrle->s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);

		zrle->rle += run_length;
	} while (zrle->rle < zrle->s.x * zrle->s.y);
	
	if (zrle->action == zrle->plain_rle) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_palette_rle(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint8_t color;

	debug(3, "zrle_palette_rle\n");

	do {
		if (cx->work.wpos < cx->work.rpos + 1) {
			zrle->action = zrle_palette_rle;
			return 0;
		}

		color = cx->work.data[cx->work.rpos] & 0x7f;
		if (color >= zrle->palette_size) {
			debug(1, "zrle color %d outside %d\n",
				color, zrle->palette_size);
			return close_connection(cx, -1);
		}

		if (!(cx->work.data[cx->work.rpos] & 0x80)) {
			++cx->work.rpos;
			ggiPutPixel(zrle->stem,
				zrle->p.x + zrle->rle % zrle->s.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->palette[color]);
			++zrle->rle;
			continue;
		}

		if (cx->work.wpos < cx->work.rpos + 2) {
			zrle->action = zrle_palette_rle;
			return 0;
		}

		rpos = cx->work.rpos + 1;
		run_length = 0;
		while (cx->work.data[rpos] == 255) {
			run_length += 255;
			if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
				return close_connection(cx, -1);
			if (cx->work.wpos < ++rpos + 1) {
				zrle->action = zrle_palette_rle;
				return 0;
			}
		}
		run_length += cx->work.data[rpos++] + 1;
		if (zrle->rle + run_length > zrle->s.x * zrle->s.y)
			return close_connection(cx, -1);
		ggiSetGCForeground(zrle->stem, zrle->palette[color]);
		cx->work.rpos = rpos;

		if (zrle->rle / zrle->s.x ==
			(zrle->rle + run_length) / zrle->s.x)
		{
			ggiDrawHLine(zrle->stem,
				zrle->p.x + zrle->rle % zrle->s.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);
			zrle->rle += run_length;
			continue;
		}

		start_x = zrle->rle % zrle->s.x;
		if (start_x) {
			ggiDrawHLine(zrle->stem,
				zrle->p.x + start_x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x - start_x);
			zrle->rle += zrle->s.x - start_x;
			run_length -= zrle->s.x - start_x;
		}
		if (run_length > zrle->s.x) {
			ggiDrawBox(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				zrle->s.x,
				run_length / zrle->s.x);
			zrle->rle += run_length / zrle->s.x * zrle->s.x;
			run_length %= zrle->s.x;
		}
		if (run_length)
			ggiDrawHLine(zrle->stem,
				zrle->p.x,
				zrle->p.y + zrle->rle / zrle->s.x,
				run_length);

		zrle->rle += run_length;
	} while (zrle->rle < zrle->s.x * zrle->s.y);
	
	if (zrle->action == zrle_palette_rle) {
		if (!zrle_next(cx))
			return zrle_done(cx);
		zrle->action = zrle_tile;
	}
	return 1;
}

static int
zrle_palette_8(struct connection *cx, action_t *action)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int i;

	debug(3, "zrle_palette_8\n");

	if (cx->work.wpos < cx->work.rpos + zrle->palette_size) {
		--cx->work.rpos;
		zrle->action = zrle_tile;
		return 0;
	}

	for (i = 0; i < zrle->palette_size; ++i)
		zrle->palette[i] = cx->work.data[cx->work.rpos++];

	return action(cx);
}

static int
zrle_palette_16(struct connection *cx, action_t *action)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int i;

	debug(3, "zrle_palette_16\n");

	if (cx->work.wpos < cx->work.rpos + 2 * zrle->palette_size) {
		--cx->work.rpos;
		zrle->action = zrle_tile;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		for (i = 0; i < zrle->palette_size; ++i) {
			zrle->palette[i] =
				get16_r(&cx->work.data[cx->work.rpos]);
			cx->work.rpos += 2;
		}
	}
	else {
		for (i = 0; i < zrle->palette_size; ++i) {
			zrle->palette[i] =
				get16(&cx->work.data[cx->work.rpos]);
			cx->work.rpos += 2;
		}
	}

	return action(cx);
}

static int
zrle_palette_24(struct connection *cx, action_t *action)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int i;

	debug(3, "zrle_palette_24\n");

	if (cx->work.wpos < cx->work.rpos + 3 * zrle->palette_size) {
		--cx->work.rpos;
		zrle->action = zrle_tile;
		return 0;
	}

	for (i = 0; i < zrle->palette_size; ++i) {
		zrle->palette[i] =
			zrle->get24(&cx->work.data[cx->work.rpos]);
		cx->work.rpos += 3;
	}

	return action(cx);
}

static int
zrle_palette_32(struct connection *cx, action_t *action)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int i;

	debug(3, "zrle_palette_32\n");

	if (cx->work.wpos < cx->work.rpos + 4 * zrle->palette_size) {
		--cx->work.rpos;
		zrle->action = zrle_tile;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		for (i = 0; i < zrle->palette_size; ++i) {
			zrle->palette[i] =
				get32_r(&cx->work.data[cx->work.rpos]);
			cx->work.rpos += 4;
		}
	}
	else {
		for (i = 0; i < zrle->palette_size; ++i) {
			zrle->palette[i] =
				get32(&cx->work.data[cx->work.rpos]);
			cx->work.rpos += 4;
		}
	}

	return action(cx);
}

static int
zrle_tile(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;

	debug(3, "zrle_tile\n");

	do {
		if (cx->work.wpos < cx->work.rpos + 1) {
			zrle->action = zrle_tile;
			return 0;
		}
		zrle->subencoding = cx->work.data[cx->work.rpos++];

		if (zrle->subencoding == 0) {
			if (!zrle->raw(cx))
				return 0;
			continue;
		}
		if (zrle->subencoding == 1) {
			if (!zrle->solid(cx))
				return 0;
			continue;
		}
		if (zrle->subencoding <= 16) {
			zrle->palette_size = zrle->subencoding;
			if (!zrle->parse_palette(cx, zrle->packed_palette))
				return 0;
			continue;
		}
		if (zrle->subencoding == 128) {
			zrle->rle = 0;
			if (!zrle->plain_rle(cx))
				return 0;
			continue;
		}
		if (zrle->subencoding >= 130) {
			zrle->rle = 0;
			zrle->palette_size = zrle->subencoding - 128;
			if (!zrle->parse_palette(cx, zrle_palette_rle))
				return 0;
			continue;
		}
		return close_connection(cx, -1);
	} while (zrle_next(cx));

	zrle_done(cx);
	return 1;
}

static int
zrle_inflate(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	int length;
	int res;
	int flush;
	const uint32_t max_length = 0x100000;
	int chunked;

	debug(3, "zrle_inflate\n");

	chunked = zrle->length > max_length;
	length = chunked ? max_length : zrle->length;

	if (cx->input.wpos < cx->input.rpos + length) {
		length = cx->input.wpos - cx->input.rpos;
		chunked = 0;
		flush = Z_NO_FLUSH;
	}
	else
		flush = chunked ? Z_NO_FLUSH : Z_SYNC_FLUSH;

	zrle->zstr.avail_in = length;
	zrle->zstr.next_in = &cx->input.data[cx->input.rpos];

	do {
		if (cx->work.wpos == cx->work.size) {
			if (buffer_reserve(&cx->work,
				cx->work.size + 65536))
			{
				debug(1, "zrle realloc failed\n");
				return close_connection(cx, -1);
			}
		}
		zrle->zstr.avail_out = cx->work.size - cx->work.wpos;
		zrle->zstr.next_out = &cx->work.data[cx->work.wpos];

		res = inflate(&zrle->zstr, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			debug(1, "zrle inflate error %d\n", res);
			inflateEnd(&zrle->zstr);
			return close_connection(cx, -1);
		}

		cx->work.wpos = cx->work.size - zrle->zstr.avail_out;
	} while (!zrle->zstr.avail_out);

	zrle->length -= length - zrle->zstr.avail_in;
	cx->input.rpos += length - zrle->zstr.avail_in;

	while (zrle->action) {
		if (!zrle->action(cx))
			break;
	}

	if (zrle->length) {
		if (zrle->p.y >= cx->y + cx->h)
			return zrle_drain_inflate(cx);

		cx->action = zrle_inflate;
		return chunked;
	}

	return 1;
}

static int
zrle_rect(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;
	ggi_pixel mask;
	const ggi_pixelformat *pf;

	debug(2, "zrle\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	zrle->length = get32_hilo(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 4;

	zrle->p.x = cx->x + cx->w;
	zrle->p.y = cx->y - 64;
	zrle_next(cx);

	cx->stem_change = zrle_stem_change;
	cx->stem_change(cx);

	switch (GT_SIZE(cx->wire_mode.graphtype)) {
	case  8:
		zrle->raw            = zrle_raw_8;
		zrle->solid          = zrle_solid_8;
		zrle->packed_palette = zrle_packed_palette_8;
		zrle->plain_rle      = zrle_plain_rle_8;
		zrle->parse_palette  = zrle_palette_8;
		break;
	case 16:
		zrle->raw            = zrle_raw_16;
		zrle->solid          = zrle_solid_16;
		zrle->packed_palette = zrle_packed_palette_16;
		zrle->plain_rle      = zrle_plain_rle_16;
		zrle->parse_palette  = zrle_palette_16;
		break;
	case 32:
		pf = ggiGetPixelFormat(zrle->stem);
		mask = pf->red_mask | pf->green_mask | pf->blue_mask;
		if (!(mask & 0xff000000)) {
			if (cx->wire_endian != cx->local_endian) {
				if (cx->wire_endian)
					zrle->get24 = get24bl_r;
				else
					zrle->get24 = get24ll_r;
			}
			else {
				if (cx->wire_endian)
					zrle->get24 = get24bl;
				else
					zrle->get24 = get24ll;
			}
			zrle->raw            = zrle_raw_24;
			zrle->solid          = zrle_solid_24;
			zrle->plain_rle      = zrle_plain_rle_24;
			zrle->parse_palette  = zrle_palette_24;
		}
		else if (!(mask & 0xff)) {
			if (cx->wire_endian != cx->local_endian) {
				if (cx->wire_endian)
					zrle->get24 = get24bh_r;
				else
					zrle->get24 = get24lh_r;
			}
			else {
				if (cx->wire_endian)
					zrle->get24 = get24bh;
				else
					zrle->get24 = get24lh;
			}
			zrle->raw            = zrle_raw_24;
			zrle->solid          = zrle_solid_24;
			zrle->plain_rle      = zrle_plain_rle_24;
			zrle->parse_palette  = zrle_palette_24;
		}
		else {
			zrle->raw            = zrle_raw_32;
			zrle->solid          = zrle_solid_32;
			zrle->plain_rle      = zrle_plain_rle_32;
			zrle->parse_palette  = zrle_palette_32;
		}
		zrle->packed_palette = zrle_packed_palette_32;
		break;
	}
	zrle->action = zrle_tile;

	return zrle_inflate(cx);
}

static void
zrle_end(struct connection *cx)
{
	struct zrle *zrle = cx->encoding_def[zrle_encoding].priv;

	if (!zrle)
		return;

	debug(1, "zrle_end\n");

	if (zrle->unpacked)
		free(zrle->unpacked);

	inflateEnd(&zrle->zstr);

	free(cx->encoding_def[zrle_encoding].priv);
	cx->encoding_def[zrle_encoding].priv = NULL;
	cx->encoding_def[zrle_encoding].action = vnc_zrle;
}

int
vnc_zrle(struct connection *cx)
{
	struct zrle *zrle;

	zrle = malloc(sizeof(*zrle));
	if (!zrle)
		return close_connection(cx, -1);
	memset(zrle, 0, sizeof(*zrle));

	cx->encoding_def[zrle_encoding].priv = zrle;
	cx->encoding_def[zrle_encoding].end = zrle_end;

	zrle->zstr.zalloc = Z_NULL;
	zrle->zstr.zfree = Z_NULL;
	zrle->zstr.opaque = Z_NULL;
	zrle->zstr.avail_in = 0;
	zrle->zstr.next_in = Z_NULL;
	zrle->zstr.avail_out = 0;

	if (inflateInit(&zrle->zstr) != Z_OK)
		return close_connection(cx, -1);

	zrle->unpacked = malloc(4 * 64 * 64);
	if (!zrle->unpacked)
		return close_connection(cx, -1);

	cx->action = cx->encoding_def[zrle_encoding].action = zrle_rect;
	return cx->action(cx);
}
