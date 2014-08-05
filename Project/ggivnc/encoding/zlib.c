/*
******************************************************************************

   VNC viewer Zlib encoding.

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

struct zlib_t {
	z_stream zstr;
	uint32_t length;
};

static struct zlib_t zlib;

static int
vnc_inflate(void)
{
	int length;
	int res;
	int flush;
	const uint32_t max_length = 0x100000;
	int chunked;
	struct buffer input;

	debug(3, "zlib_inflate\n");

	chunked = zlib.length > max_length;
	length = chunked ? max_length : zlib.length;

	if (g.input.wpos < g.input.rpos + length) {
		length = g.input.wpos - g.input.rpos;
		chunked = 0;
		flush = Z_NO_FLUSH;
	}
	else
		flush = chunked ? Z_NO_FLUSH : Z_SYNC_FLUSH;

	zlib.zstr.avail_in = length;
	zlib.zstr.next_in = &g.input.data[g.input.rpos];

	do {
		if (g.work.wpos == g.work.size) {
			void *tmp;
			g.work.size += 65536;
			tmp = realloc(g.work.data, g.work.size);
			if (!tmp) {
				debug(1, "zlib realloc failed\n");
				free(g.work.data);
				g.work.data = NULL;
				exit(1);
			}
			g.work.data = tmp;
		}
		zlib.zstr.avail_out = g.work.size - g.work.wpos;
		zlib.zstr.next_out = &g.work.data[g.work.wpos];

		res = inflate(&zlib.zstr, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			debug(1, "zlib inflate error %d\n", res);
			inflateEnd(&zlib.zstr);
			exit(1);
		}

		g.work.wpos = g.work.size - zlib.zstr.avail_out;
	} while (!zlib.zstr.avail_out);

	zlib.length -= length - zlib.zstr.avail_in;
	g.input.rpos += length - zlib.zstr.avail_in;

	if (zlib.length) {
		g.action = vnc_inflate;
		return chunked;
	}

	input = g.input;
	g.input = g.work;
	if (!vnc_raw())
		exit(1);
	g.input = input;

	g.work.rpos = 0;
	g.work.wpos = 0;

	return 1;
}

int
vnc_zlib(void)
{
	debug(2, "zlib\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	zlib.length = get32_hilo(&g.input.data[g.input.rpos]);
	g.input.rpos += 4;

	return vnc_inflate();
}

int
vnc_zlib_init(void)
{
	zlib.zstr.zalloc = Z_NULL;
	zlib.zstr.zfree = Z_NULL;
	zlib.zstr.opaque = Z_NULL;
	zlib.zstr.avail_in = 0;
	zlib.zstr.next_in = Z_NULL;
	zlib.zstr.avail_out = 0;

	if (inflateInit(&zlib.zstr) != Z_OK)
		return -1;

	return 0;
}
