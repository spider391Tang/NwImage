/*
******************************************************************************

   VNC viewer icon.

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

#ifndef GGIVNC_ICON_H
#define GGIVNC_ICON_H

#define R 255,0,0
#define Y 255,255,0
#define G 0,255,0
#define B 0,0,255
#define M 255,0,255
#define C 128,0,128
#define i 0,0,0
#define j 255,255,255
#define _ 17,17,42

const uint8_t ggivnc_icon[] = {
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,R,R,R,_,_,_,_,_,_,_,Y,Y,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,R,R,R,R,R,_,_,_,Y,Y,Y,Y,Y,Y,Y,_,_,_,_,_,_,G,G,_,_,_,_,_,
	_,_,_,R,R,i,R,R,R,R,_,Y,Y,Y,Y,Y,Y,Y,Y,_,_,G,G,G,G,G,G,G,_,_,_,_,
	_,_,R,R,i,i,R,R,R,R,_,Y,Y,Y,i,i,Y,Y,Y,Y,_,G,G,G,i,G,G,G,_,_,_,_,
	_,R,R,i,i,R,R,i,R,R,R,Y,Y,i,i,Y,Y,i,Y,Y,_,G,G,G,i,i,G,G,G,_,_,_,
	_,R,R,i,i,R,i,i,i,R,R,Y,Y,i,Y,Y,i,i,Y,Y,_,_,G,G,G,i,G,G,G,_,_,_,
	_,_,R,R,i,R,R,i,i,R,R,R,Y,i,i,Y,Y,i,i,Y,Y,_,G,G,G,i,G,G,G,_,_,_,
	_,_,_,R,i,i,i,i,R,R,R,_,Y,Y,i,i,Y,i,Y,Y,Y,_,G,G,G,i,i,G,G,_,_,_,
	_,_,_,R,R,R,i,R,R,_,_,_,Y,Y,Y,i,i,Y,Y,Y,Y,_,G,G,G,G,i,G,G,G,_,_,
	_,_,_,R,R,R,R,R,_,_,_,_,_,Y,Y,Y,Y,Y,_,_,_,_,_,G,G,G,G,G,G,G,_,_,
	_,_,_,_,_,R,R,_,_,_,_,_,_,Y,Y,_,_,_,_,_,_,_,_,G,G,G,G,G,G,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,M,M,M,M,M,M,M,_,_,C,C,C,_,_,_,_,_,_,
	_,_,B,B,B,B,B,_,_,_,_,M,M,M,M,M,M,M,M,M,M,_,_,C,C,C,C,C,C,C,_,_,
	_,_,B,B,B,B,B,B,B,B,B,M,M,M,M,M,M,M,j,M,M,_,_,C,C,C,C,C,C,C,_,_,
	_,_,B,B,j,B,B,B,B,B,B,_,M,j,j,M,M,M,j,M,M,_,_,C,C,C,C,C,C,C,_,_,
	_,_,B,B,j,B,B,B,j,B,B,_,M,M,j,j,j,M,j,M,M,_,_,C,C,j,j,C,C,_,_,_,
	_,_,B,B,j,B,B,j,B,B,_,_,_,M,j,M,j,M,j,M,M,_,C,C,j,C,C,j,C,_,_,_,
	_,_,B,B,j,B,j,B,B,B,_,_,_,M,j,M,M,j,j,M,M,_,C,C,j,C,C,C,C,_,_,_,
	_,B,B,B,j,j,B,B,B,B,_,_,_,M,j,M,M,M,j,M,M,_,C,C,j,C,C,C,C,_,_,_,
	_,B,B,B,j,j,B,B,B,_,_,_,_,M,j,M,M,M,M,M,M,_,C,C,j,C,C,j,C,_,_,_,
	_,B,B,B,B,B,B,B,B,_,_,_,_,M,M,M,M,M,M,M,M,_,C,C,C,j,j,C,C,_,_,_,
	_,B,B,B,B,B,B,B,B,_,_,_,_,M,M,M,M,M,M,M,_,_,C,C,C,C,C,C,C,_,_,_,
	_,_,_,_,_,_,B,B,B,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,C,C,C,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
	_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_
};

#undef R
#undef Y
#undef G
#undef B
#undef M
#undef C
#undef i
#undef j
#undef _

const uint8_t ggivnc_mask[] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x03, 0x80, 0xc0, 0x00,
	0x0f, 0x8f, 0xe0, 0x60,
	0x1f, 0xdf, 0xe7, 0xf0,
	0x3f, 0xdf, 0xf7, 0xf0,
	0x7f, 0xff, 0xf7, 0xf8,
	0x7f, 0xff, 0xf3, 0xf8,
	0x3f, 0xff, 0xfb, 0xf8,
	0x1f, 0xef, 0xfb, 0xf8,
	0x1f, 0x8f, 0xfb, 0xfc,
	0x1f, 0x07, 0xc1, 0xfc,
	0x06, 0x06, 0x01, 0xf8,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x03, 0xf9, 0xc0,
	0x3e, 0x1f, 0xf9, 0xfc,
	0x3f, 0xff, 0xf9, 0xfc,
	0x3f, 0xef, 0xf9, 0xfc,
	0x3f, 0xef, 0xf9, 0xf8,
	0x3f, 0xc7, 0xfb, 0xf8,
	0x3f, 0xc7, 0xfb, 0xf8,
	0x7f, 0xc7, 0xfb, 0xf8,
	0x7f, 0x87, 0xfb, 0xf8,
	0x7f, 0x87, 0xfb, 0xf8,
	0x7f, 0x87, 0xf3, 0xf8,
	0x03, 0x80, 0x00, 0x70,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

#endif /* GGIVNC_ICON_H */
