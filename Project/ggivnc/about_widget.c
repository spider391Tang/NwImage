/*
******************************************************************************

   Present an about dialog to the user using ggiwidgets.

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

#include <string.h>
#include <ggi/gii.h>
#include <ggi/gii-events.h>
#include <ggi/gii-keyboard.h>
#include <ggi/ggi.h>
#include <ggi/ggi_widget.h>

#include "vnc.h"
#include "dialog.h"
#include "ggivnc-icon.h"

static void
about_close(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 1;
}

int
show_about(struct connection *cx)
{
	ggi_widget_t item;
	ggi_widget_t line;
	ggi_widget_t button;
	ggi_widget_t dlg;
	static struct ggiWidgetImage icon = {
		{ 32, 32 },
		GWT_IF_RGB, ggivnc_icon,
		GWT_IF_BITMAP, ggivnc_mask
	};
	int done = 0;

	dlg = ggiWidgetCreateContainerStack(2, NULL);
	if (!dlg)
		return 0;
	dlg->pad.t = dlg->pad.l = dlg->pad.b = dlg->pad.r = 10;

	line = ggiWidgetCreateContainerLine(20, NULL);
	if (!line)
		goto destroy_dlg;
	line->pad.b = 5;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, line);
	item = ggiWidgetCreateImage(&icon);
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel(PACKAGE_STRING);
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, item);

	item = ggiWidgetCreateLabel("Written by Peter Rosin");
	if (!item)
		goto destroy_dlg;
	item->pad.b = 5;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("Uses libraries from");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("the GGI project");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
#ifdef HAVE_ZLIB
	item = ggiWidgetCreateLabel("and the zlib library by");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("Jean-loup Gailly and Mark Adler");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
#ifdef HAVE_JPEG
#if defined HAVE_TURBOJPEG
	item = ggiWidgetCreateLabel("and the TurboJPEG library by");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("Julian Smart, Robert Roebling et al");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("which may use the jpeg library from");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
#elif defined HAVE_JPEGLIB
	item = ggiWidgetCreateLabel("and the jpeg library from");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
#endif
	item = ggiWidgetCreateLabel("the Independent JPEG Group\n");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
#endif /* HAVE_JPEG */
#endif /* HAVE_ZLIB */
#ifdef HAVE_OPENSSL
	item = ggiWidgetCreateLabel("This product includes software\n");
	if (!item)
		goto destroy_dlg;
	item->pad.t = 5;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("developed by the OpenSSL Project\n");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("for use in the OpenSSL Toolkit.\n");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("(http://www.openssl.org/)\n");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("This product includes crypto-\n");
	if (!item)
		goto destroy_dlg;
	item->pad.t = 5;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("graphic software written by\n");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("Eric Young (eay@cryptsoft.com)\n");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
#endif
	item->pad.b = 5;
	item = ggiWidgetCreateLabel("OK");
	if (!item)
		goto destroy_dlg;
	item->pad.t = item->pad.b = 3;
	item->pad.l = item->pad.r = 12;
	button = ggiWidgetCreateButton(item);
	if (!button) {
		ggiWidgetDestroy(item);
		goto destroy_dlg;
	}
	button->callback = about_close;
	button->callbackpriv = &done;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, button);
	ggiWidgetFocus(button, NULL, NULL);

	dlg = popup_dialog(cx, dlg, &done, NULL, NULL);
	if (!dlg)
		return 0;

destroy_dlg:
	ggiWidgetDestroy(dlg);
	return done;
}
