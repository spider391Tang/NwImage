/*
******************************************************************************

   Safe ggiCrossBlit for ggivnc, the ggiCrossBlit from GGI 2.x does not
   clip against the source visual.

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

#include "vnc.h"
#include "vnc-compat.h"

int
ggiCrossBlit(ggi_visual_t src, int sx, int sy, int w, int h,
	ggi_visual_t dst, int dx, int dy)
{
	ggi_mode mode;
	ggiGetMode(src, &mode);
	if (sx < 0) {
		w += sx;
		dx += sx;
		sx = 0;
	}
	if (sx + w > mode.virt.x)
		w = mode.virt.x - sx;
	if (w < 0)
		return 0;
	if (sy < 0) {
		h += sy;
		dy += sy;
		sy = 0;
	}
	if (sy + h > mode.virt.y)
		h = mode.virt.y - sy;
	if (h < 0)
		return 0;
#undef ggiCrossBlit /* now safe to call the real ggiCrossBlit */
	return ggiCrossBlit(src, sx, sy, w, h, dst, dx, dy);
}
