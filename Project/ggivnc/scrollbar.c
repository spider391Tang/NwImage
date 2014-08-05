/*
******************************************************************************

   VNC viewer scrollbar handling.

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

#include <ggi/gii.h>
#include <ggi/ggi.h>
#include <ggi/ggi_widget.h>

#include "vnc.h"
#include "scrollbar.h"
#include "vnc-debug.h"

int
scrollbar_process(gii_event *event)
{
	ggi_widget_t visualanchor = g.visualanchor;

	if (event->any.type == evKeyPress ||
		event->any.type == evKeyRepeat ||
		event->any.type == evKeyRelease)
		return 0;

	if (!visualanchor)
		return 0;

	if (!ggiWidgetProcessEvent(visualanchor, event))
		return 0;

	ggiWidgetRedrawWidgets(visualanchor);
	ggiCrossBlit(g.wire_stem,
		g.slide.x, g.slide.y,
		g.area.x, g.area.y,
		g.stem,
		g.offset.x, g.offset.y);
	ggiFlush(g.stem);
	return 1;
}

void
scrollbar_area(void)
{
	if (g.mode.visible.x < g.width) {
		g.scrollx = 1;
		g.area.x = g.mode.visible.x;
		if (g.mode.visible.y - SCROLL_SIZE >= g.height)
			return;

		g.scrolly = 1;
		g.area.x -= SCROLL_SIZE;
		g.area.y = g.mode.visible.y - SCROLL_SIZE;
		return;
	}

	if (g.mode.visible.y < g.height) {
		g.scrolly = 1;
		g.area.y = g.mode.visible.y;
		if (g.mode.visible.x - SCROLL_SIZE >= g.width)
			return;

		g.scrollx = 1;
		g.area.x = g.mode.visible.x - SCROLL_SIZE;
		g.area.y -= SCROLL_SIZE;
	}
}

static void
scrollbar_x(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	if (cbt != GWT_CB_STATECHANGE)
		return;
	g.slide.x = *(double *)widget->statevar;
}

static void
scrollbar_y(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	if (cbt != GWT_CB_STATECHANGE)
		return;
	g.slide.y = *(double *)widget->statevar;
}

void
scrollbar_create(void)
{
	ggi_widget_t scroll_x = NULL;
	ggi_widget_t scroll_y = NULL;
	ggi_widget_t visualanchor = g.visualanchor;

	if (g.scrollx) {
		int length = (g.area.x - 18) * g.area.x / g.width;
		double max = g.width - g.area.x;
		double step = max / 10;
		scroll_x = ggiWidgetCreateScrollbar(0,
			g.area.x, SCROLL_SIZE, length,
			-1.0, 0.0, max, step);
		scroll_x->statevar = &g.sx;
		scroll_x->callback = scrollbar_x;
		g.scroll = scroll_x;
	}

	if (g.scrolly) {
		int length = (g.area.y - 18) * g.area.y / g.height;
		double max = g.height - g.area.y;
		double step = max / 10;
		scroll_y = ggiWidgetCreateScrollbar(1,
			g.area.y, SCROLL_SIZE, length,
			-1.0, 0.0, max, step);
		scroll_y->statevar = &g.sy;
		scroll_y->callback = scrollbar_y;
		g.scroll = scroll_y;
	}

	if (g.scrollx && g.scrolly) {
		ggi_widget_t grid;
		grid = ggiWidgetCreateContainerGrid(2, 2, 0, 0);
		grid->linkchild(grid, BY_XYPOS, scroll_x, 0, 1);
		grid->linkchild(grid, BY_XYPOS, scroll_y, 1, 0);
		visualanchor->linkchild(visualanchor, BY_XYPOS, grid,
			g.offset.x, g.offset.y,
			g.area.x + SCROLL_SIZE, g.area.y + SCROLL_SIZE,
			0);
		g.scroll = grid;
	}
	else if (g.scrollx)
		visualanchor->linkchild(visualanchor, BY_XYPOS, scroll_x,
			g.offset.x, g.offset.y + g.area.y,
			g.area.x, SCROLL_SIZE,
			0);
	else if (g.scrolly)
		visualanchor->linkchild(visualanchor, BY_XYPOS, scroll_y,
			g.offset.x + g.area.x, g.offset.y,
			SCROLL_SIZE, g.area.y,
			0);

	if (g.scroll)
		ggiWidgetRedrawWidgets(visualanchor);
}

void
scrollbar_destroy(void)
{
	ggi_widget_t scroll = g.scroll;
	ggi_widget_t visualanchor = g.visualanchor;

	if (!scroll || !visualanchor)
		return;

	visualanchor->unlinkchild(visualanchor, UNLINK_BY_WIDGETPTR, scroll);
	scroll->destroy(scroll);
	g.scroll = NULL;
}
