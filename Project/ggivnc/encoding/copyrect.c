/*
******************************************************************************

   VNC viewer CopyRect encoding.

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
vnc_copyrect(void)
{
	uint16_t x, y;

	debug(2, "copyrect\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	x = get16_hilo(&g.input.data[g.input.rpos + 0]);
	y = get16_hilo(&g.input.data[g.input.rpos + 2]);

	if (g.wire_stem)
		ggiCopyBox(g.wire_stem, x, y, g.w, g.h, g.x, g.y);
	else
		ggiCopyBox(g.stem, x, y, g.w, g.h, g.x, g.y);

	--g.rects;
	g.input.rpos += 4;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}
