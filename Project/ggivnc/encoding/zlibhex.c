/*
******************************************************************************

   VNC viewer ZlibHex encoding.

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
#include <zlib.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

struct zlibhex_t {
	z_stream zstr[2];
	z_stream *ztream;
	uint16_t length;
	action_t *hextile;
	uint8_t subencoding;
	int bpp;
};

static struct zlibhex_t zhex;

static int vnc_zlibhex_tile(void);

static int
vnc_zlibhex_inflate(void)
{
	uint16_t length;
	int res;
	int flush;
	struct buffer input;

	if (g.input.wpos < g.input.rpos + zhex.length) {
		length = g.input.wpos - g.input.rpos;
		flush = Z_NO_FLUSH;
	}
	else {
		length = zhex.length;
		flush = Z_SYNC_FLUSH;
	}

	zhex.ztream->avail_in = length;
	zhex.ztream->next_in = &g.input.data[g.input.rpos];

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
		zhex.ztream->avail_out = g.work.size - g.work.wpos;
		zhex.ztream->next_out = &g.work.data[g.work.wpos];

		res = inflate(zhex.ztream, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			exit(1);
		}

		g.work.wpos = g.work.size - zhex.ztream->avail_out;
	} while (!zhex.ztream->avail_out);

	zhex.length -= length - zhex.ztream->avail_in;
	g.input.rpos += length - zhex.ztream->avail_in;

	if (zhex.length) {
		g.action = vnc_zlibhex_inflate;
		return 0;
	}

	input = g.input;
	g.input = g.work;
	g.action = zhex.hextile;
	g.action();
	g.input = input;

	g.work.rpos = 0;
	g.work.wpos = 0;

	if (zhex.hextile != g.action) {
		if (g.action != vnc_update_rect) {
			debug(1, "Aiee, bad bad bad\n");
			exit(1);
		}
		remove_dead_data();
		return 1;
	}

	g.action = vnc_zlibhex_tile;
	return 1;
}

static int
vnc_zlibhex_tile(void)
{
	int tile_size;

next_tile:
	if (g.input.wpos < g.input.rpos + 1) {
		g.action = vnc_zlibhex_tile;
		return 0;
	}

	zhex.subencoding = g.input.data[g.input.rpos];
	if (zhex.subencoding >= 0x60) {
		debug(1, "zlibhex subencoding %02x\n", zhex.subencoding);
		exit(1);
	}

	if (!(zhex.subencoding & 0x60)) {
		int rpos;
		int wpos;
		int size;

		tile_size = vnc_hextile_size(zhex.bpp);
		if (!tile_size) {
			g.action = vnc_zlibhex_tile;
			return 0;
		}
		if (g.input.wpos < g.input.rpos + tile_size) {
			g.action = vnc_zlibhex_tile;
			return 0;
		}
		rpos = g.input.rpos;
		wpos = g.input.wpos;
		size = g.input.size;
		g.input.wpos = g.input.rpos + tile_size;
		g.input.size = g.input.rpos + tile_size;
		g.action = zhex.hextile;
		g.action();
		if (zhex.hextile != g.action) {
			if (g.action != vnc_update_rect) {
				debug(1, "Aiee, bad bad bad\n");
				exit(1);
			}
			if (g.input.wpos != 0 || g.input.rpos != 0) {
				debug(1, "Aiee, bad bad bad bad\n");
				exit(1);
			}
			g.input.rpos = rpos + tile_size;
			g.input.wpos = wpos;
			g.input.size = size;
			remove_dead_data();
			return 1;
		}
		g.input.wpos = wpos;
		g.input.size = size;

		g.action = vnc_zlibhex_tile;
		goto next_tile;
	}

	if (g.input.wpos < g.input.rpos + 3) {
		g.action = vnc_zlibhex_tile;
		return 0;
	}
	++g.input.rpos;

	if (zhex.subencoding & 0x20)
		zhex.ztream = &zhex.zstr[0];
	else if (zhex.subencoding & 0x40)
		zhex.ztream = &zhex.zstr[1];

	zhex.length = get16_hilo(&g.input.data[g.input.rpos]);
	g.input.rpos += 2;
	if (zhex.subencoding & 0x20)
		zhex.subencoding |= 1;
	g.work.data[g.work.wpos++] = zhex.subencoding & 0x1f;

	return vnc_zlibhex_inflate();
}

int
vnc_zlibhex(void)
{
	int wpos;
	debug(2, "zlibhex\n");

	zhex.bpp = GT_SIZE(g.wire_mode.graphtype) / 8;
	zhex.hextile = vnc_hextile;
	g.action = vnc_hextile;
	wpos = g.input.wpos;
	g.input.wpos = g.input.rpos;
	g.action();
	zhex.hextile = g.action;
	g.input.wpos = wpos;

	return vnc_zlibhex_tile();
}

int
vnc_zlibhex_init(void)
{
	zhex.zstr[0].zalloc = Z_NULL;
	zhex.zstr[0].zfree = Z_NULL;
	zhex.zstr[0].opaque = Z_NULL;
	zhex.zstr[0].avail_in = 0;
	zhex.zstr[0].next_in = Z_NULL;
	zhex.zstr[0].avail_out = 0;

	zhex.zstr[1].zalloc = Z_NULL;
	zhex.zstr[1].zfree = Z_NULL;
	zhex.zstr[1].opaque = Z_NULL;
	zhex.zstr[1].avail_in = 0;
	zhex.zstr[1].next_in = Z_NULL;
	zhex.zstr[1].avail_out = 0;

	if (inflateInit(&zhex.zstr[0]) != Z_OK)
		return -1;

	if (inflateInit(&zhex.zstr[1]) != Z_OK) {
		inflateEnd(&zhex.zstr[0]);
		return -1;
	}

	return 0;
}
