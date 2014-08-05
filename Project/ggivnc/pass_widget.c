/*
******************************************************************************

   Get a password from the user using ggiwidgets.

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

#include <string.h>
#include <ggi/ggi_widget.h>

#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif

#include "vnc.h"
#include "vnc-debug.h"
#include "dialog.h"

static void
password_cb(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 1;
}

static void
cancel_cb(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 2;
}

int
get_password(void)
{
	ggi_widget_t label;
	ggi_widget_t text;
	ggi_widget_t ok;
	ggi_widget_t cancel;
	ggi_widget_t buttons;
	ggi_widget_t stack;
	ggi_widget_t res;
	ggi_widget_t visualanchor = g.visualanchor;
	static char password[9];
	int done = 0;

	g.width = 320;
	g.height = 100;

	select_mode();

	ggiCheckMode(g.stem, &g.mode);

	if (ggiSetMode(g.stem, &g.mode))
		return -1;

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	if (set_title())
		return -1;

	ggiSetFlags(g.stem, GGIFLAG_ASYNC);
	ggiSetColorfulPalette(g.stem);

	label = ggiWidgetCreateLabel("Enter password");
	memset(password, 0, sizeof(password));
	text = ggiWidgetCreateText(
		password, sizeof(password) - 1, sizeof(password) - 1);
	text->callback = password_cb;
	text->callbackpriv = &done;
	g.passwd = password;

	ggiWidgetSendControl(text,
		GWT_CONTROLFLAG_NONE, "SETPASSWORDTYPE", 1);
	ggiWidgetSendControl(text,
		GWT_CONTROLFLAG_NONE, "SETPASSWORDSILENT", 0);
	ggiWidgetSendControl(text,
		GWT_CONTROLFLAG_NONE, "SETPASSWORDCHAR", '*');

	ok = ggiWidgetCreateLabel("OK");
	ok->pad.t = ok->pad.b = 3;
	ok->pad.l = ok->pad.r = 24;
	ok = ggiWidgetCreateButton(ok);
	ok->callback = password_cb;
	ok->callbackpriv = &done;

	cancel = ggiWidgetCreateLabel("Cancel");
	cancel->pad.t = cancel->pad.b = 3;
	cancel->pad.l = cancel->pad.r = 8;
	cancel = ggiWidgetCreateButton(cancel);
	cancel->callback = cancel_cb;
	cancel->callbackpriv = &done;
	cancel->hotkey.sym = GIIUC_Escape;

	buttons = ggiWidgetCreateContainerLine(10, ok, cancel, NULL);
	buttons->gravity = GWT_GRAV_NORTH;
	buttons->pad.t = 10;

	stack = ggiWidgetCreateContainerStack(4, label, text, buttons, NULL);
	stack->pad.t = stack->pad.l = 5;
	stack->pad.b = stack->pad.r = 5;

	ggiWidgetFocus(text);

	res = attach_and_fit_visual(stack, NULL, NULL);
	if (!res)
		goto out;

	if (set_title())
		goto out;

	stack = res;

	while (!done) {
		gii_event event;
		giiEventRead(g.stem, &event, emAll);

#ifdef GGIWMHFLAG_CATCH_CLOSE
		switch (event.any.type) {
		case evFromAPI:
			if (event.fromapi.api_id == libggiwmh->id) {
				switch (event.fromapi.code) {
				case GII_SLI_CODE_WMH_CLOSEREQUEST:
					debug(1, "quiting\n");
					done = 2;
					break;
				}
			}
			break;
		}
#endif

		ggiWidgetProcessEvent(visualanchor, &event);
		ggiWidgetRedrawWidgets(visualanchor);
	}

	visualanchor->unlinkchild(visualanchor, UNLINK_BY_WIDGETPTR, stack);
out:
	stack->destroy(stack);

	return done - 1;
}
