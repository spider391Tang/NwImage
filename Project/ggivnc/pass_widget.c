/*
******************************************************************************

   Get a password from the user using ggiwidgets.

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
#include <ggi/ggi_widget.h>

#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif

#include "vnc.h"
#include "vnc-debug.h"
#include "dialog.h"

static void
password_cb(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 1;
}

static void
cancel_cb(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 2;
}

int
get_password(struct connection *cx, const char *uprompt, int ulen,
	const char *pprompt, int plen)
{
	ggi_widget_t item;
	ggi_widget_t label;
	ggi_widget_t buttons;
	ggi_widget_t stack = NULL;
	ggi_widget_t res;
	ggi_widget_t visualanchor = cx->visualanchor;
	const char *prompt;
	char *username = NULL;
	char *password = NULL;
	int done = 0;

	if (uprompt) {
		username = malloc(ulen + 1);
		if (!username)
			goto out;
	}

	password = malloc(plen + 1);
	if (!password)
		goto out;

	cx->width = 320;
	cx->height = 100 + (uprompt ? 25 : 0);

	select_mode(cx);

	ggiCheckMode(cx->stem, &cx->mode);

	if (ggiSetMode(cx->stem, &cx->mode))
		goto out;

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	if (set_title(cx))
		goto out;

	ggiSetFlags(cx->stem, GGIFLAG_ASYNC);
	ggiSetColorfulPalette(cx->stem);

	stack = ggiWidgetCreateContainerStack(2, NULL);
	if (!stack)
		goto out;
	stack->pad.t = stack->pad.l = 5;
	stack->pad.b = stack->pad.r = 5;

	if (uprompt) {
		item = ggiWidgetCreateLabel(uprompt);
		if (!item)
			goto out;
		ggiWidgetLinkChild(stack, GWT_LAST_CHILD, item);

		memset(username, 0, ulen + 1);
		item = ggiWidgetCreateText(username,
			ulen < 32 ? ulen : 32, ulen);
		if (!item)
			goto out;
		item->callback = password_cb;
		item->callbackpriv = &done;
		cx->username = username;
		ggiWidgetLinkChild(stack, GWT_LAST_CHILD, item);
	}

	prompt = pprompt ? pprompt : "Enter password";
	item = ggiWidgetCreateLabel(prompt);
	if (!item)
		goto out;
	ggiWidgetLinkChild(stack, GWT_LAST_CHILD, item);

	memset(password, 0, plen + 1);
	item = ggiWidgetCreateText(password, plen < 32 ? plen : 32, plen);
	if (!item)
		goto out;
	item->callback = password_cb;
	item->callbackpriv = &done;
	ggiWidgetSendControl(item,
		GWT_CONTROLFLAG_NONE, "SETPASSWORDTYPE", 1);
	ggiWidgetSendControl(item,
		GWT_CONTROLFLAG_NONE, "SETPASSWORDSILENT", 0);
	ggiWidgetSendControl(item,
		GWT_CONTROLFLAG_NONE, "SETPASSWORDCHAR", '*');
	cx->passwd = password;
	ggiWidgetLinkChild(stack, GWT_LAST_CHILD, item);

	buttons = ggiWidgetCreateContainerLine(10, NULL);
	if (!buttons)
		goto out;
	buttons->gravity = GWT_GRAV_NORTH;
	buttons->pad.t = 10;
	ggiWidgetLinkChild(stack, GWT_LAST_CHILD, buttons);

	label = ggiWidgetCreateLabel("OK");
	if (!label)
		goto out;
	label->pad.t = label->pad.b = 3;
	label->pad.l = label->pad.r = 24;
	item = ggiWidgetCreateButton(label);
	if (!item) {
		ggiWidgetDestroy(label);
		goto out;
	}
	item->callback = password_cb;
	item->callbackpriv = &done;
	ggiWidgetLinkChild(buttons, GWT_LAST_CHILD, item);

	label = ggiWidgetCreateLabel("Cancel");
	if (!label)
		goto out;
	label->pad.t = label->pad.b = 3;
	label->pad.l = label->pad.r = 8;
	item = ggiWidgetCreateButton(label);
	if (!item) {
		ggiWidgetDestroy(label);
		goto out;
	}
	item->callback = cancel_cb;
	item->callbackpriv = &done;
	item->hotkey.sym = GIIUC_Escape;
	ggiWidgetLinkChild(buttons, GWT_LAST_CHILD, item);

	ggiWidgetFocus(ggiWidgetGetChild(stack, 1), NULL, NULL);

	res = attach_and_fit_visual(cx, stack, NULL, NULL);
	if (!res)
		goto out;
	stack = res;

	if (set_title(cx))
		goto out;

	while (!done) {
		gii_event event;
		giiEventRead(cx->stem, &event, emAll);

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

out:
	ggiWidgetUnlinkChild(visualanchor, GWT_UNLINK_BY_WIDGETPTR, stack);
	ggiWidgetDestroy(stack);

	if (--done) {
		if (username) {
			free(username);
			cx->username = NULL;
		}
		if (password) {
			free(password);
			cx->passwd = NULL;
		}
	}
	return done;
}
