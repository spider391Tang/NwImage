/*
******************************************************************************

   VNC viewer dialog handling.

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

#include <ggi/ggi.h>
#include <ggi/ggi_widget.h>

#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif

#include "vnc.h"
#include "vnc-debug.h"
#include "dialog.h"

struct flush_ctx {
	ggi_widget_t dlg;
	void *behind;
};

int
try_to_resize(ggi_widget_size_t min, ggi_widget_size_t opt)
{
	ggi_mode old_mode = g.mode;

	g.width = opt.x;
	g.height = opt.y;

	select_mode();

	if (ggiCheckMode(g.stem, &g.mode) < 0) {
		g.mode = old_mode;
		return 0;
	}

	if (g.mode.visible.x == old_mode.visible.x &&
		g.mode.visible.y == old_mode.visible.y)
	{
		g.mode = old_mode;
		return 0;
	}

	if (g.mode.visible.x < min.x ||
		g.mode.visible.y < min.y)
	{
		/* TODO: It still might be better... */
		g.mode = old_mode;
		return 0;
	}

	if (ggiSetMode(g.stem, &g.mode)) {
		g.mode = old_mode;
		if (ggiSetMode(g.stem, &g.mode))
			return -1;

#ifdef GGIWMHFLAG_CATCH_CLOSE
		ggiWmhAddFlags(g.stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
		ggiWmhAddFlags(g.stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif
		return 0;
	}

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	return 2;
}

ggi_widget_t
add_scroller(ggi_widget_t child)
{
	int sx, sy;
	int x;
	int y;

	sx = g.mode.visible.x;
	sy = g.mode.visible.y;
	if (sx < child->min.x) {
		sy -= 11;
		if (sy < child->min.y)
			sx -= 11;
	}
	else {
		sx -= 11;
		if (sx < child->min.x)
			sy -= 11;
	}

	x = (sx - 18) * sx / child->opt.x;
	y = (sy - 18) * sy / child->opt.y;
	if (sx >= child->min.x)
		x = 0;
	if (sy >= child->min.y)
		y = 0;

	return ggiWidgetCreateContainerScroller(
		sx, sy, x, y, 11, 0.1, 0.1,
		GWT_SCROLLER_OPTION_X_AUTO | GWT_SCROLLER_OPTION_Y_AUTO,
		child);
}

ggi_widget_t
attach_and_fit_visual(ggi_widget_t widget,
	void (*hook)(ggi_widget_t, void *), void *data)
{
	int resize;
	ggi_widget_t visualanchor = g.visualanchor;

	visualanchor->linkchild(visualanchor, BY_XYPOS, widget,
		0, 0,
		g.mode.visible.x, g.mode.visible.y,
		0);
	ggiWidgetRedrawWidgets(visualanchor);

	if (hook)
		hook(widget, data);

	if (g.mode.visible.x >= widget->min.x &&
		g.mode.visible.y >= widget->min.y)
	{
		return widget;
	}

	debug(2, "Trying to resize (min %dx%d, have %dx%d)\n",
		widget->min.x, widget->min.y,
		g.mode.visible.x, g.mode.visible.y);

	resize = try_to_resize(widget->min, widget->opt);
	if (resize < 0)
		return NULL;

	if (resize > 0) {
		visualanchor->unlinkchild(visualanchor,
			UNLINK_BY_WIDGETPTR, widget);
		visualanchor->linkchild(visualanchor, BY_XYPOS, widget,
			0, 0,
			g.mode.visible.x, g.mode.visible.y,
			0);
	}

	if (resize >= 2)
		return widget;

	ggiWidgetRedrawWidgets(visualanchor);

	if (hook)
		hook(widget, data);

	visualanchor->unlinkchild(visualanchor, UNLINK_BY_WIDGETPTR, widget);
	widget = add_scroller(widget);
	visualanchor->linkchild(visualanchor, BY_XYPOS, widget,
		0, 0,
		g.mode.visible.x, g.mode.visible.y,
		0);

	return widget;
}

static ggi_widget_t
dlg_scroller(ggi_widget_t dlg, int *w, int *h)
{
	int sx, sy;
	int x = 0;
	int y = 0;
	int wx, wy;

	wx = *w;
	wy = *h;
	sx = g.mode.visible.x - 4;
	sy = g.mode.visible.y - 4;
	if (sx < wx) {
		x = 1;
		wy += 11;
		if (sy < wy) {
			y = 1;
			wx += 11;
		}
	}
	else {
		y = 1;
		wx += 11;
		if (sx < wx) {
			x = 1;
			wy += 11;
		}
	}

	if (sx > wx)
		sx = wx;
	if (sy > wy)
		sy = wy;
	if (x)
		x = (sx - 18 - 11 * !!y) * (sx - 11 * !!y) / *w;
	if (y)
		y = (sy - 18 - 11 * !!x) * (sy - 11 * !!x) / *h;
	*w = sx;
	*h = sy;

	return ggiWidgetCreateContainerScroller(
		sx - 11 * !!y, sy - 11 * !!x,
		x, y, 11, 0.1, 0.1,
		GWT_SCROLLER_OPTION_X_AUTO | GWT_SCROLLER_OPTION_Y_AUTO,
		dlg);
}

static void
dlg_flush(void *data)
{
	struct flush_ctx *ctx = (struct flush_ctx *)data;

	if (ctx->behind &&
		ggiGetDisplayFrame(g.stem) != ggiGetReadFrame(g.stem))
	{
		ggiGetBox(g.stem,
			ctx->dlg->pos.x, ctx->dlg->pos.y,
			ctx->dlg->size.x, ctx->dlg->size.y,
			ctx->behind);
	}

	SET_ICHANGED(ctx->dlg);
	ggiWidgetRedrawWidgets(ctx->dlg);
}

static void
dlg_post_flush(void *data)
{
	struct flush_ctx *ctx = (struct flush_ctx *)data;

	if (ctx->behind &&
		ggiGetDisplayFrame(g.stem) != ggiGetReadFrame(g.stem))
	{
		ggiPutBox(g.stem,
			ctx->dlg->pos.x, ctx->dlg->pos.y,
			ctx->dlg->size.x, ctx->dlg->size.y,
			ctx->behind);
	}
}

ggi_widget_t
popup_dialog(ggi_widget_t dlg, int *done)
{
	ggi_widget_t scroller;
	ggi_widget_t frame;
	ggi_widget_t visualanchor = g.visualanchor;
	int x, y, w, h;
	struct flush_ctx flush_ctx = { NULL, NULL };
	int w_frame;

	visualanchor->linkchild(visualanchor, BY_XYPOS, dlg,
		0, 0, 0, 0, 0);

	ggiWidgetRedrawWidgets(visualanchor);
	w = dlg->min.x;
	h = dlg->min.y;
	visualanchor->unlinkchild(visualanchor, UNLINK_BY_WIDGETPTR, dlg);

	if (g.mode.visible.x - 4 < w || g.mode.visible.y - 4 < h) {
		scroller = dlg_scroller(dlg, &w, &h);
		if (!scroller) {
			*done = 1;
			return dlg;
		}
		dlg = scroller;
	}

	frame = ggiWidgetCreateFrame(
		GWT_FRAMEGROUP_USER1, 2, GWT_FRAMETYPE_3D_OUT, dlg);
	if (!frame) {
		*done = 1;
		return dlg;
	}
	dlg = frame;
	w += 4;
	h += 4;

	x = (g.mode.visible.x - w) / 2;
	y = (g.mode.visible.y - h) / 2;

	w_frame = ggiGetWriteFrame(g.stem);
	ggiSetWriteFrame(g.stem, ggiGetDisplayFrame(g.stem));
	ggiSetReadFrame(g.stem, ggiGetDisplayFrame(g.stem));

	if (!g.wire_stem ||
		x + w > g.offset.x + g.area.x ||
		y + h > g.offset.y + g.area.y)
	{
		flush_ctx.behind = malloc(w * h * GT_ByPP(g.mode.graphtype));
		if (!flush_ctx.behind) {
			*done = 1;
			return dlg;
		}
		ggiGetBox(g.stem, x, y, w, h, flush_ctx.behind);
	}

	visualanchor->linkchild(visualanchor, BY_XYPOS, dlg,
		x, y, w, h, 0);
	ggiWidgetRedrawWidgets(visualanchor);

	ggiSetWriteFrame(g.stem, w_frame);
	ggiSetReadFrame(g.stem, w_frame);

	g.flush_hook = dlg_flush;
	g.post_flush_hook = dlg_post_flush;
	flush_ctx.dlg = dlg;
	g.flush_hook_data = &flush_ctx;

	while (!*done) {
		gii_event event;
		giiEventRead(g.stem, &event, emAll);

		w_frame = ggiGetWriteFrame(g.stem);
		ggiSetWriteFrame(g.stem, ggiGetDisplayFrame(g.stem));
		ggiSetReadFrame(g.stem, ggiGetDisplayFrame(g.stem));

		ggiWidgetProcessEvent(visualanchor, &event);
		ggiWidgetRedrawWidgets(visualanchor);

		ggiSetWriteFrame(g.stem, w_frame);
		ggiSetReadFrame(g.stem, w_frame);

		switch(event.any.type) {
		case evKeyRelease:
			if (event.key.sym == GIIUC_Escape) {
				*done = 1;
				break;
			}

#ifdef GGIWMHFLAG_CATCH_CLOSE
		case evFromAPI:
			if (event.fromapi.api_id == libggiwmh->id) {
				switch (event.fromapi.code) {
				case GII_SLI_CODE_WMH_CLOSEREQUEST:
					debug(1, "quiting\n");
					*done = -1;
					break;
				}
			}
			break;
#endif
		}
	}

	g.flush_hook = NULL;
	g.post_flush_hook = NULL;
	g.flush_hook_data = NULL;

	visualanchor->unlinkchild(visualanchor, UNLINK_BY_WIDGETPTR, dlg);

	if (flush_ctx.behind) {
		w_frame = ggiGetWriteFrame(g.stem);
		ggiSetWriteFrame(g.stem, ggiGetDisplayFrame(g.stem));
		ggiSetReadFrame(g.stem, ggiGetDisplayFrame(g.stem));

		ggiPutBox(g.stem, x, y, w, h, flush_ctx.behind);
		free(flush_ctx.behind);

		ggiSetWriteFrame(g.stem, w_frame);
		ggiSetReadFrame(g.stem, w_frame);
	}
	if (g.wire_stem) {
		ggiSetGCClipping(g.stem,
			g.offset.x, g.offset.y,
			g.offset.x + g.area.x, g.offset.y + g.area.y);
		ggiCrossBlit(g.wire_stem,
			g.slide.x + x, g.slide.y + y,
			w, h,
			g.stem,
			g.offset.x + x, g.offset.y + y);
		ggiSetGCClipping(g.stem, 0, 0, g.mode.virt.x, g.mode.virt.y);
	}

	ggiFlushRegion(g.stem, x, y, w, h);

	return dlg;
}
