/*
******************************************************************************

   VNC viewer dialog handling.

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

#ifndef DIALOG_H
#define DIALOG_H

int try_to_resize(struct connection *cx,
	ggi_widget_size_t min, ggi_widget_size_t opt);
ggi_widget_t add_scroller(ggi_widget_t child, int sx, int sy);
ggi_widget_t attach_and_fit_visual(struct connection *cx, ggi_widget_t widget,
	void (*hook)(ggi_widget_t, void *), void *data);
ggi_widget_t popup_dialog(struct connection *cx, ggi_widget_t dlg, int *done,
	int (*hook)(struct connection *cx, void *), void *data);

#endif /* DIALOG_H */
