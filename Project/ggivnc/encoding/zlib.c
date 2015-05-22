/*
******************************************************************************

   VNC viewer Zlib encoding.

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

struct zlib {
	z_stream zstr;
	uint32_t length;
};

static int
vnc_inflate(struct connection *cx)
{
	struct zlib *zlib = cx->encoding_def[zlib_encoding].priv;

	int length;
	int res;
	int flush;
	const uint32_t max_length = 0x100000;
	int chunked;
	struct buffer input;

	debug(3, "zlib_inflate\n");

	chunked = zlib->length > max_length;
	length = chunked ? max_length : zlib->length;

	if (cx->input.wpos < cx->input.rpos + length) {
		length = cx->input.wpos - cx->input.rpos;
		chunked = 0;
		flush = Z_NO_FLUSH;
	}
	else
		flush = chunked ? Z_NO_FLUSH : Z_SYNC_FLUSH;

	zlib->zstr.avail_in = length;
	zlib->zstr.next_in = &cx->input.data[cx->input.rpos];

	do {
		if (cx->work.wpos == cx->work.size) {
			if (buffer_reserve(&cx->work,
				cx->work.size + 65536))
			{
				debug(1, "zlib realloc failed\n");
				return close_connection(cx, -1);
			}
		}
		zlib->zstr.avail_out = cx->work.size - cx->work.wpos;
		zlib->zstr.next_out = &cx->work.data[cx->work.wpos];

		res = inflate(&zlib->zstr, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			debug(1, "zlib inflate error %d\n", res);
			inflateEnd(&zlib->zstr);
			return close_connection(cx, -1);
		}

		cx->work.wpos = cx->work.size - zlib->zstr.avail_out;
	} while (!zlib->zstr.avail_out);

	zlib->length -= length - zlib->zstr.avail_in;
	cx->input.rpos += length - zlib->zstr.avail_in;

	if (zlib->length) {
		cx->action = vnc_inflate;
		return chunked;
	}

	input = cx->input;
	cx->input = cx->work;
	res = vnc_raw(cx);
	cx->input = input;
	if (!res)
		return close_connection(cx, -1);

	cx->work.rpos = 0;
	cx->work.wpos = 0;

	return 1;
}

static int
zlib_rect(struct connection *cx)
{
	struct zlib *zlib = cx->encoding_def[zlib_encoding].priv;

	debug(2, "zlib\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	zlib->length = get32_hilo(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 4;

	return vnc_inflate(cx);
}

static void
zlib_end(struct connection *cx)
{
	struct zlib *zlib = cx->encoding_def[zlib_encoding].priv;

	if (!zlib)
		return;

	debug(1, "zlib_end\n");

	inflateEnd(&zlib->zstr);

	free(cx->encoding_def[zlib_encoding].priv);
	cx->encoding_def[zlib_encoding].priv = NULL;
	cx->encoding_def[zlib_encoding].action = vnc_zlib;
}

int
vnc_zlib(struct connection *cx)
{
	struct zlib *zlib;

	zlib = malloc(sizeof(*zlib));
	if (!zlib)
		return close_connection(cx, -1);
	memset(zlib, 0, sizeof(*zlib));

	cx->encoding_def[zlib_encoding].priv = zlib;
	cx->encoding_def[zlib_encoding].end = zlib_end;

	zlib->zstr.zalloc = Z_NULL;
	zlib->zstr.zfree = Z_NULL;
	zlib->zstr.opaque = Z_NULL;
	zlib->zstr.avail_in = 0;
	zlib->zstr.next_in = Z_NULL;
	zlib->zstr.avail_out = 0;

	if (inflateInit(&zlib->zstr) != Z_OK)
		return close_connection(cx, -1);

	cx->action = cx->encoding_def[zlib_encoding].action = zlib_rect;
	return cx->action(cx);
}
