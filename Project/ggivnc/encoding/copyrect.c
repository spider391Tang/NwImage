/*
******************************************************************************

   VNC viewer CopyRect encoding.

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
#include <ggi/ggi.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

int
vnc_copyrect(struct connection *cx)
{
	uint16_t x, y;

	debug(2, "copyrect\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	x = get16_hilo(&cx->input.data[cx->input.rpos + 0]);
	y = get16_hilo(&cx->input.data[cx->input.rpos + 2]);

	if (cx->wire_stem)
		ggiCopyBox(cx->wire_stem, x, y, cx->w, cx->h, cx->x, cx->y);
	else
		ggiCopyBox(cx->stem, x, y, cx->w, cx->h, cx->x, cx->y);

	--cx->rects;
	cx->input.rpos += 4;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}
