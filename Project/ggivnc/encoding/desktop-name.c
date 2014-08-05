/*
******************************************************************************

   VNC viewer DesktopName encoding.

   Copyright (C) 2008 Peter Rosin  [peda@lysator.liu.se]

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

#include "vnc.h"
#include "vnc-debug.h"
#include "vnc-endian.h"

static int
drain_desktop_name(void)
{
	const int max_chunk = 0x100000;
	uint32_t rest;
	uint32_t limit;
	int chunk;

	debug(2, "drain-desktop-name\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	rest = get32_hilo(&g.input.data[g.input.rpos]);

	limit = max_chunk;
	if (g.input.wpos - (g.input.rpos + 4) < max_chunk)
		limit = g.input.wpos - (g.input.rpos + 4);

	chunk = rest < limit ? rest : limit;

	if (rest > limit) {
		g.input.rpos += chunk;
		remove_dead_data();
		rest -= limit;
		g.input.data[0] = rest >> 24;
		g.input.data[1] = rest >> 16;
		g.input.data[2] = rest >> 8;
		g.input.data[3] = rest;
		return chunk > 0;
	}

	g.input.rpos += 4 + chunk;

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

int
vnc_desktop_name(void)
{
	const uint32_t max_length = 2000;
	uint32_t name_length;
	int length;

	debug(2, "desktop-name\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	name_length = get32_hilo(&g.input.data[g.input.rpos]);
	length = name_length < max_length ? name_length : max_length;

	if (g.input.wpos < g.input.rpos + 4 + length)
		return 0;

	if (g.name)
		free(g.name);
	g.name = NULL;

	if (!length)
		goto done;

	g.name = malloc(length + 1);
	if (!g.name)
		goto done; /* FIXME: Silent failure... */

	memcpy(g.name, &g.input.data[g.input.rpos + 4], length);
	g.name[length] = '\0';

done:
	set_title();
	
	if (name_length > max_length) {
		g.input.rpos += length;
		remove_dead_data();
		name_length -= max_length;
		g.input.data[0] = name_length >> 24;
		g.input.data[1] = name_length >> 16;
		g.input.data[2] = name_length >> 8;
		g.input.data[3] = name_length;
		g.action = drain_desktop_name;
		return 1;
	}

	g.input.rpos += 4 + length;

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}
