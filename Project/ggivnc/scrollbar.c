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

#include "config.h"

#include <ggi/gii.h>
#include <ggi/ggi.h>
#include <ggi/ggi_widget.h>

#include "vnc.h"
#include "scrollbar.h"
#include "vnc-debug.h"
#include "vnc-compat.h"

int
scrollbar_process(struct connection *cx, gii_event *event)
{
	ggi_widget_t visualanchor = cx->visualanchor;

	if (event->any.type == evKeyPress ||
		event->any.type == evKeyRepeat ||
		event->any.type == evKeyRelease)
		return 0;

	if (!visualanchor)
		return 0;

	if (!ggiWidgetProcessEvent(visualanchor, event))
		return 0;

	ggiWidgetRedrawWidgets(visualanchor);
	ggiCrossBlit(cx->wire_stem,
		cx->slide.x, cx->slide.y,
		cx->area.x, cx->area.y,
		cx->stem,
		cx->offset.x, cx->offset.y);
	ggiFlush(cx->stem);
	return 1;
}

void
scrollbar_area(struct connection *cx)
{
	if (cx->mode.visible.x < cx->width) {
		cx->scrollx = 1;
		cx->area.x = cx->mode.visible.x;
		if (cx->mode.visible.y - SCROLL_SIZE >= cx->height)
			return;

		cx->scrolly = 1;
		cx->area.x -= SCROLL_SIZE;
		cx->area.y = cx->mode.visible.y - SCROLL_SIZE;
		return;
	}

	if (cx->mode.visible.y < cx->height) {
		cx->scrolly = 1;
		cx->area.y = cx->mode.visible.y;
		if (cx->mode.visible.x - SCROLL_SIZE >= cx->width)
			return;

		cx->scrollx = 1;
		cx->area.x = cx->mode.visible.x - SCROLL_SIZE;
		cx->area.y -= SCROLL_SIZE;
	}
}

static void
scrollbar_x(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct connection *cx = widget->callbackpriv;
	if (cbt != GWT_CB_STATECHANGE)
		return;
	cx->slide.x = *(double *)widget->statevar;
}

static void
scrollbar_y(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct connection *cx = widget->callbackpriv;
	if (cbt != GWT_CB_STATECHANGE)
		return;
	cx->slide.y = *(double *)widget->statevar;
}

void
scrollbar_create(struct connection *cx)
{
	ggi_widget_t scroll_x = NULL;
	ggi_widget_t scroll_y = NULL;
	ggi_widget_t visualanchor = cx->visualanchor;

	if (cx->scrollx) {
		int length = (cx->area.x - 18) * cx->area.x / cx->width;
		double max = cx->width - cx->area.x;
		double step = max / 10;
		scroll_x = ggiWidgetCreateScrollbar(0,
			cx->area.x, SCROLL_SIZE, length,
			-1.0, 0.0, max, step);
		scroll_x->statevar = &cx->sx;
		scroll_x->callback = scrollbar_x;
		scroll_x->callbackpriv = cx;
		cx->scroll = scroll_x;
	}

	if (cx->scrolly) {
		int length = (cx->area.y - 18) * cx->area.y / cx->height;
		double max = cx->height - cx->area.y;
		double step = max / 10;
		scroll_y = ggiWidgetCreateScrollbar(1,
			cx->area.y, SCROLL_SIZE, length,
			-1.0, 0.0, max, step);
		scroll_y->statevar = &cx->sy;
		scroll_y->callback = scrollbar_y;
		scroll_y->callbackpriv = cx;
		cx->scroll = scroll_y;
	}

	if (cx->scrollx && cx->scrolly) {
		ggi_widget_t grid;
		grid = ggiWidgetCreateContainerGrid(2, 2, 0, 0);
		ggiWidgetLinkChild(grid, GWT_BY_XYPOS, scroll_x, 0, 1);
		ggiWidgetLinkChild(grid, GWT_BY_XYPOS, scroll_y, 1, 0);
		ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, grid,
			cx->offset.x, cx->offset.y,
			cx->area.x + SCROLL_SIZE, cx->area.y + SCROLL_SIZE,
			GWT_AF_BACKGROUND);
		cx->scroll = grid;
	}
	else if (cx->scrollx)
		ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, scroll_x,
			cx->offset.x, cx->offset.y + cx->area.y,
			cx->area.x, SCROLL_SIZE,
			GWT_AF_BACKGROUND);
	else if (cx->scrolly)
		ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, scroll_y,
			cx->offset.x + cx->area.x, cx->offset.y,
			SCROLL_SIZE, cx->area.y,
			GWT_AF_BACKGROUND);

	if (cx->scroll)
		ggiWidgetRedrawWidgets(visualanchor);
}

void
scrollbar_destroy(struct connection *cx)
{
	ggi_widget_t scroll = cx->scroll;
	ggi_widget_t visualanchor = cx->visualanchor;

	if (!scroll || !visualanchor)
		return;

	ggiWidgetUnlinkChild(visualanchor, GWT_UNLINK_BY_WIDGETPTR, scroll);
	ggiWidgetDestroy(scroll);
	cx->scroll = NULL;
}
