/*
******************************************************************************

   VNC viewer xvp pseudo-encoding.

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

#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <ggi/ggi.h>
#include <ggi/ggi_widget.h>
#include "vnc.h"
#include "dialog.h"
#include "vnc-endian.h"
#include "vnc-debug.h"


struct xvp {
	int version;
	int popup;
};

static void
xvp_fail_close(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 2;
}

static int
xvp_fail(struct connection *cx)
{
	struct xvp *xvp = cx->encoding_def[xvp_encoding].priv;
	ggi_widget_t item;
	ggi_widget_t button;
	ggi_widget_t dlg;
	int done = 0;

	if (xvp->popup) {
		debug(2, "xvp_fail with popup already visible.\n");
		return 2;
	}

	if (cx->flush_hook) {
		debug(1, "xvp_fail with *other* popup already visible.\n");
		return 0;
	}

	dlg = ggiWidgetCreateContainerStack(2, NULL);
	if (!dlg)
		return 0;
	dlg->pad.t = dlg->pad.l = dlg->pad.b = dlg->pad.r = 10;

	item = ggiWidgetCreateLabel("An XVP operation failed");
	if (!item)
		goto destroy_dlg;
	item->pad.b = 5;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);

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
	button->callback = xvp_fail_close;
	button->callbackpriv = &done;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, button);
	ggiWidgetFocus(button, NULL, NULL);

	xvp->popup = 1;
	dlg = popup_dialog(cx, dlg, &done, NULL, NULL);
	xvp->popup = 0;

destroy_dlg:
	ggiWidgetDestroy(dlg);

	return done;
}

static int
xvp_send_msg(struct connection *cx, uint8_t code)
{
	struct xvp *xvp = cx->encoding_def[xvp_encoding].priv;
	uint8_t buf[4] = {
		250,
		0,
		1
	};

	if (!xvp) {
		debug(1, "xvp not initialized\n");
		return 0;
	}

	if (xvp->version != 1) {
		debug(1, "xvp not active\n");
		return 0;
	}

	if (cx->no_input)
		return 0;

	buf[3] = code;

	if (safe_write(cx, buf, sizeof(buf))) {
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	return 1;
}

int
xvp_shutdown(struct connection *cx)
{
	return xvp_send_msg(cx, 2);
}

int
xvp_reboot(struct connection *cx)
{
	return xvp_send_msg(cx, 3);
}

int
xvp_reset(struct connection *cx)
{
	return xvp_send_msg(cx, 4);
}

static int
xvp_receive_msg(struct connection *cx)
{
	struct xvp *xvp = cx->encoding_def[xvp_encoding].priv;
	uint8_t version;
	uint8_t code;

	if (!xvp)
		return close_connection(cx, -1);

	debug(1, "xvp_receive\n");

	if (cx->input.wpos < cx->input.rpos + 3)
		return 0;

	version = cx->input.data[cx->input.rpos + 2];
	if (version != 1) {
		debug(1, "got unknown xvp version %d\n", version);
		return close_connection(cx, -1);
	}

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	code = cx->input.data[cx->input.rpos + 3];
	cx->input.rpos += 4;

	switch (code) {
	case 0: /* fail */
		switch (xvp_fail(cx)) {
		case -1:
			/* TODO: better way to close the app */
		case 0:
		case 1:
			return close_connection(cx, -1);
		}
		break;

	case 1: /* init */
		xvp->version = version;
		break;

	default:
		debug(1, "got unknown xvp code %d\n", code);
		return close_connection(cx, -1);
	}

	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static void
xvp_end(struct connection *cx)
{
	struct xvp *xvp = cx->encoding_def[xvp_encoding].priv;

	if (!xvp)
		return;

	debug(1, "xvp_end\n");

	free(cx->encoding_def[xvp_encoding].priv);
	cx->encoding_def[xvp_encoding].priv = NULL;
	cx->encoding_def[xvp_encoding].action = xvp_receive;
}

int
xvp_receive(struct connection *cx)
{
	struct xvp *xvp;

	debug(1, "xvp init\n");

	xvp = malloc(sizeof(*xvp));
	if (!xvp)
		return close_connection(cx, -1);
	memset(xvp, 0, sizeof(*xvp));

	cx->encoding_def[xvp_encoding].priv = xvp;
	cx->encoding_def[xvp_encoding].end = xvp_end;

	cx->action = cx->encoding_def[xvp_encoding].action = xvp_receive_msg;
	return cx->action(cx);
}
