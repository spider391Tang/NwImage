/*
******************************************************************************

   VNC viewer DesktopName encoding.

   The MIT License

   Copyright (C) 2008-2010 Peter Rosin  [peda@lysator.liu.se]

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

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#include "vnc.h"
#include "vnc-compat.h"
#include "vnc-debug.h"
#include "vnc-endian.h"

static int
drain_desktop_name(struct connection *cx)
{
	const int max_chunk = 0x100000;
	uint32_t rest;
	uint32_t limit;
	int chunk;

	debug(2, "drain-desktop-name\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	rest = get32_hilo(&cx->input.data[cx->input.rpos]);

	limit = max_chunk;
	if (cx->input.wpos - (cx->input.rpos + 4) < max_chunk)
		limit = cx->input.wpos - (cx->input.rpos + 4);

	chunk = rest < limit ? rest : limit;

	if (rest > limit) {
		cx->input.rpos += chunk;
		remove_dead_data(&cx->input);
		rest -= limit;
		insert32_hilo(cx->input.data, rest);
		return chunk > 0;
	}

	cx->input.rpos += 4 + chunk;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

int
vnc_desktop_name(struct connection *cx)
{
	const uint32_t max_length = 2000;
	uint32_t name_length;
	int length;
	iconv_t cd;
	ICONV_CONST char *in;
	size_t inlen;
	char *out;
	size_t outlen;

	debug(2, "desktop-name\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	name_length = get32_hilo(&cx->input.data[cx->input.rpos]);
	length = name_length < max_length ? name_length : max_length;

	if (cx->input.wpos < cx->input.rpos + 4 + length)
		return 0;

	if (cx->name)
		free(cx->name);
	cx->name = NULL;

	if (!length)
		goto done;

	cx->name = malloc(length + 1);
	if (!cx->name)
		goto late_failure;

	cd = iconv_open("US-ASCII", "UTF-8");
	if (!cd)
		goto late_failure;
	in = (ICONV_CONST char *)&cx->input.data[cx->input.rpos + 4];
	inlen = length;
	out = cx->name;
	outlen = length;
	iconv(cd, &in, &inlen, &out, &outlen);
	iconv_close(cd);
	*out = '\0';

	if (!cx->name[0]) {
late_failure:
		/* FIXME: Silent failure... */
		if (cx->name)
			free(cx->name);
		cx->name = NULL;
	}
done:
	set_title(cx);
	
	if (name_length > max_length) {
		cx->input.rpos += length;
		remove_dead_data(&cx->input);
		name_length -= max_length;
		insert32_hilo(cx->input.data, name_length);
		cx->action = drain_desktop_name;
		return 1;
	}

	cx->input.rpos += 4 + length;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}
