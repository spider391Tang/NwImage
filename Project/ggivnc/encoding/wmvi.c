/*
******************************************************************************

   VNC viewer WMVi encoding.

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

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

int
vnc_wmvi(struct connection *cx)
{
	uint8_t *sinit;
	uint16_t red_max;
	uint16_t green_max;
	uint16_t blue_max;
	ggi_pixelformat server_ggi_pf;
	char pixfmt[30];
	int endian;
	ggi_coord wire_size;

	debug(2, "wmvi %dx%d\n", cx->w, cx->h);

	if (cx->input.wpos < cx->input.rpos + 16) {
		cx->action = vnc_wmvi;
		return 0;
	}

	sinit = &cx->input.data[cx->input.rpos];

	red_max = get16_hilo(&sinit[4]);
	green_max = get16_hilo(&sinit[6]);
	blue_max = get16_hilo(&sinit[8]);

	memset(&server_ggi_pf, 0, sizeof(server_ggi_pf));
	server_ggi_pf.size = sinit[0];
	if (sinit[3]) {
		server_ggi_pf.depth = color_bits(red_max) +
			color_bits(green_max) +
			color_bits(blue_max);
		if (server_ggi_pf.size < server_ggi_pf.depth)
			server_ggi_pf.depth = sinit[1];
		server_ggi_pf.red_mask = red_max << sinit[10];
		server_ggi_pf.green_mask = green_max << sinit[11];
		server_ggi_pf.blue_mask = blue_max << sinit[12];
	}
	else {
		server_ggi_pf.depth = sinit[1];
		server_ggi_pf.clut_mask = (1 << server_ggi_pf.depth) - 1;
	}
	endian = !!sinit[6];

	generate_pixfmt(pixfmt, sizeof(pixfmt), &server_ggi_pf);
	if (!strcmp(pixfmt, "weird"))
		return close_connection(cx, -1);

	wire_size.x = cx->w;
	wire_size.y = cx->h;

	cx->desktop_size = 1;
	if (wire_mode_switch(cx, pixfmt, endian, wire_size) < 0)
		return close_connection(cx, -1);

	cx->input.rpos += 16;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}
