/*
******************************************************************************

   VNC viewer scrollbar handling.

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

#ifndef SCROLLBAR_H
#define SCROLLBAR_H

#define SCROLL_SIZE 11

#ifdef HAVE_WIDGETS

int scrollbar_process(struct connection *cx, gii_event *event);
void scrollbar_area(struct connection *cx);
void scrollbar_create(struct connection *cx);
void scrollbar_destroy(struct connection *cx);

#else

#define scrollbar_process(cx, event) 0
#define scrollbar_area(cx) do; while(0)
#define scrollbar_create(cx) do; while(0)
#define scrollbar_destroy(cx) do; while(0)

#endif /* HAVE_WIDGETS */

#endif /* SCROLLBAR_H */
