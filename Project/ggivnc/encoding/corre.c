/*
******************************************************************************

   VNC viewer CoRRE encoding.

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

struct corre {
	int32_t rects;
	ggi_visual_t stem;
};

static int
corre_stem_change(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;

	corre->stem = cx->wire_stem ? cx->wire_stem : cx->stem;
	return 0;
}

static int
vnc_corre_rect_8(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (corre->rects) {
		if (cx->input.wpos < cx->input.rpos + 5) {
			cx->action = vnc_corre_rect_8;
			return 0;
		}

		pixel = cx->input.data[cx->input.rpos++];
		x = cx->input.data[cx->input.rpos++];
		y = cx->input.data[cx->input.rpos++];
		w = cx->input.data[cx->input.rpos++];
		h = cx->input.data[cx->input.rpos++];

		ggiSetGCForeground(corre->stem, pixel);
		ggiDrawBox(corre->stem, cx->x + x, cx->y + y, w, h);
		--corre->rects;
	}

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_corre_8(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;
	ggi_pixel pixel;

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = vnc_corre_8;
		return 0;
	}

	pixel = cx->input.data[cx->input.rpos++];

	ggiSetGCForeground(corre->stem, pixel);
	ggiDrawBox(corre->stem, cx->x, cx->y, cx->w, cx->h);

	return vnc_corre_rect_8(cx);
}

static int
vnc_corre_rect_16(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (corre->rects) {
		if (cx->input.wpos < cx->input.rpos + 6) {
			cx->action = vnc_corre_rect_16;
			return 0;
		}

		if (cx->wire_endian != cx->local_endian)
			pixel = get16_r(&cx->input.data[cx->input.rpos]);
		else
			pixel = get16(&cx->input.data[cx->input.rpos]);
		cx->input.rpos += 2;
		x = cx->input.data[cx->input.rpos++];
		y = cx->input.data[cx->input.rpos++];
		w = cx->input.data[cx->input.rpos++];
		h = cx->input.data[cx->input.rpos++];

		ggiSetGCForeground(corre->stem, pixel);
		ggiDrawBox(corre->stem, cx->x + x, cx->y + y, w, h);
		--corre->rects;
	}

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_corre_16(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;
	ggi_pixel pixel;

	if (cx->input.wpos < cx->input.rpos + 2) {
		cx->action = vnc_corre_16;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get16_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get16(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 2;

	ggiSetGCForeground(corre->stem, pixel);
	ggiDrawBox(corre->stem, cx->x, cx->y, cx->w, cx->h);

	return vnc_corre_rect_16(cx);
}

static int
vnc_corre_rect_32(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;
	ggi_pixel pixel;
	uint16_t x, y, w, h;

	while (corre->rects) {
		if (cx->input.wpos < cx->input.rpos + 8) {
			cx->action = vnc_corre_rect_32;
			return 0;
		}

		if (cx->wire_endian != cx->local_endian)
			pixel = get32_r(&cx->input.data[cx->input.rpos]);
		else
			pixel = get32(&cx->input.data[cx->input.rpos]);
		cx->input.rpos += 4;
		x = cx->input.data[cx->input.rpos++];
		y = cx->input.data[cx->input.rpos++];
		w = cx->input.data[cx->input.rpos++];
		h = cx->input.data[cx->input.rpos++];

		ggiSetGCForeground(corre->stem, pixel);
		ggiDrawBox(corre->stem, cx->x + x, cx->y + y, w, h);
		--corre->rects;
	}

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_corre_32(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;
	ggi_pixel pixel;

	if (cx->input.wpos < cx->input.rpos + 4) {
		cx->action = vnc_corre_32;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get32_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get32(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 4;

	ggiSetGCForeground(corre->stem, pixel);
	ggiDrawBox(corre->stem, cx->x, cx->y, cx->w, cx->h);

	return vnc_corre_rect_32(cx);
}

static int
corre_rect(struct connection *cx)
{
	struct corre *corre = cx->encoding_def[corre_encoding].priv;

	debug(2, "corre\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	corre->rects = get32_hilo(&cx->input.data[cx->input.rpos]);

	cx->input.rpos += 4;

	cx->stem_change = corre_stem_change;
	cx->stem_change(cx);

	switch (GT_SIZE(cx->wire_mode.graphtype)) {
	case  8: return vnc_corre_8(cx);
	case 16: return vnc_corre_16(cx);
	case 32: return vnc_corre_32(cx);
	}
	return 1;
}

static void
corre_end(struct connection *cx)
{
	if (!cx->encoding_def[corre_encoding].priv)
		return;

	debug(1, "corre_end\n");

	free(cx->encoding_def[corre_encoding].priv);
	cx->encoding_def[corre_encoding].priv = NULL;
	cx->encoding_def[corre_encoding].action = vnc_corre;
}

int
vnc_corre(struct connection *cx)
{
	struct corre *corre;

	corre = malloc(sizeof(*corre));
	if (!corre)
		return close_connection(cx, -1);
	memset(corre, 0, sizeof(*corre));

	cx->encoding_def[corre_encoding].priv = corre;
	cx->encoding_def[corre_encoding].end = corre_end;

	cx->action = cx->encoding_def[corre_encoding].action = corre_rect;
	return cx->action(cx);
}
