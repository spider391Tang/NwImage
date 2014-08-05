/*
******************************************************************************

   VNC viewer Raw encoding.

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

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

int
vnc_raw(void)
{
	int bytes;
	ggi_visual_t stem;
	int bpp;

	debug(2, "raw\n");

	stem = g.wire_stem ? g.wire_stem : g.stem;
	bpp = GT_SIZE(g.wire_mode.graphtype) / 8;

	bytes = bpp * g.w * g.h;

	if (g.input.wpos < g.input.rpos + bytes)
		return 0;

	if (g.wire_endian != g.local_endian) {
		/* Should be handled by a crossblit, but that's not
		 * supported by libggi. Yet...
		 */
		ggi_mode mode;
		ggiGetMode(stem, &mode);

		switch (GT_SIZE(mode.graphtype)) {
		case 16:
			buffer_reverse_16(g.input.data + g.input.rpos, bytes);
			break;
		case 32:
			buffer_reverse_32(g.input.data + g.input.rpos, bytes);
			break;
		}
	}

	ggiPutBox(stem, g.x, g.y, g.w, g.h, g.input.data + g.input.rpos);

	--g.rects;
	g.input.rpos += bytes;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}
