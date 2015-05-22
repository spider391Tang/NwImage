/*
******************************************************************************

   VNC viewer TRLE encoding.

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

struct trle {
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
trle_stem_change(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;

	trle->stem = cx->wire_stem ? cx->wire_stem : cx->stem;
	return 0;
}

static int trle_tile(struct connection *cx);

static int
trle_done(struct connection *cx)
{
	debug(3, "trle_done\n");

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
trle_next(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;

	debug(3, "trle_next\n");

	trle->p.x += 16;
	if (trle->p.x < cx->x + cx->w) {
		if (cx->x + cx->w - trle->p.x < 16)
			trle->s.x = cx->x + cx->w - trle->p.x;
		else
			trle->s.x = 16;
		return 1;
	}

	trle->p.x = cx->x;
	if (cx->w < 16)
		trle->s.x = cx->w;
	else
		trle->s.x = 16;
	trle->p.y += 16;
	if (trle->p.y >= cx->y + cx->h)
		return 0;
	if (cx->y + cx->h - trle->p.y < 16)
		trle->s.y = cx->y + cx->h - trle->p.y;
	else
		trle->s.y = 16;
	return 1;
}

static int
trle_raw_8(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int bytes = trle->s.x * trle->s.y;

	debug(3, "trle_raw_8\n");

	if (cx->input.wpos < cx->input.rpos + bytes) {
		cx->action = trle->raw;
		return 0;
	}

	ggiPutBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y,
		cx->input.data + cx->input.rpos);
	cx->input.rpos += bytes;

	if (cx->action == trle->raw) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_raw_16(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int bytes = 2 * trle->s.x * trle->s.y;
	uint16_t *buf;

	debug(3, "trle_raw_16\n");

	if (cx->input.wpos < cx->input.rpos + bytes) {
		cx->action = trle->raw;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		buf = (uint16_t *)trle->unpacked;
		for (; bytes; bytes -= 2) {
			*buf++ = get16_r(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
		}
	}
	else {
		memcpy(trle->unpacked, &cx->input.data[cx->input.rpos], bytes);
		cx->input.rpos += bytes;
	}
	ggiPutBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y,
		trle->unpacked);

	if (cx->action == trle->raw) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_raw_24(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int bytes = 3 * trle->s.x * trle->s.y;
	uint32_t *buf;

	debug(3, "trle_raw_24\n");

	if (cx->input.wpos < cx->input.rpos + bytes) {
		cx->action = trle->raw;
		return 0;
	}

	buf = (uint32_t *)trle->unpacked;
	for (; bytes; bytes -= 3) {
		*buf++ = trle->get24(&cx->input.data[cx->input.rpos]);
		cx->input.rpos += 3;
	}
	ggiPutBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y,
		trle->unpacked);

	if (cx->action == trle->raw) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_raw_32(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int bytes = 4 * trle->s.x * trle->s.y;
	uint32_t *buf;

	debug(3, "trle_raw_32\n");

	if (cx->input.wpos < cx->input.rpos + bytes) {
		cx->action = trle->raw;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		buf = (uint32_t *)trle->unpacked;
		for (; bytes; bytes -= 4) {
			*buf++ = get32_r(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
		}
	}
	else {
		memcpy(trle->unpacked, &cx->input.data[cx->input.rpos], bytes);
		cx->input.rpos += bytes;
	}
	ggiPutBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y,
		trle->unpacked);

	if (cx->action == trle->raw) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_solid_8(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;

	debug(3, "trle_solid_8\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = trle->solid;
		return 0;
	}

	ggiSetGCForeground(trle->stem, cx->input.data[cx->input.rpos++]);
	ggiDrawBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y);

	if (cx->action == trle->solid) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_solid_16(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	uint16_t pixel;
	debug(3, "trle_solid_16\n");

	if (cx->input.wpos < cx->input.rpos + 2) {
		cx->action = trle->solid;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get16_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get16(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 2;
	ggiSetGCForeground(trle->stem, pixel);
	ggiDrawBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y);

	if (cx->action == trle->solid) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_solid_24(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	uint32_t pixel;
	debug(3, "trle_solid_24\n");

	if (cx->input.wpos < cx->input.rpos + 3) {
		cx->action = trle->solid;
		return 0;
	}

	pixel = trle->get24(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 3;
	ggiSetGCForeground(trle->stem, pixel);
	ggiDrawBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y);

	if (cx->action == trle->solid) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_solid_32(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	uint32_t pixel;
	debug(3, "trle_solid_32\n");

	if (cx->input.wpos < cx->input.rpos + 4) {
		cx->action = trle->solid;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get32_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get32(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 4;
	ggiSetGCForeground(trle->stem, pixel);
	ggiDrawBox(trle->stem, trle->p.x, trle->p.y, trle->s.x, trle->s.y);

	if (cx->action == trle->solid) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_packed_palette_8(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint8_t *dst;

	debug(3, "trle_packed_palette_8\n");

	if (trle->subencoding == 2) {
		step = 1;
		extra = (trle->s.x + 7) / 8 * trle->s.y;
	}
	else if (trle->subencoding <= 4) {
		step = 2;
		extra = (trle->s.x + 3) / 4 * trle->s.y;
	}
	else {
		step = 4;
		extra = (trle->s.x + 1) / 2 * trle->s.y;
	}

	if (cx->input.wpos < cx->input.rpos + extra) {
		cx->action = trle->packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &cx->input.data[cx->input.rpos];
	dst = trle->unpacked;

	for (y = 0; y < trle->s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < trle->s.x; ++x) {
			*dst++ = trle->palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(trle->stem,
		trle->p.x, trle->p.y, trle->s.x, trle->s.y,
		trle->unpacked);

	cx->input.rpos += extra;

	if (cx->action == trle->packed_palette) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_packed_palette_16(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint16_t *dst;

	debug(3, "trle_packed_palette_16\n");

	if (trle->subencoding == 2) {
		step = 1;
		extra = (trle->s.x + 7) / 8 * trle->s.y;
	}
	else if (trle->subencoding <= 4) {
		step = 2;
		extra = (trle->s.x + 3) / 4 * trle->s.y;
	}
	else {
		step = 4;
		extra = (trle->s.x + 1) / 2 * trle->s.y;
	}

	if (cx->input.wpos < cx->input.rpos + extra) {
		cx->action = trle->packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &cx->input.data[cx->input.rpos];
	dst = (uint16_t *)trle->unpacked;

	for (y = 0; y < trle->s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < trle->s.x; ++x) {
			*dst++ = trle->palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(trle->stem,
		trle->p.x, trle->p.y, trle->s.x, trle->s.y,
		trle->unpacked);

	cx->input.rpos += extra;

	if (cx->action == trle->packed_palette) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_packed_palette_32(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int extra;
	int x;
	int y;
	uint8_t mask;
	int shift;
	int step;
	uint8_t *src;
	uint32_t *dst;

	debug(3, "trle_packed_palette_32\n");

	if (trle->subencoding == 2) {
		step = 1;
		extra = (trle->s.x + 7) / 8 * trle->s.y;
	}
	else if (trle->subencoding <= 4) {
		step = 2;
		extra = (trle->s.x + 3) / 4 * trle->s.y;
	}
	else {
		step = 4;
		extra = (trle->s.x + 1) / 2 * trle->s.y;
	}

	if (cx->input.wpos < cx->input.rpos + extra) {
		cx->action = trle->packed_palette;
		return 0;
	}

	mask = 0xff >> (8 - step);
	src = &cx->input.data[cx->input.rpos];
	dst = (uint32_t *)trle->unpacked;

	for (y = 0; y < trle->s.y; ++y) {
		shift = 8 - step;
		for (x = 0; x < trle->s.x; ++x) {
			*dst++ = trle->palette[(*src >> shift) & mask];
			shift -= step;
			if (shift < 0) {
				shift = 8 - step;
				++src;
			}
		}
		if (shift != 8 - step)
			++src;
	}

	ggiPutBox(trle->stem,
		trle->p.x, trle->p.y, trle->s.x, trle->s.y,
		trle->unpacked);

	cx->input.rpos += extra;

	if (cx->action == trle->packed_palette) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_plain_rle_8(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;

	debug(3, "trle_plain_rle_8\n");

	do {
		if (cx->input.wpos < cx->input.rpos + 2) {
			cx->action = trle->plain_rle;
			return 0;
		}

		rpos = cx->input.rpos + 1;
		run_length = 0;
		while (cx->input.data[rpos] == 255) {
			run_length += 255;
			if (trle->rle + run_length > trle->s.x * trle->s.y)
				return close_connection(cx, -1);
			if (cx->input.wpos < ++rpos + 1) {
				cx->action = trle->plain_rle;
				return 0;
			}
		}
		run_length += cx->input.data[rpos++] + 1;
		if (trle->rle + run_length > trle->s.x * trle->s.y)
			return close_connection(cx, -1);
		ggiSetGCForeground(trle->stem, cx->input.data[cx->input.rpos]);
		cx->input.rpos = rpos;

		if (trle->rle / trle->s.x ==
				(trle->rle + run_length) / trle->s.x)
		{
			ggiDrawHLine(trle->stem,
				trle->p.x + trle->rle % trle->s.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);
			trle->rle += run_length;
			continue;
		}

		start_x = trle->rle % trle->s.x;
		if (start_x) {
			ggiDrawHLine(trle->stem,
				trle->p.x + start_x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x - start_x);
			trle->rle += trle->s.x - start_x;
			run_length -= trle->s.x - start_x;
		}
		if (run_length > trle->s.x) {
			ggiDrawBox(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x,
				run_length / trle->s.x);
			trle->rle += run_length / trle->s.x * trle->s.x;
			run_length %= trle->s.x;
		}
		if (run_length)
			ggiDrawHLine(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);

		trle->rle += run_length;
	} while (trle->rle < trle->s.x * trle->s.y);
	
	if (cx->action == trle->plain_rle) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_plain_rle_16(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint16_t pixel;

	debug(3, "trle_plain_rle_16\n");

	do {
		if (cx->input.wpos < cx->input.rpos + 3) {
			cx->action = trle->plain_rle;
			return 0;
		}

		rpos = cx->input.rpos + 2;
		run_length = 0;
		while (cx->input.data[rpos] == 255) {
			run_length += 255;
			if (trle->rle + run_length > trle->s.x * trle->s.y)
				return close_connection(cx, -1);
			if (cx->input.wpos < ++rpos + 1) {
				cx->action = trle->plain_rle;
				return 0;
			}
		}
		run_length += cx->input.data[rpos++] + 1;
		if (trle->rle + run_length > trle->s.x * trle->s.y)
			return close_connection(cx, -1);
		if (cx->wire_endian != cx->local_endian)
			pixel = get16_r(&cx->input.data[cx->input.rpos]);
		else
			pixel = get16(&cx->input.data[cx->input.rpos]);
		ggiSetGCForeground(trle->stem, pixel);
		cx->input.rpos = rpos;

		if (trle->rle / trle->s.x ==
			(trle->rle + run_length) / trle->s.x)
		{
			ggiDrawHLine(trle->stem,
				trle->p.x + trle->rle % trle->s.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);
			trle->rle += run_length;
			continue;
		}

		start_x = trle->rle % trle->s.x;
		if (start_x) {
			ggiDrawHLine(trle->stem,
				trle->p.x + start_x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x - start_x);
			trle->rle += trle->s.x - start_x;
			run_length -= trle->s.x - start_x;
		}
		if (run_length > trle->s.x) {
			ggiDrawBox(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x,
				run_length / trle->s.x);
			trle->rle += run_length / trle->s.x * trle->s.x;
			run_length %= trle->s.x;
		}
		if (run_length)
			ggiDrawHLine(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);

		trle->rle += run_length;
	} while (trle->rle < trle->s.x * trle->s.y);
	
	if (cx->action == trle->plain_rle) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_plain_rle_24(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint32_t pixel;

	debug(3, "trle_plain_rle_24\n");

	do {
		if (cx->input.wpos < cx->input.rpos + 4) {
			cx->action = trle->plain_rle;
			return 0;
		}

		rpos = cx->input.rpos + 3;
		run_length = 0;
		while (cx->input.data[rpos] == 255) {
			run_length += 255;
			if (trle->rle + run_length > trle->s.x * trle->s.y)
				return close_connection(cx, -1);
			if (cx->input.wpos < ++rpos + 1) {
				cx->action = trle->plain_rle;
				return 0;
			}
		}
		run_length += cx->input.data[rpos++] + 1;
		if (trle->rle + run_length > trle->s.x * trle->s.y)
			return close_connection(cx, -1);
		pixel = trle->get24(&cx->input.data[cx->input.rpos]);
		ggiSetGCForeground(trle->stem, pixel);
		cx->input.rpos = rpos;

		if (trle->rle / trle->s.x ==
			(trle->rle + run_length) / trle->s.x)
		{
			ggiDrawHLine(trle->stem,
				trle->p.x + trle->rle % trle->s.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);
			trle->rle += run_length;
			continue;
		}

		start_x = trle->rle % trle->s.x;
		if (start_x) {
			ggiDrawHLine(trle->stem,
				trle->p.x + start_x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x - start_x);
			trle->rle += trle->s.x - start_x;
			run_length -= trle->s.x - start_x;
		}
		if (run_length > trle->s.x) {
			ggiDrawBox(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x,
				run_length / trle->s.x);
			trle->rle += run_length / trle->s.x * trle->s.x;
			run_length %= trle->s.x;
		}
		if (run_length)
			ggiDrawHLine(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);

		trle->rle += run_length;
	} while (trle->rle < trle->s.x * trle->s.y);
	
	if (cx->action == trle->plain_rle) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_plain_rle_32(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint32_t pixel;

	debug(3, "trle_plain_rle_32\n");

	do {
		if (cx->input.wpos < cx->input.rpos + 5) {
			cx->action = trle->plain_rle;
			return 0;
		}

		rpos = cx->input.rpos + 4;
		run_length = 0;
		while (cx->input.data[rpos] == 255) {
			run_length += 255;
			if (trle->rle + run_length > trle->s.x * trle->s.y)
				return close_connection(cx, -1);
			if (cx->input.wpos < ++rpos + 1) {
				cx->action = trle->plain_rle;
				return 0;
			}
		}
		run_length += cx->input.data[rpos++] + 1;
		if (trle->rle + run_length > trle->s.x * trle->s.y)
			return close_connection(cx, -1);
		if (cx->wire_endian != cx->local_endian)
			pixel = get32_r(&cx->input.data[cx->input.rpos]);
		else
			pixel = get32(&cx->input.data[cx->input.rpos]);
		ggiSetGCForeground(trle->stem, pixel);
		cx->input.rpos = rpos;

		if (trle->rle / trle->s.x ==
			(trle->rle + run_length) / trle->s.x)
		{
			ggiDrawHLine(trle->stem,
				trle->p.x + trle->rle % trle->s.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);
			trle->rle += run_length;
			continue;
		}

		start_x = trle->rle % trle->s.x;
		if (start_x) {
			ggiDrawHLine(trle->stem,
				trle->p.x + start_x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x - start_x);
			trle->rle += trle->s.x - start_x;
			run_length -= trle->s.x - start_x;
		}
		if (run_length > trle->s.x) {
			ggiDrawBox(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x,
				run_length / trle->s.x);
			trle->rle += run_length / trle->s.x * trle->s.x;
			run_length %= trle->s.x;
		}
		if (run_length)
			ggiDrawHLine(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);

		trle->rle += run_length;
	} while (trle->rle < trle->s.x * trle->s.y);
	
	if (cx->action == trle->plain_rle) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_palette_rle(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int run_length;
	int rpos;
	int start_x;
	uint8_t color;

	debug(3, "trle_palette_rle\n");

	do {
		if (cx->input.wpos < cx->input.rpos + 1) {
			cx->action = trle_palette_rle;
			return 0;
		}

		color = cx->input.data[cx->input.rpos] & 0x7f;
		if (color >= trle->palette_size) {
			debug(1, "trle color %d outside %d\n",
				color, trle->palette_size);
			return close_connection(cx, -1);
		}

		if (!(cx->input.data[cx->input.rpos] & 0x80)) {
			++cx->input.rpos;
			ggiPutPixel(trle->stem,
				trle->p.x + trle->rle % trle->s.x,
				trle->p.y + trle->rle / trle->s.x,
				trle->palette[color]);
			++trle->rle;
			continue;
		}

		if (cx->input.wpos < cx->input.rpos + 2) {
			cx->action = trle_palette_rle;
			return 0;
		}

		rpos = cx->input.rpos + 1;
		run_length = 0;
		while (cx->input.data[rpos] == 255) {
			run_length += 255;
			if (trle->rle + run_length > trle->s.x * trle->s.y)
				return close_connection(cx, -1);
			if (cx->input.wpos < ++rpos + 1) {
				cx->action = trle_palette_rle;
				return 0;
			}
		}
		run_length += cx->input.data[rpos++] + 1;
		if (trle->rle + run_length > trle->s.x * trle->s.y)
			return close_connection(cx, -1);
		ggiSetGCForeground(trle->stem, trle->palette[color]);
		cx->input.rpos = rpos;

		if (trle->rle / trle->s.x ==
			(trle->rle + run_length) / trle->s.x)
		{
			ggiDrawHLine(trle->stem,
				trle->p.x + trle->rle % trle->s.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);
			trle->rle += run_length;
			continue;
		}

		start_x = trle->rle % trle->s.x;
		if (start_x) {
			ggiDrawHLine(trle->stem,
				trle->p.x + start_x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x - start_x);
			trle->rle += trle->s.x - start_x;
			run_length -= trle->s.x - start_x;
		}
		if (run_length > trle->s.x) {
			ggiDrawBox(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				trle->s.x,
				run_length / trle->s.x);
			trle->rle += run_length / trle->s.x * trle->s.x;
			run_length %= trle->s.x;
		}
		if (run_length)
			ggiDrawHLine(trle->stem,
				trle->p.x,
				trle->p.y + trle->rle / trle->s.x,
				run_length);

		trle->rle += run_length;
	} while (trle->rle < trle->s.x * trle->s.y);
	
	if (cx->action == trle_palette_rle) {
		if (!trle_next(cx))
			return trle_done(cx);
		cx->action = trle_tile;
	}
	return 1;
}

static int
trle_palette_8(struct connection *cx, action_t *action)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int i;

	debug(3, "trle_palette_8\n");

	if (cx->input.wpos < cx->input.rpos + trle->palette_size) {
		--cx->input.rpos;
		cx->action = trle_tile;
		return 0;
	}

	for (i = 0; i < trle->palette_size; ++i)
		trle->palette[i] = cx->input.data[cx->input.rpos++];

	return action(cx);
}

static int
trle_palette_16(struct connection *cx, action_t *action)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int i;

	debug(3, "trle_palette_16\n");

	if (cx->input.wpos < cx->input.rpos + 2 * trle->palette_size) {
		--cx->input.rpos;
		cx->action = trle_tile;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		for (i = 0; i < trle->palette_size; ++i) {
			trle->palette[i] =
				get16_r(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
		}
	}
	else {
		for (i = 0; i < trle->palette_size; ++i) {
			trle->palette[i] =
				get16(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
		}
	}

	return action(cx);
}

static int
trle_palette_24(struct connection *cx, action_t *action)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int i;

	debug(3, "trle_palette_24\n");

	if (cx->input.wpos < cx->input.rpos + 3 * trle->palette_size) {
		--cx->input.rpos;
		cx->action = trle_tile;
		return 0;
	}

	for (i = 0; i < trle->palette_size; ++i) {
		trle->palette[i] =
			trle->get24(&cx->input.data[cx->input.rpos]);
		cx->input.rpos += 3;
	}

	return action(cx);
}

static int
trle_palette_32(struct connection *cx, action_t *action)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	int i;

	debug(3, "trle_palette_32\n");

	if (cx->input.wpos < cx->input.rpos + 4 * trle->palette_size) {
		--cx->input.rpos;
		cx->action = trle_tile;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian) {
		for (i = 0; i < trle->palette_size; ++i) {
			trle->palette[i] =
				get32_r(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
		}
	}
	else {
		for (i = 0; i < trle->palette_size; ++i) {
			trle->palette[i] =
				get32(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
		}
	}

	return action(cx);
}

static int
trle_tile(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;

	debug(3, "trle_tile\n");

	do {
		if (cx->input.wpos < cx->input.rpos + 1) {
			cx->action = trle_tile;
			return 0;
		}
		trle->subencoding = cx->input.data[cx->input.rpos++];

		if (trle->subencoding == 0) {
			if (!trle->raw(cx))
				return 0;
			continue;
		}
		if (trle->subencoding == 1) {
			if (!trle->solid(cx))
				return 0;
			continue;
		}
		if (trle->subencoding <= 16) {
			trle->palette_size = trle->subencoding;
			if (!trle->parse_palette(cx, trle->packed_palette))
				return 0;
			continue;
		}
		if (trle->subencoding == 127) {
			trle->subencoding = trle->palette_size;
			if (!trle->packed_palette(cx))
				return 0;
			continue;
		}
		if (trle->subencoding == 128) {
			trle->rle = 0;
			if (!trle->plain_rle(cx))
				return 0;
			continue;
		}
		if (trle->subencoding == 129) {
			trle->rle = 0;
			if (!trle_palette_rle(cx))
				return 0;
			continue;
		}
		if (trle->subencoding >= 130) {
			trle->rle = 0;
			trle->palette_size = trle->subencoding - 128;
			if (!trle->parse_palette(cx, trle_palette_rle))
				return 0;
			continue;
		}
		return close_connection(cx, -1);
	} while (trle_next(cx));

	trle_done(cx);
	return 1;
}

static int
trle_rect(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;
	ggi_pixel mask;
	const ggi_pixelformat *pf;

	debug(2, "trle\n");

	trle->p.x = cx->x + cx->w;
	trle->p.y = cx->y - 16;
	trle_next(cx);

	cx->stem_change = trle_stem_change;
	cx->stem_change(cx);

	switch (GT_SIZE(cx->wire_mode.graphtype)) {
	case  8:
		trle->raw            = trle_raw_8;
		trle->solid          = trle_solid_8;
		trle->packed_palette = trle_packed_palette_8;
		trle->plain_rle      = trle_plain_rle_8;
		trle->parse_palette  = trle_palette_8;
		break;
	case 16:
		trle->raw            = trle_raw_16;
		trle->solid          = trle_solid_16;
		trle->packed_palette = trle_packed_palette_16;
		trle->plain_rle      = trle_plain_rle_16;
		trle->parse_palette  = trle_palette_16;
		break;
	case 32:
		pf = ggiGetPixelFormat(trle->stem);
		mask = pf->red_mask | pf->green_mask | pf->blue_mask;
		if (!(mask & 0xff000000)) {
			if (cx->wire_endian != cx->local_endian) {
				if (cx->wire_endian)
					trle->get24 = get24bl_r;
				else
					trle->get24 = get24ll_r;
			}
			else {
				if (cx->wire_endian)
					trle->get24 = get24bl;
				else
					trle->get24 = get24ll;
			}
			trle->raw            = trle_raw_24;
			trle->solid          = trle_solid_24;
			trle->plain_rle      = trle_plain_rle_24;
			trle->parse_palette  = trle_palette_24;
		}
		else if (!(mask & 0xff)) {
			if (cx->wire_endian != cx->local_endian) {
				if (cx->wire_endian)
					trle->get24 = get24bh_r;
				else
					trle->get24 = get24lh_r;
			}
			else {
				if (cx->wire_endian)
					trle->get24 = get24bh;
				else
					trle->get24 = get24lh;
			}
			trle->raw            = trle_raw_24;
			trle->solid          = trle_solid_24;
			trle->plain_rle      = trle_plain_rle_24;
			trle->parse_palette  = trle_palette_24;
		}
		else {
			trle->raw            = trle_raw_32;
			trle->solid          = trle_solid_32;
			trle->plain_rle      = trle_plain_rle_32;
			trle->parse_palette  = trle_palette_32;
		}
		trle->packed_palette = trle_packed_palette_32;
		break;
	}
	cx->action = trle_tile;
	return 1;
}

static void
trle_end(struct connection *cx)
{
	struct trle *trle = cx->encoding_def[trle_encoding].priv;

	if (!trle)
		return;

	debug(1, "trle_end\n");

	if (trle->unpacked)
		free(trle->unpacked);

	free(cx->encoding_def[trle_encoding].priv);
	cx->encoding_def[trle_encoding].priv = NULL;
	cx->encoding_def[trle_encoding].action = vnc_trle;
}

int
vnc_trle(struct connection *cx)
{
	struct trle *trle;

	trle = malloc(sizeof(*trle));
	if (!trle)
		return close_connection(cx, -1);
	memset(trle, 0, sizeof(*trle));

	cx->encoding_def[trle_encoding].priv = trle;
	cx->encoding_def[trle_encoding].end = trle_end;

	trle->unpacked = malloc(4 * 16 * 16);
	if (!trle->unpacked)
		return close_connection(cx, -1);

	cx->action = cx->encoding_def[trle_encoding].action = trle_rect;
	return cx->action(cx);
}
