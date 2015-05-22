/*
******************************************************************************

   VNC viewer ZlibHex encoding.

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

struct zlibhex {
	z_stream zstr[2];
	z_stream *ztream;
	uint16_t length;
	action_t *hextile;
	uint8_t subencoding;
	int bpp;
};

static int vnc_zlibhex_tile(struct connection *cx);

static int
vnc_zlibhex_inflate(struct connection *cx)
{
	struct zlibhex *zhex = cx->encoding_def[zlibhex_encoding].priv;
	uint16_t length;
	int res;
	int flush;
	struct buffer input;

	if (cx->input.wpos < cx->input.rpos + zhex->length) {
		length = cx->input.wpos - cx->input.rpos;
		flush = Z_NO_FLUSH;
	}
	else {
		length = zhex->length;
		flush = Z_SYNC_FLUSH;
	}

	zhex->ztream->avail_in = length;
	zhex->ztream->next_in = &cx->input.data[cx->input.rpos];

	do {
		if (cx->work.wpos == cx->work.size) {
			if (buffer_reserve(&cx->work,
				cx->work.size + 65536))
			{
				debug(1, "zlibhex realloc failed\n");
				return close_connection(cx, -1);
			}
		}
		zhex->ztream->avail_out = cx->work.size - cx->work.wpos;
		zhex->ztream->next_out = &cx->work.data[cx->work.wpos];

		res = inflate(zhex->ztream, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			debug(1, "zlibhex inflate error %d\n", res);
			inflateEnd(zhex->ztream);
			return close_connection(cx, -1);
		}

		cx->work.wpos = cx->work.size - zhex->ztream->avail_out;
	} while (!zhex->ztream->avail_out);

	zhex->length -= length - zhex->ztream->avail_in;
	cx->input.rpos += length - zhex->ztream->avail_in;

	if (zhex->length) {
		cx->action = vnc_zlibhex_inflate;
		return 0;
	}

	input = cx->input;
	cx->input = cx->work;
	cx->action = zhex->hextile;
	cx->action(cx);
	cx->input = input;

	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (zhex->hextile != cx->action) {
		if (cx->action != vnc_update_rect) {
			debug(1, "Aiee, bad bad bad\n");
			return close_connection(cx, -1);
		}
		remove_dead_data(&cx->input);
		return 1;
	}

	cx->action = vnc_zlibhex_tile;
	return 1;
}

static int
vnc_zlibhex_tile(struct connection *cx)
{
	struct zlibhex *zhex = cx->encoding_def[zlibhex_encoding].priv;
	int tile_size;

next_tile:
	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = vnc_zlibhex_tile;
		return 0;
	}

	zhex->subencoding = cx->input.data[cx->input.rpos];
	if (zhex->subencoding >= 0x60) {
		debug(1, "zlibhex subencoding %02x\n", zhex->subencoding);
		return close_connection(cx, -1);
	}

	if (!(zhex->subencoding & 0x60)) {
		int rpos;
		int wpos;
		int size;

		tile_size = vnc_hextile_size(cx, zhex->bpp);
		if (!tile_size) {
			cx->action = vnc_zlibhex_tile;
			return 0;
		}
		if (cx->input.wpos < cx->input.rpos + tile_size) {
			cx->action = vnc_zlibhex_tile;
			return 0;
		}
		rpos = cx->input.rpos;
		wpos = cx->input.wpos;
		size = cx->input.size;
		cx->input.wpos = cx->input.rpos + tile_size;
		cx->input.size = cx->input.rpos + tile_size;
		cx->action = zhex->hextile;
		cx->action(cx);
		if (zhex->hextile != cx->action) {
			if (cx->action != vnc_update_rect) {
				debug(1, "Aiee, bad bad bad\n");
				return close_connection(cx, -1);
			}
			if (cx->input.wpos != 0 || cx->input.rpos != 0) {
				debug(1, "Aiee, bad bad bad bad\n");
				return close_connection(cx, -1);
			}
			cx->input.rpos = rpos + tile_size;
			cx->input.wpos = wpos;
			cx->input.size = size;
			remove_dead_data(&cx->input);
			return 1;
		}
		cx->input.wpos = wpos;
		cx->input.size = size;

		cx->action = vnc_zlibhex_tile;
		goto next_tile;
	}

	if (cx->input.wpos < cx->input.rpos + 3) {
		cx->action = vnc_zlibhex_tile;
		return 0;
	}
	++cx->input.rpos;

	if (zhex->subencoding & 0x20)
		zhex->ztream = &zhex->zstr[0];
	else if (zhex->subencoding & 0x40)
		zhex->ztream = &zhex->zstr[1];

	zhex->length = get16_hilo(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 2;
	if (zhex->subencoding & 0x20)
		zhex->subencoding |= 1;
	cx->work.data[cx->work.wpos++] = zhex->subencoding & 0x1f;

	return vnc_zlibhex_inflate(cx);
}

static int
zlibhex_rect(struct connection *cx)
{
	struct zlibhex *zhex = cx->encoding_def[zlibhex_encoding].priv;
	int wpos;
	debug(2, "zlibhex\n");

	zhex->bpp = GT_SIZE(cx->wire_mode.graphtype) / 8;
	cx->action = zhex->hextile =
		cx->encoding_def[hextile_encoding].action;
	wpos = cx->input.wpos;
	cx->input.wpos = cx->input.rpos;
	cx->action(cx);
	if (cx->close_connection)
		return 0;
	zhex->hextile = cx->action;
	cx->input.wpos = wpos;

	return vnc_zlibhex_tile(cx);
}

static void
zlibhex_end(struct connection *cx)
{
	struct zlibhex *zhex = cx->encoding_def[zlibhex_encoding].priv;

	if (!zhex)
		return;

	debug(1, "zlibhex_end\n");

	inflateEnd(&zhex->zstr[1]);
	inflateEnd(&zhex->zstr[0]);

	free(cx->encoding_def[zlibhex_encoding].priv);
	cx->encoding_def[zlibhex_encoding].priv = NULL;
	cx->encoding_def[zlibhex_encoding].action = vnc_zlibhex;
}

int
vnc_zlibhex(struct connection *cx)
{
	struct zlibhex *zhex;

	zhex = malloc(sizeof(*zhex));
	if (!zhex)
		return close_connection(cx, -1);
	memset(zhex, 0, sizeof(*zhex));

	cx->encoding_def[zlibhex_encoding].priv = zhex;
	cx->encoding_def[zlibhex_encoding].end = zlibhex_end;

	zhex->zstr[0].zalloc = Z_NULL;
	zhex->zstr[0].zfree = Z_NULL;
	zhex->zstr[0].opaque = Z_NULL;
	zhex->zstr[0].avail_in = 0;
	zhex->zstr[0].next_in = Z_NULL;
	zhex->zstr[0].avail_out = 0;

	zhex->zstr[1].zalloc = Z_NULL;
	zhex->zstr[1].zfree = Z_NULL;
	zhex->zstr[1].opaque = Z_NULL;
	zhex->zstr[1].avail_in = 0;
	zhex->zstr[1].next_in = Z_NULL;
	zhex->zstr[1].avail_out = 0;

	if (inflateInit(&zhex->zstr[0]) != Z_OK)
		return close_connection(cx, -1);

	if (inflateInit(&zhex->zstr[1]) != Z_OK)
		return close_connection(cx, -1);

	cx->action = cx->encoding_def[zlibhex_encoding].action = zlibhex_rect;
	return cx->action(cx);
}
