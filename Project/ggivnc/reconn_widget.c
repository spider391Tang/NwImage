/*
******************************************************************************

   Present a reconnection dialog to the user using ggiwidgets.

   The MIT License

   Copyright (C) 2009, 2010 Peter Rosin  [peda@lysator.liu.se]

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
#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif
#include <ggi/ggi_widget.h>

#include "vnc.h"
#include "vnc-debug.h"
#include "dialog.h"
extern const uint8_t ggivnc_icon[];
extern const uint8_t ggivnc_mask[];
/* #include "ggivnc-icon.h" */

static void
reconnect_yes(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 2;
}

static void
reconnect_no(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 1;
}

struct timeout {
	time_t stamp;
	int *done;
};

static int
timeout_hook(struct connection *cx, void *data)
{
	struct timeout *to = (struct timeout *)data;
	struct timeval tv = { 1, 0 };

	if (!cx->auto_reconnect)
		return 0;

	if (time(NULL) > to->stamp + 2) {
		debug(1, "auto-reconnect\n");
		*to->done = 2;
		return 1;
	}
	return !giiEventPoll(cx->stem, emAll, &tv);
}

int
show_reconnect(struct connection *cx, int popup)
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
	struct timeout to = { 0, NULL };

	to.done = &done;

	debug(2, "show_reconnect\n");

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

	item = ggiWidgetCreateLabel("The connection has been closed.");
	if (!item)
		goto destroy_dlg;
	item->pad.b = 5;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item = ggiWidgetCreateLabel("Do you wish to reconnect?");
	if (!item)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);
	item->pad.b = 5;

	line = ggiWidgetCreateContainerLine(20, NULL);
	if (!line)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, line);
	item = ggiWidgetCreateLabel("Yes");
	if (!item)
		goto destroy_dlg;
	item->pad.t = item->pad.b = 3;
	item->pad.l = item->pad.r = 12;
	button = ggiWidgetCreateButton(item);
	if (!button) {
		ggiWidgetDestroy(item);
		goto destroy_dlg;
	}
	button->callback = reconnect_yes;
	button->callbackpriv = &done;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, button);
	ggiWidgetFocus(button, NULL, NULL);
	item = ggiWidgetCreateLabel("No");
	if (!item)
		goto destroy_dlg;
	item->pad.t = item->pad.b = 3;
	item->pad.l = item->pad.r = 12;
	button = ggiWidgetCreateButton(item);
	if (!button) {
		ggiWidgetDestroy(item);
		goto destroy_dlg;
	}
	button->callback = reconnect_no;
	button->callbackpriv = &done;
	button->hotkey.sym = GIIUC_Escape;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, button);

	to.stamp = time(NULL);
	if (popup) {
		dlg = popup_dialog(cx, dlg, &done, timeout_hook, &to);
		goto destroy_dlg;
	}

	{
		ggi_widget_t res = attach_and_fit_visual(cx, dlg, NULL, NULL);

		if (!res)
			goto out;
		if (set_title(cx))
			goto out;
		dlg = res;
	}

	ggiWidgetRedrawWidgets(cx->visualanchor);

	while (!done) {
		gii_event event;
		if (timeout_hook(cx, &to))
			continue;
		giiEventRead(cx->stem, &event, emAll);

#ifdef GGIWMHFLAG_CATCH_CLOSE
		switch (event.any.type) {
		case evFromAPI:
			if (event.fromapi.api_id == libggiwmh->id) {
				switch (event.fromapi.code) {
				case GII_SLI_CODE_WMH_CLOSEREQUEST:
					debug(1, "quiting\n");
					done = 1;
					break;
				}
			}
			break;
		}
#endif

		ggiWidgetProcessEvent(cx->visualanchor, &event);
		ggiWidgetRedrawWidgets(cx->visualanchor);
	}

out:
	if (dlg) {
		ggiWidgetUnlinkChild(cx->visualanchor,
			GWT_UNLINK_BY_WIDGETPTR, dlg);
	}
	if (done <= 1)
		close_visual(cx);

destroy_dlg:
	ggiWidgetDestroy(dlg);
	return done - 1;
}
