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
	struct connection *cx;
	ggi_widget_t dlg;
	void *behind;
};

int
try_to_resize(struct connection *cx,
	ggi_widget_size_t min, ggi_widget_size_t opt)
{
	ggi_mode old_mode = cx->mode;

	cx->width = opt.x;
	cx->height = opt.y;

	select_mode(cx);

	ggiCheckMode(cx->stem, &cx->mode);

	if (cx->mode.visible.x == old_mode.visible.x &&
		cx->mode.visible.y == old_mode.visible.y)
	{
		cx->mode = old_mode;
		return 0;
	}

	if (cx->mode.visible.x < min.x ||
		cx->mode.visible.y < min.y)
	{
		/* TODO: It still might be better... */
		cx->mode = old_mode;
		return 0;
	}

	if (ggiSetMode(cx->stem, &cx->mode)) {
		cx->mode = old_mode;
		if (ggiSetMode(cx->stem, &cx->mode))
			return -1;

		ggiSetColorfulPalette(cx->stem);
#ifdef GGIWMHFLAG_CATCH_CLOSE
		ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
		ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif
		return 0;
	}

	ggiSetColorfulPalette(cx->stem);
#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	return 2;
}

ggi_widget_t
add_scroller(ggi_widget_t child, int sx, int sy)
{
	int x;
	int y;

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
attach_and_fit_visual(struct connection *cx, ggi_widget_t widget,
	void (*hook)(ggi_widget_t, void *), void *data)
{
	int resize;
	ggi_widget_t visualanchor = cx->visualanchor;

	ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, widget,
		0, 0,
		cx->mode.visible.x, cx->mode.visible.y,
		0);
	ggiWidgetRedrawWidgets(visualanchor);

	if (hook)
		hook(widget, data);

	if (cx->mode.visible.x >= widget->min.x &&
		cx->mode.visible.y >= widget->min.y)
	{
		return widget;
	}

	debug(2, "Trying to resize (min %dx%d, have %dx%d)\n",
		widget->min.x, widget->min.y,
		cx->mode.visible.x, cx->mode.visible.y);

	resize = try_to_resize(cx, widget->min, widget->opt);
	if (resize < 0)
		return NULL;

	if (resize > 0) {
		ggiWidgetUnlinkChild(visualanchor,
			GWT_UNLINK_BY_WIDGETPTR, widget);
		ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, widget,
			0, 0,
			cx->mode.visible.x, cx->mode.visible.y,
			0);
	}

	if (resize >= 2)
		return widget;

	ggiWidgetRedrawWidgets(visualanchor);

	if (hook)
		hook(widget, data);

	ggiWidgetUnlinkChild(visualanchor, GWT_UNLINK_BY_WIDGETPTR, widget);
	widget = add_scroller(widget, cx->mode.visible.x, cx->mode.visible.y);
	ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, widget,
		0, 0,
		cx->mode.visible.x, cx->mode.visible.y,
		0);

	return widget;
}

static ggi_widget_t
dlg_scroller(ggi_widget_t dlg, int *w, int *h, int sx, int sy)
{
	int x = 0;
	int y = 0;
	int wx, wy;

	wx = *w;
	wy = *h;
	sx -= 4;
	sy -= 4;
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
	struct connection *cx = ctx->cx;

	if (ctx->behind &&
		ggiGetDisplayFrame(cx->stem) != ggiGetReadFrame(cx->stem))
	{
		ggiGetBox(cx->stem,
			ctx->dlg->pos.x, ctx->dlg->pos.y,
			ctx->dlg->size.x, ctx->dlg->size.y,
			ctx->behind);
	}

	GWT_SET_ICHANGED(ctx->dlg);
	ggiWidgetRedrawWidgets(ctx->dlg);
}

static void
dlg_post_flush(void *data)
{
	struct flush_ctx *ctx = (struct flush_ctx *)data;
	struct connection *cx = ctx->cx;

	if (ctx->behind &&
		ggiGetDisplayFrame(cx->stem) != ggiGetReadFrame(cx->stem))
	{
		ggiPutBox(cx->stem,
			ctx->dlg->pos.x, ctx->dlg->pos.y,
			ctx->dlg->size.x, ctx->dlg->size.y,
			ctx->behind);
	}
}

ggi_widget_t
popup_dialog(struct connection *cx, ggi_widget_t dlg, int *done,
	int (*hook)(struct connection *cx, void *), void *data)
{
	ggi_widget_t scroller;
	ggi_widget_t frame;
	ggi_widget_t visualanchor = cx->visualanchor;
	int x, y, w, h;
	struct flush_ctx flush_ctx = { NULL, NULL, NULL };
	int w_frame;

	ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, dlg,
		0, 0, 0, 0, 0);

	ggiWidgetRedrawWidgets(visualanchor);
	w = dlg->min.x;
	h = dlg->min.y;
	ggiWidgetUnlinkChild(visualanchor, GWT_UNLINK_BY_WIDGETPTR, dlg);

	if (cx->mode.visible.x - 4 < w || cx->mode.visible.y - 4 < h) {
		scroller = dlg_scroller(dlg, &w, &h,
			cx->mode.visible.x, cx->mode.visible.y);
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

	x = (cx->mode.visible.x - w) / 2;
	y = (cx->mode.visible.y - h) / 2;

	w_frame = ggiGetWriteFrame(cx->stem);
	ggiSetWriteFrame(cx->stem, ggiGetDisplayFrame(cx->stem));
	ggiSetReadFrame(cx->stem, ggiGetDisplayFrame(cx->stem));

	if (!cx->wire_stem ||
		x + w > cx->offset.x + cx->area.x ||
		y + h > cx->offset.y + cx->area.y)
	{
		flush_ctx.behind =
			malloc(w * h * GT_ByPP(cx->mode.graphtype));
		if (!flush_ctx.behind) {
			*done = 1;
			return dlg;
		}
		ggiGetBox(cx->stem, x, y, w, h, flush_ctx.behind);
	}

	ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, dlg,
		x, y, w, h, 0);
	ggiWidgetRedrawWidgets(visualanchor);

	ggiSetWriteFrame(cx->stem, w_frame);
	ggiSetReadFrame(cx->stem, w_frame);

	cx->flush_hook = dlg_flush;
	cx->post_flush_hook = dlg_post_flush;
	flush_ctx.cx = cx;
	flush_ctx.dlg = dlg;
	cx->flush_hook_data = &flush_ctx;

	while (!*done && !cx->close_connection) {
		gii_event event;
		if (hook) {
			if (hook(cx, data))
				continue;
		}
		giiEventRead(cx->stem, &event, emAll);

		w_frame = ggiGetWriteFrame(cx->stem);
		ggiSetWriteFrame(cx->stem, ggiGetDisplayFrame(cx->stem));
		ggiSetReadFrame(cx->stem, ggiGetDisplayFrame(cx->stem));

		ggiWidgetProcessEvent(visualanchor, &event);
		ggiWidgetRedrawWidgets(visualanchor);

		ggiSetWriteFrame(cx->stem, w_frame);
		ggiSetReadFrame(cx->stem, w_frame);

		switch(event.any.type) {
		case evCommand:
			if (event.cmd.origin == GII_EV_ORIGIN_SENDEVENT) {
				switch (event.cmd.code) {
				case UPLOAD_FILE_FRAGMENT_CMD:
					file_upload_fragment(cx);
					break;
				}
			}
			break;
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

	cx->flush_hook = NULL;
	cx->post_flush_hook = NULL;
	cx->flush_hook_data = NULL;

	ggiWidgetUnlinkChild(visualanchor, GWT_UNLINK_BY_WIDGETPTR, dlg);

	if (flush_ctx.behind) {
		w_frame = ggiGetWriteFrame(cx->stem);
		ggiSetWriteFrame(cx->stem, ggiGetDisplayFrame(cx->stem));
		ggiSetReadFrame(cx->stem, ggiGetDisplayFrame(cx->stem));

		ggiPutBox(cx->stem, x, y, w, h, flush_ctx.behind);
		free(flush_ctx.behind);

		ggiSetWriteFrame(cx->stem, w_frame);
		ggiSetReadFrame(cx->stem, w_frame);
	}
	if (cx->wire_stem) {
		ggiSetGCClipping(cx->stem,
			cx->offset.x, cx->offset.y,
			cx->offset.x + cx->area.x, cx->offset.y + cx->area.y);
		ggiCrossBlit(cx->wire_stem,
			cx->slide.x + x, cx->slide.y + y,
			w, h,
			cx->stem,
			cx->offset.x + x, cx->offset.y + y);
		ggiSetGCClipping(cx->stem,
			0, 0, cx->mode.virt.x, cx->mode.virt.y);
	}

	ggiFlushRegion(cx->stem, x, y, w, h);

	return dlg;
}
