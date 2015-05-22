/*
******************************************************************************

   VNC viewer open connection dialog handling.

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

#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <math.h>
#include <ggi/gg.h>
#include <ggi/gii.h>
#include <ggi/ggi.h>
#include <ggi/ggi_widget.h>
#include <ggi/ggi_widget_highlevel.h>
#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "vnc.h"
#include "handshake.h"
#include "vnc-compat.h"
#include "vnc-debug.h"
#include "dialog.h"

struct ctx {
	struct connection *cx;
	int done;
	int pixfmt[4];
	int pf_opt;
	char pf_custom[30];
	int endian_opt;
	int protocol_opt;
	int family_opt;
	int expert;
	ggi_widget_t dialog;
	ggi_widget_t connect;
	ggi_widget_t tab;
	ggi_widget_t security_page;
	ggi_widget_t encoding_page;
};

struct move_encodings {
	struct ctx *ctx;
	int32_t encoding;
	const char *label;
	ggi_widget_t to;
	ggi_widget_t from;
	int free_cb_priv;
};


static void
cb_ok(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct ctx *ctx = widget->callbackpriv;
	struct connection *cx = ctx->cx;

	if (!cx->server_port[0]) {
		ggiWidgetSetTreeState(ctx->connect, GWT_STATE_INACTIVE, 1);
		return;
	}

	if (cbt != GWT_CB_ACTIVATE) {
		ggiWidgetSetTreeState(ctx->connect, GWT_STATE_INACTIVE, 0);
		return;
	}

	switch (parse_port(cx)) {
	case -2:
		ctx->done = -2;
		return;
	case -1:
		return;
	}

	if (ctx->pixfmt[0])
		strcpy(cx->wire_pixfmt, "local");
	else if (ctx->pixfmt[1])
		strcpy(cx->wire_pixfmt, "server");
	else if (ctx->pixfmt[2]) {
		switch(ctx->pf_opt) {
		case 0:
			strcpy(cx->wire_pixfmt, "p5r1g1b1");
			break;
		case 1:
			strcpy(cx->wire_pixfmt, "p2r2g2b2");
			break;
		case 2:
			strcpy(cx->wire_pixfmt, "r3g3b2");
			break;
		case 3:
			strcpy(cx->wire_pixfmt, "c8");
			break;
		case 4:
			strcpy(cx->wire_pixfmt, "p1r5g5b5");
			break;
		case 5:
			strcpy(cx->wire_pixfmt, "r5g6b5");
			break;
		case 6:
			strcpy(cx->wire_pixfmt, "p8r8g8b8");
			break;
		default:
			return;
		}
	}
	else if (ctx->pixfmt[3]) {
		ggstrlcpy(cx->wire_pixfmt, ctx->pf_custom,
			sizeof(cx->wire_pixfmt));
		if (canonicalize_pixfmt(cx->wire_pixfmt,
			sizeof(cx->wire_pixfmt)))
		{
			return;
		}
	}
	else
		return;

	switch (ctx->endian_opt) {
	case 0:
		cx->wire_endian = -2;
		break;
	case 1:
		cx->wire_endian = -3;
		break;
	case 2:
		cx->wire_endian = 1;
		break;
	case 3:
		cx->wire_endian = 0;
		break;
	default:
		return;
	}

	switch (ctx->protocol_opt) {
	case 0:
		cx->max_protocol = 3;
		break;
	case 1:
		cx->max_protocol = 7;
		break;
	case 2:
		cx->max_protocol = 8;
		break;
	default:
		return;
	}

	switch (ctx->family_opt) {
	case 0:
		cx->net_family = 0;
		break;
	case 1:
		cx->net_family = 4;
		break;
	case 2:
		cx->net_family = 6;
		break;
	default:
		return;
	}

	ctx->done = 1;
}

static void
cb_about(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct ctx *ctx = widget->callbackpriv;
	struct connection *cx = ctx->cx;
	ggi_widget_t visualanchor = cx->visualanchor;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	ggiWidgetUnlinkChild(visualanchor, 0);
	show_about(cx);
	ggiWidgetLinkChild(visualanchor, GWT_BY_XYPOS, ctx->dialog,
		0, 0,
		cx->mode.visible.x, cx->mode.visible.y,
		0);
}

static void
cb_cancel(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct ctx *ctx = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	ctx->done = -1;
}

struct tab_ctrl {
	ggi_widget_t tab;
	ggi_widget_t sheet;
};

static void
cb_page(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct tab_ctrl *tab_ctrl = widget->callbackpriv;
	ggi_widget_t tab = tab_ctrl->tab;
	ggi_widget_t sheet = tab_ctrl->sheet;
	ggi_widget_t btn;
	int *page_number = sheet->statevar;
	int i;

	if (cbt != GWT_CB_STATECHANGE)
		return;

	for (i = 0; (btn = ggiWidgetGetChild(tab, i)); ++i) {
		if (btn == widget)
			break;
	}
	if (!btn)
		return;

	*page_number = i;
	GWT_SET_ICHANGED(sheet);
}

static inline ggi_widget_t
cleanup(ggi_widget_t widget)
{
	ggiWidgetDestroy(widget);
	return NULL;
}

static inline void
link_last_child(ggi_widget_t widget, ggi_widget_t child)
{
	ggiWidgetLinkChild(widget, GWT_LAST_CHILD, child);
}

static void 
cb_move_encodings(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus);

static ggi_widget_t
create_item(struct ctx *ctx, int32_t encoding, const char *label,
	ggi_widget_t to, ggi_widget_t from, struct move_encodings *me)
{
	ggi_widget_t item;

	item = ggiWidgetCreateLabel(label);
	if (!item)
		return NULL;
	item->gravity = GWT_GRAV_WEST;
	item->pad.t = item->pad.l = 1;
	item->pad.b = item->pad.r = 1;

	if (!me) {
		me = malloc(sizeof(*me));
		if (!me)
			return cleanup(item);
	}
	me->ctx = ctx;
	me->encoding = encoding;
	me->label = label;
	me->to = to;
	me->from = from;
	me->free_cb_priv = 1;

	item->callbackpriv = me;
	item->callback = cb_move_encodings;

	item->state |= GWT_STATE_IS_SELECTABLE;

	return item;
}

static void
cb_move_encodings(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct move_encodings *move = widget->callbackpriv;
	int i;
	ggi_widget_t child, list;
	ggi_widget_t to, from;
	struct move_encodings *me;

	if (cbt == GWT_CB_DESTROY) {
		if (move && move->free_cb_priv)
			free(move);
		return;
	}

	if (cbt != GWT_CB_ACTIVATE)
		return;

	to = move->to;
	from = move->from;

	i = -1;
	for (;;) {
		child = ggiWidgetGetChild(from, ++i);
		if (!child)
			break;

		if (!(child->state & GWT_STATE_IS_SELECTED))
			continue;

		to->pad.b -= child->min.y;
		from->pad.b += child->min.y;

		me = child->callbackpriv;
		me->free_cb_priv = 0;

		ggiWidgetSendControl(from, GWT_CONTROLFLAG_NONE,
			"DELETEROW", i--);
		GWT_SET_ICHANGED(from);

		list = create_item(me->ctx, me->encoding, me->label,
			me->from, me->to, me);
		if (!list) {
			me->ctx->done = -2;
			return;
		}

		ggiWidgetSendControl(to, GWT_CONTROLFLAG_NONE,
			"INSERTROW", GWT_LAST_CHILD);
		link_last_child(to, list);
		GWT_SET_ICHANGED(to);
	}
}

static void
cb_expert(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct ctx *ctx = widget->callbackpriv;
	int *expert = widget->statevar;
	ggi_widget_t item;
	ggi_widget_t list = ctx->tab->parent;

	if (cbt != GWT_CB_STATECHANGE)
		return;

	if (*expert) {
		if (ggiWidgetGetChild(ctx->tab, 1) == ctx->security_page)
			return;

		ggiWidgetLinkChild(ctx->tab, 1, ctx->security_page);
		ggiWidgetLinkChild(ctx->tab, 2, ctx->encoding_page);

		item = ggiWidgetUnlinkChild(list, GWT_LAST_CHILD);
		ggiWidgetDestroy(item);

		item = ggiWidgetCreateColoredPatch(10, 2,
			&(ggiWidgetGetPalette(list)
				->col[GWT_COLOR_SHADOW_LIGHT]));
		if (!item) {
			ctx->done = -2;
			return;
		}
		item->gravity = GWT_GRAV_SOUTH;
		link_last_child(list, item);
	}
	else {
		if (ggiWidgetGetChild(ctx->tab, 1) != ctx->security_page)
			return;

		item = ggiWidgetCreateLabel("");
		if (!item) {
			ctx->done = -2;
			return;
		}
		ggiWidgetLinkChild(ctx->tab, 1, item);
		item = ggiWidgetCreateLabel("");
		if (!item) {
			ctx->done = -2;
			return;
		}
		ggiWidgetLinkChild(ctx->tab, 2, item);

		item = ggiWidgetUnlinkChild(list, GWT_LAST_CHILD);
		ggiWidgetDestroy(item);

		item = ggiWidgetCreateColoredPatch(
			10 + ctx->security_page->min.x +
				ctx->encoding_page->min.x,
			2, &(ggiWidgetGetPalette(list)
				->col[GWT_COLOR_SHADOW_LIGHT]));
		if (!item) {
			ctx->done = -2;
			return;
		}
		item->gravity = GWT_GRAV_SOUTH;
		link_last_child(list, item);
	}

	item = ggiWidgetUnlinkChild(ctx->tab, 3);
	if (item != ctx->security_page)
		ggiWidgetDestroy(item);
	item = ggiWidgetUnlinkChild(ctx->tab, 3);
	if (item != ctx->encoding_page)
		ggiWidgetDestroy(item);

	GWT_SET_ICHANGED(ctx->tab);
}

static void
cb_sheet(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	ggi_widget_t child;
	int i;
	int *page = widget->statevar;

	if (cbt != GWT_CB_STATECHANGE)
		return;

	for (i = 0; (child = ggiWidgetGetChild(widget, i)); ++i)
		ggiWidgetSetTreeState(child, GWT_STATE_INACTIVE, i != *page);

	GWT_WIDGET_MAKE_ACTIVE(widget);
	GWT_SET_ICHANGED(widget);
}

struct adjust {
	struct connection *cx;
	ggi_widget_t used;
	ggi_widget_t unused;
	ggi_widget_t sec_used;
	ggi_widget_t sec_unused;
	int *min_y;
};

static void
adjust_size(ggi_widget_t widget, void *data)
{
	struct adjust *adj = data;
	ggi_widget_t used;
	ggi_widget_t unused;
	int i;

	used = adj->used;
	unused = adj->unused;

	for (i = 0; ggiWidgetGetChild(used, i); ++i);
	unused->pad.b = i * *adj->min_y;
	GWT_SET_ICHANGED(unused);

	for (i = 0; ggiWidgetGetChild(unused, i); ++i);
	used->pad.b = i * *adj->min_y;
	GWT_SET_ICHANGED(used);

	used = adj->sec_used;
	unused = adj->sec_unused;

	for (i = 0; ggiWidgetGetChild(used, i); ++i);
	unused->pad.b = i * *adj->min_y;
	GWT_SET_ICHANGED(unused);

	for (i = 0; ggiWidgetGetChild(unused, i); ++i);
	used->pad.b = i * *adj->min_y;
	GWT_SET_ICHANGED(used);

	ggiWidgetRedrawWidgets(adj->cx->visualanchor);
}

static void
cb_integer(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	double *state = (double *)widget->statevar;
	*state = round(*state);
}

static ggi_widget_t
create_checkbox(const char *text)
{
	ggi_widget_t label;
	ggi_widget_t item;

	label = ggiWidgetCreateLabel(text);
	if (!label)
		return NULL;
	label->pad.l = 2;

	item = ggiWidgetCreateCheckbox(label, NULL);
	if (!item)
		return cleanup(label);

	return item;
}

#ifdef HAVE_ZLIB
static ggi_widget_t
checkbox_widget(struct ctx *ctx)
{
	struct connection *cx = ctx->cx;
	ggi_widget_t cont;
	ggi_widget_t item;
	int i;

	cont = ggiWidgetCreateContainerLine(2, NULL);
	if (!cont)
		return NULL;
	cont->gravity = GWT_GRAV_WEST;

	item = create_checkbox("Compression");
	if (!item)
		return cleanup(cont);
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &cx->compression;
	link_last_child(cont, item);

	item = ggiWidgetCreateOdometer(25, 40.0, 320.0, 0.0, 9.0, 8, 0);
	if (!item)
		return cleanup(cont);
	item->statevar = &cx->compression_level;
	item->callback = cb_integer;
	link_last_child(cont, item);

	cx->compression = 0;
	cx->compression_level = 0.0;
	for (i = 0; i < cx->allowed_encodings; ++i) {
		if (-256 > cx->allow_encoding[i] ||
			cx->allow_encoding[i] > -247)
		{
			continue;
		}
		cx->compression = 1;
		cx->compression_level = cx->allow_encoding[i] + 256;
	}

#ifdef HAVE_JPEG
	item = create_checkbox("Quality");
	if (!item)
		return cleanup(cont);
	item->pad.l = 20;
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &cx->quality;
	link_last_child(cont, item);

	item = ggiWidgetCreateOdometer(25, 40.0, 320.0, 0.0, 9.0, 8, 0);
	if (!item)
		return cleanup(cont);
	item->statevar = &cx->quality_level;
	item->callback = cb_integer;
	link_last_child(cont, item);

	cx->quality = 0;
	cx->quality_level = 0.0;
	for (i = 0; i < cx->allowed_encodings; ++i) {
		if (-32 > cx->allow_encoding[i] ||
			cx->allow_encoding[i] > -23)
		{
			continue;
		}
		cx->quality = 1;
		cx->quality_level = cx->allow_encoding[i] + 32;
	}
#endif /* HAVE_JPEG */

	return cont;
}
#endif /* HAVE_ZLIB */

static ggi_widget_t
pixfmt_widget(struct ctx *ctx)
{
	struct connection *cx = ctx->cx;
	const char *options[] = {
		"r1g1b1",
		"r2g2b2",
		"r3g3b2",
		"c8    ",
		"r5g5b5",
		"r5g6b5",
		"r8g8b8",
		NULL
	};
	ggi_widget_t atom;
	ggi_widget_t item;
	ggi_widget_t cont;
	int i;

	ctx->pf_opt = 5;
	if (!cx->wire_pixfmt[0])
		ctx->pixfmt[0] = 1;
	else if (!strcmp(cx->wire_pixfmt, "local"))
		ctx->pixfmt[0] = 1;
	else if (!strcmp(cx->wire_pixfmt, "server"))
		ctx->pixfmt[1] = 1;
	else if (!strcmp(cx->wire_pixfmt, "p5r1g1b1")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 0;
	}
	else if (!strcmp(cx->wire_pixfmt, "p2r2g2b2")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 1;
	}
	else if (!strcmp(cx->wire_pixfmt, "r3g3b2")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 2;
	}
	else if (!strcmp(cx->wire_pixfmt, "c8")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 3;
	}
	else if (!strcmp(cx->wire_pixfmt, "p1r5g5b5")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 4;
	}
	else if (!strcmp(cx->wire_pixfmt, "r5g6b5")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 5;
	}
	else if (!strcmp(cx->wire_pixfmt, "p8r8g8b8")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 6;
	}
	else if (!strcmp(cx->wire_pixfmt, "weird")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 5;
	}
	else {
		ctx->pixfmt[3] = 1;
		ggstrlcpy(ctx->pf_custom, cx->wire_pixfmt,
			sizeof(ctx->pf_custom));
	}

	cont = ggiWidgetCreateContainerStack(0, NULL);
	if (!cont)
		return NULL;
	cont->gravity = GWT_GRAV_NORTH;

	item = ggiWidgetCreateLabel("Pixel Format");
	if (!item)
		return cleanup(cont);
	item->pad.b = 5;
	link_last_child(cont, item);

	atom = ggiWidgetCreateLabel("Local");
	if (!atom)
		return cleanup(cont);
	atom->pad.l = atom->pad.t = atom->pad.b = 2;
	item = ggiWidgetCreateRadiobutton(ctx->pixfmt, atom, NULL);
	if (!item) {
		cleanup(atom);
		return cleanup(cont);
	}
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &ctx->pixfmt[0];
	link_last_child(cont, item);

	atom = ggiWidgetCreateLabel("Server");
	if (!atom)
		return cleanup(cont);
	atom->pad.l = atom->pad.t = atom->pad.b = 2;
	item = ggiWidgetCreateRadiobutton(ctx->pixfmt, atom, NULL);
	if (!item) {
		cleanup(atom);
		return cleanup(cont);
	}
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &ctx->pixfmt[1];
	link_last_child(cont, item);

	atom = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	if (!atom)
		return cleanup(cont);
	atom->statevar = &ctx->pf_opt;
	item = ggiWidgetCreateRadiobutton(ctx->pixfmt, atom, NULL);
	if (!item) {
		cleanup(atom);
		return cleanup(cont);
	}
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &ctx->pixfmt[2];
	link_last_child(cont, item);
	for (i = 0; options[i]; ++i) {
		item = ggiWidgetCreateLabel(options[i]);
		if (!item)
			return cleanup(cont);
		GWT_WIDGET_MAKE_SELECTABLE(item);
		if (i == ctx->pf_opt)
			GWT_WIDGET_MAKE_SELECTED(item);
		link_last_child(atom, item);
	}

	atom = ggiWidgetCreateText(ctx->pf_custom, 8, 29);
	if (!atom)
		return cleanup(cont);
	item = ggiWidgetCreateRadiobutton(ctx->pixfmt, atom, NULL);
	if (!item) {
		cleanup(atom);
		return cleanup(cont);
	}
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &ctx->pixfmt[3];
	link_last_child(cont, item);

	return cont;
}

static ggi_widget_t
endian_widget(struct ctx *ctx)
{
	struct connection *cx = ctx->cx;
	const char *options[] = {
		"Local ",
		"Server",
		"Big   ",
		"Little",
		NULL
	};
	ggi_widget_t item;
	ggi_widget_t atom;
	ggi_widget_t cont;
	int i;

	switch (cx->wire_endian) {
	case 1:
		ctx->endian_opt = 2;
		break;
	case 0:
		ctx->endian_opt = 3;
		break;
	case -1:
	case -2:
		ctx->endian_opt = 0;
		break;
	case -3:
		ctx->endian_opt = 1;
		break;
	}

	cont = ggiWidgetCreateContainerStack(0, NULL);
	if (!cont)
		return NULL;
	cont->gravity = GWT_GRAV_NORTH;

	item = ggiWidgetCreateLabel("Endian");
	if (!item)
		return cleanup(cont);
	item->pad.b = 2;
	link_last_child(cont, item);

	item = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	if (!item)
		return cleanup(cont);
	item->statevar = &ctx->endian_opt;
	link_last_child(cont, item);

	for (i = 0; options[i]; ++i) {
		atom = ggiWidgetCreateLabel(options[i]);
		if (!atom)
			return cleanup(cont);
		GWT_WIDGET_MAKE_SELECTABLE(atom);
		if (i == ctx->endian_opt)
			GWT_WIDGET_MAKE_SELECTED(atom);
		link_last_child(item, atom);
	}

	return cont;
}

static ggi_widget_t
protocol_widget(struct ctx *ctx)
{
	struct connection *cx = ctx->cx;
	const char *options[] = {
		"3.3",
		"3.7",
		"3.8",
		NULL
	};
	ggi_widget_t item;
	ggi_widget_t atom;
	ggi_widget_t cont;
	int i;

	switch (cx->max_protocol) {
	case 3:
		ctx->protocol_opt = 0;
		break;
	case 7:
		ctx->protocol_opt = 1;
		break;
	case 8:
		ctx->protocol_opt = 2;
		break;
	}

	cont = ggiWidgetCreateContainerLine(10, NULL);
	if (!cont)
		return NULL;
	cont->gravity = GWT_GRAV_NORTH;

	item = ggiWidgetCreateLabel("RFB Protocol Version");
	if (!item)
		return cleanup(cont);
	link_last_child(cont, item);

	item = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	if (!item)
		return cleanup(cont);
	item->statevar = &ctx->protocol_opt;
	link_last_child(cont, item);
	for (i = 0; options[i]; ++i) {
		atom = ggiWidgetCreateLabel(options[i]);
		if (!atom)
			return cleanup(cont);
		GWT_WIDGET_MAKE_SELECTABLE(atom);
		if (i == ctx->protocol_opt)
			GWT_WIDGET_MAKE_SELECTED(atom);
		link_last_child(item, atom);
	}

	return cont;
}

#ifdef PF_INET6
static ggi_widget_t
family_widget(struct ctx *ctx)
{
	struct connection *cx = ctx->cx;
	const char *options[] = {
		"Any ",
		"IPv4",
		"IPv6",
		NULL
	};
	ggi_widget_t item;
	ggi_widget_t cont;
	ggi_widget_t atom;
	int i;

	switch (cx->net_family) {
	case 4:
		ctx->family_opt = 1;
		break;
	case 6:
		ctx->family_opt = 2;
		break;
	default:
		ctx->family_opt = 0;
	}

	cont = ggiWidgetCreateContainerLine(0, NULL);
	if (!cont)
		return NULL;
	cont->gravity = GWT_GRAV_NORTH;

	item = ggiWidgetCreateLabel("Protocol Family");
	if (!item)
		return cleanup(cont);
	item->pad.r = 10;
	link_last_child(cont, item);

	item = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	if (!item)
		return cleanup(cont);
	item->statevar = &ctx->family_opt;
	link_last_child(cont, item);
	for (i = 0; options[i]; ++i) {
		atom = ggiWidgetCreateLabel(options[i]);
		if (!atom)
			return cleanup(cont);
		GWT_WIDGET_MAKE_SELECTABLE(atom);
		if (i == ctx->family_opt)
			GWT_WIDGET_MAKE_SELECTED(atom);
		link_last_child(item, atom);
	}

	return cont;
}
#endif /* PF_INET6 */

static char *
get_config_file(const char *file)
{
	const char *home = get_appdata_path();
	char dot[] = ".";
	int len;
	char *path;
	struct stat st;

	if (home)
		dot[0] = '\0';
	else
		home = getenv("HOME");

	if (!home)
		return NULL;

	len = strlen(home) + sizeof("/." PACKAGE "/") + strlen(file);
	path = malloc(len);
	if (!path)
		return NULL;

	strcpy(path, home);
	strcat(path, "/");
	strcat(path, dot);
	strcat(path, PACKAGE);
	if (stat(path, &st))
		mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
	strcat(path, "/");
	strcat(path, file);
	return path;
}

static ggi_widget_t
server_widget(char *server, size_t length)
{
	ggi_widget_t vnc_server = NULL;
	char *recent_path = get_config_file("recent");
	FILE *recent = NULL;
	char recent_srv[1024];
	int size;
	int i;

	if (!recent_path)
		goto done;

	debug(2, "Opening recent file: \"%s\"\n", recent_path);
	recent = fopen(recent_path, "rt");
	if (!recent) {
		debug(1, "Failed to open recent file: \"%s\"\n", recent_path);
		goto done;
	}
	for (;;) {
		if (!fgets(server, length, recent))
			goto done;
		size = strlen(server);
		if (server[size - 1] == '\n')
			server[--size] = '\0';
		if (!size)
			continue;
		debug(2, "Recent server: \"%s\"\n", server);
		break;
	}
	vnc_server = ggiWidgetCreateTextHistory(server,
		32, length - 1, 1);
	if (!vnc_server)
		goto done;
	for (i = 0; i < 10; ++i) {
		if (!fgets(recent_srv, sizeof(recent_srv), recent))
			break;
		size = strlen(recent_srv);
		if (recent_srv[size - 1] == '\n')
			recent_srv[--size] = '\0';
		if (!size)
			continue;
		debug(2, "Recent server: \"%s\"\n", recent_srv);
		ggiWidgetSendControl(vnc_server, GWT_CONTROLFLAG_NONE,
			"ADDHISTORY", 0, recent_srv);
	}
	ggiWidgetSendControl(vnc_server, GWT_CONTROLFLAG_NONE,
		"ADDHISTORY", 0, "");
done:
	if (recent)
		fclose(recent);
	if (recent_path)
		free(recent_path);
	if (!vnc_server) {
		memset(server, 0, length);
		vnc_server = ggiWidgetCreateText(server, 36, length - 1);
	}

	return vnc_server;
}

static void
update_recent_file(struct connection *cx)
{
	/* Store server in recent servers file. */
	char *recent_new_path = get_config_file("recent_XXXXXX");
	char *recent_path = get_config_file("recent");
	int recent_new_fd;
	FILE *recent = NULL;
	FILE *recent_new = NULL;
	char recent_srv[1024];
	int size;
	int count = 0;

	debug(1, "Most recent server: %s\n", cx->server_port);
	if (!recent_new_path || !recent_path) {
		debug(1, "Failed to generate config path.\n");
		goto fail;
	}

	recent = fopen(recent_path, "r");
	while (recent) {
		if (!fgets(recent_srv, sizeof(recent_srv), recent)) {
			fclose(recent);
			recent = NULL;
			break;
		}
		size = strlen(recent_srv);
		if (recent_srv[size - 1] == '\n')
			recent_srv[--size] = '\0';
		if (!size)
			continue;
		if (!strcmp(recent_srv, cx->server_port)) {
			debug(1, "Same server as last time.\n");
			goto done;
		}
		break;
	}
	recent_new_fd = mkstemp(recent_new_path);
	if (recent_new_fd < 0) {
		debug(1, "Failed to open new recent server file.\n");
		goto fail;
	}
	debug(1, "Opened new temporary recent file \"%s\".\n",
		recent_new_path);
	recent_new = fdopen(recent_new_fd, "w");
	if (!recent_new) {
		debug(1, "Failed to fdopen new recent file.\n");
		close(recent_new_fd);
		unlink(recent_new_path);
		goto fail;
	}
	if (fputs(cx->server_port, recent_new) < 0) {
		debug(1, "Failed to write recent server.\n");
		goto fail;
	}
	if (fputc('\n', recent_new) < 0) {
		debug(1, "Failed to write NL in recent.\n");
		goto fail;
	}
	++count;
	debug(2, "New recent server: \"%s\"\n", cx->server_port);
	if (recent) {
		if (fputs(recent_srv, recent_new) < 0) {
			debug(1, "Failed to write recent server.\n");
			goto fail;
		}
		if (fputc('\n', recent_new) < 0) {
			debug(1, "Failed to write NL in recent.\n");
			goto fail;
		}
		++count;
		debug(2, "New recent server: \"%s\"\n", recent_srv);
	}
	while (recent && count < 50) {
		if (!fgets(recent_srv, sizeof(recent_srv), recent))
			break;
		size = strlen(recent_srv);
		if (recent_srv[size - 1] == '\n')
			recent_srv[--size] = '\0';
		if (!size)
			continue;
		if (!strcmp(recent_srv, cx->server_port))
			continue;
		if (fputs(recent_srv, recent_new) < 0) {
			debug(1, "Failed to write recent server.\n");
			goto fail;
		}
		if (fputc('\n', recent_new) < 0) {
			debug(1, "Failed to write NL in recent.\n");
			goto fail;
		}
		++count;
		debug(2, "New recent server: \"%s\"\n", recent_srv);
	}
	fclose(recent_new);
	recent_new = NULL;
	if (recent) {
		fclose(recent);
		recent = NULL;
	}
	if (rename(recent_new_path, recent_path)) {
		debug(1, "Failed to rename recent.\n");
		unlink(recent_new_path);
		goto fail;
	}
	goto done;
fail:
	debug(1, "Failed to update recent servers.\n");
	if (recent_new) {
		fclose(recent_new);
		recent_new = NULL;
		unlink(recent_new_path);
	}
done:
	if (recent)
		fclose(recent);
	if (recent_path)
		free(recent_path);
	if (recent_new_path)
		free(recent_new_path);
}

static ggi_widget_t
server_page(struct ctx *ctx, ggi_widget_t *focus, ggi_widget_t *expert)
{
	struct connection *cx = ctx->cx;
	ggi_widget_t params;
	ggi_widget_t cont;
	ggi_widget_t item;
	ggi_widget_t vnc_server;
	static char server[1024];

	params = ggiWidgetCreateContainerStack(4, NULL);
	if (!params)
		return NULL;
	params->pad.t = 15;
	params->gravity = GWT_GRAV_NORTH;

	item = ggiWidgetCreateLabel("VNC Server");
	if (!item)
		return cleanup(params);
	link_last_child(params, item);

	vnc_server = server_widget(server, sizeof(server));
	if (!vnc_server)
		return cleanup(params);
	vnc_server->callback = cb_ok;
	vnc_server->callbackpriv = ctx;
	cx->server_port = server;
	link_last_child(params, vnc_server);

	item = ggiWidgetCreateLabel("(host, host:display or host::port)");
	if (!item)
		return cleanup(params);
	link_last_child(params, item);

#ifdef PF_INET6
	item = family_widget(ctx);
	if (!item)
		return cleanup(params);
	link_last_child(params, item);
#endif

	cont = ggiWidgetCreateContainerStack(2, NULL);
	if (!cont)
		return cleanup(params);
	cont->pad.t = 15;
	link_last_child(params, cont);

	item = create_checkbox("Request Shared Session");
	if (!item)
		return cleanup(params);
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &cx->shared;
	link_last_child(cont, item);

	item = create_checkbox("View Only (inputs ignored)");
	if (!item)
		return cleanup(params);
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &cx->no_input;
	link_last_child(cont, item);

	item = create_checkbox("Show More Options");
	if (!item)
		return cleanup(params);
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &ctx->expert;
	item->callback = cb_expert;
	item->callbackpriv = ctx;
	link_last_child(cont, item);
	*expert = item;

	*focus = vnc_server;
	return params;
}

static ggi_widget_t
security_page(struct ctx *ctx, struct adjust *adj)
{
	struct connection *cx = ctx->cx;
	ggi_widget_t cont;
	ggi_widget_t atom;
	ggi_widget_t item;
	ggi_widget_t used, unused;
	ggi_widget_t move;
	ggi_widget_t label1, label2;
	ggi_widget_t button1, button2;
	ggi_widget_t list = NULL;
	struct move_encodings *move_left;
	struct move_encodings *move_right;
	int i, j;
	static struct ggiWidgetImage left = {
		{ 15, 9 },
		GWT_IF_BITMAP,
		(const unsigned char *)
		"\x00\x00\x0c\x00\x18\x00\x30\x00\x67\xfc"
		"\x30\x00\x18\x00\x0c\x00\x00\x00",
		GWT_IF_NONE, NULL
	};
	static struct ggiWidgetImage right = {
		{ 15, 9 },
		GWT_IF_BITMAP,
		(const unsigned char *)
		"\x00\x00\x00\x30\x00\x18\x00\x0c\x7f\xe6"
		"\x00\x0c\x00\x18\x00\x30\x00\x00",
		GWT_IF_NONE, NULL
	};


	cont = ggiWidgetCreateContainerStack(4, NULL);
	if (!cont)
		return NULL;
	cont->pad.t = 15;
	cont->gravity = GWT_GRAV_NORTH;

	item = ggiWidgetCreateContainerLine(10, NULL);
	if (!item)
		return cleanup(cont);
	link_last_child(cont, item);

	used = ggiWidgetCreateContainerStack(0, NULL);
	if (!used)
		return cleanup(cont);
	used->gravity = GWT_GRAV_NORTH;
	link_last_child(item, used);

	label1 = ggiWidgetCreateLabel("Allowed");
	if (!label1)
		return cleanup(cont);
	label1->pad.b = 1;
	link_last_child(used, label1);

	label2 = ggiWidgetCreateLabel("Security Types");
	if (!label2)
		return cleanup(cont);
	link_last_child(used, label2);

	adj->sec_used = ggiWidgetCreateContainerGrid(1, 0, 0, 0);
	if (!adj->sec_used)
		return cleanup(cont);
	adj->sec_used->gravity = GWT_GRAV_NORTH;
	adj->sec_used->pad.t = 5;
	link_last_child(used, adj->sec_used);

	move = ggiWidgetCreateContainerStack(5, NULL);
	if (!move)
		return cleanup(cont);
	move->pad.t = 20;
	move->gravity = GWT_GRAV_NORTH;
	link_last_child(item, move);

	button1 = ggiWidgetMakeImagebutton(&left);
	if (!button1)
		return cleanup(cont);
	button1->callback = cb_move_encodings;
	link_last_child(move, button1);
	move_left = malloc(sizeof(*move_left));
	if (!move_left)
		return cleanup(cont);
	button1->callbackpriv = move_left;
	move_left->free_cb_priv = 1;

	button2 = ggiWidgetMakeImagebutton(&right);
	if (!button2)
		return cleanup(cont);
	button2->callback = cb_move_encodings;
	link_last_child(move, button2);
	move_right = malloc(sizeof(*move_right));
	if (!move_right)
		return cleanup(cont);
	button2->callbackpriv = move_right;
	move_right->free_cb_priv = 1;

	unused = ggiWidgetCreateContainerStack(0, NULL);
	if (!unused)
		return cleanup(cont);
	unused->gravity = GWT_GRAV_NORTH;
	link_last_child(item, unused);

	label1 = ggiWidgetCreateLabel("Disallowed");
	if (!label1)
		return cleanup(cont);
	label1->pad.b = 1;
	link_last_child(unused, label1);

	label2 = ggiWidgetCreateLabel("Security Types");
	if (!label2)
		return cleanup(cont);
	link_last_child(unused, label2);

	adj->sec_unused = ggiWidgetCreateContainerGrid(1, 0, 0, 0);
	if (!adj->sec_unused)
		return cleanup(cont);
	adj->sec_unused->gravity = GWT_GRAV_NORTH;
	adj->sec_unused->pad.t = 5;
	link_last_child(unused, adj->sec_unused);

	list = ggiWidgetCreateContainerStack(2, NULL);
	if (!list)
		return cleanup(cont);
	list->pad.t = 15;
	link_last_child(cont, list);

	atom = ggiWidgetCreateContainerStack(2, NULL);
	if (!atom)
		return cleanup(cont);
	atom->pad.l = 2;

	item = ggiWidgetCreateCheckbox(atom, NULL);
	if (!item) {
		cleanup(atom);
		return cleanup(cont);
	}
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &cx->force_security;
	link_last_child(list, item);

	item = ggiWidgetCreateLabel("As a last resort, try to force");
	if (!item)
		return cleanup(cont);
	link_last_child(atom, item);

	item = ggiWidgetCreateLabel("the first allowed security type");
	if (!item)
		return cleanup(cont);
	link_last_child(atom, item);

	ggiWidgetSendControl(adj->sec_used, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_Y);
	ggiWidgetSendControl(adj->sec_used, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_MULTI);
	for (i = 0; cx->allow_security[i]; ++i) {
		list = create_item(ctx, cx->allow_security[i],
			security_name(cx->allow_security[i]),
			adj->sec_unused, adj->sec_used, NULL);
		if (!list)
			return cleanup(cont);
		ggiWidgetSendControl(adj->sec_used,
			GWT_CONTROLFLAG_NONE,
			"INSERTROW", GWT_LAST_CHILD);
		link_last_child(adj->sec_used, list);
	}

	ggiWidgetSendControl(adj->sec_unused, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_Y);
	ggiWidgetSendControl(adj->sec_unused, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_MULTI);
	for (j = 0; security_types[j].number; ++j) {
		if (!security_types[j].weight)
			continue;
		for (i = 0; cx->allow_security[i]; ++i) {
			if (cx->allow_security[i] == security_types[j].number)
				break;
		}
		if (cx->allow_security[i])
			continue;
		list = create_item(ctx, security_types[j].number,
			security_name(security_types[j].number),
			adj->sec_used, adj->sec_unused, NULL);
		if (!list)
			return cleanup(cont);
		ggiWidgetSendControl(adj->sec_unused, GWT_CONTROLFLAG_NONE,
			"INSERTROW", GWT_LAST_CHILD);
		link_last_child(adj->sec_unused, list);
	}

	move_left->ctx = ctx;
	move_left->to = adj->sec_used;
	move_left->from = adj->sec_unused;
	move_right->ctx = ctx;
	move_right->to = adj->sec_unused;
	move_right->from = adj->sec_used;

	adj->min_y = &list->min.y;

	return cont;
}

static ggi_widget_t
encodings_page(struct ctx *ctx, struct adjust *adj)
{
	struct connection *cx = ctx->cx;
	ggi_widget_t cont;
	ggi_widget_t item;
	ggi_widget_t used, unused;
	ggi_widget_t move;
	ggi_widget_t label1, label2;
	ggi_widget_t button1, button2;
	ggi_widget_t list = NULL;
	struct move_encodings *move_left;
	struct move_encodings *move_right;
	const int32_t *def_enc;
	int def_enc_count;
	int i, j;
	static struct ggiWidgetImage left = {
		{ 15, 9 },
		GWT_IF_BITMAP,
		(const unsigned char *)
		"\x00\x00\x0c\x00\x18\x00\x30\x00\x67\xfc"
		"\x30\x00\x18\x00\x0c\x00\x00\x00",
		GWT_IF_NONE, NULL
	};
	static struct ggiWidgetImage right = {
		{ 15, 9 },
		GWT_IF_BITMAP,
		(const unsigned char *)
		"\x00\x00\x00\x30\x00\x18\x00\x0c\x7f\xe6"
		"\x00\x0c\x00\x18\x00\x30\x00\x00",
		GWT_IF_NONE, NULL
	};


	cont = ggiWidgetCreateContainerStack(4, NULL);
	if (!cont)
		return NULL;

	item = create_checkbox("Automatic encoding selection");
	if (!item)
		return cleanup(cont);
	item->gravity = GWT_GRAV_WEST;
	item->statevar = &cx->auto_encoding;
	link_last_child(cont, item);

#ifdef HAVE_ZLIB
	item = checkbox_widget(ctx);
	if (!item)
		return cleanup(cont);
	link_last_child(cont, item);
#endif

	item = ggiWidgetCreateContainerLine(10, NULL);
	if (!item)
		return cleanup(cont);
	item->pad.t = 5;
	link_last_child(cont, item);

	used = ggiWidgetCreateContainerStack(0, NULL);
	if (!used)
		return cleanup(cont);
	used->gravity = GWT_GRAV_NORTH;
	link_last_child(item, used);

	label1 = ggiWidgetCreateLabel("Requested");
	if (!label1)
		return cleanup(cont);
	label1->pad.b = 1;
	link_last_child(used, label1);

	label2 = ggiWidgetCreateLabel("Encodings");
	if (!label2)
		return cleanup(cont);
	link_last_child(used, label2);

	adj->used = ggiWidgetCreateContainerGrid(1, 0, 0, 0);
	if (!adj->used)
		return cleanup(cont);
	adj->used->gravity = GWT_GRAV_NORTH;
	adj->used->pad.t = 5;
	link_last_child(used, adj->used);

	move = ggiWidgetCreateContainerStack(5, NULL);
	if (!move)
		return cleanup(cont);
	move->pad.t = 20;
	move->gravity = GWT_GRAV_NORTH;
	link_last_child(item, move);

	button1 = ggiWidgetMakeImagebutton(&left);
	if (!button1)
		return cleanup(cont);
	button1->callback = cb_move_encodings;
	link_last_child(move, button1);
	move_left = malloc(sizeof(*move_left));
	if (!move_left)
		return cleanup(cont);
	button1->callbackpriv = move_left;
	move_left->free_cb_priv = 1;

	button2 = ggiWidgetMakeImagebutton(&right);
	if (!button2)
		return cleanup(cont);
	button2->callback = cb_move_encodings;
	link_last_child(move, button2);
	move_right = malloc(sizeof(*move_right));
	if (!move_right)
		return cleanup(cont);
	button2->callbackpriv = move_right;
	move_right->free_cb_priv = 1;

	unused = ggiWidgetCreateContainerStack(0, NULL);
	if (!unused)
		return cleanup(cont);
	unused->gravity = GWT_GRAV_NORTH;
	link_last_child(item, unused);

	label1 = ggiWidgetCreateLabel("Leftover");
	if (!label1)
		return cleanup(cont);
	label1->pad.b = 1;
	link_last_child(unused, label1);

	label2 = ggiWidgetCreateLabel("Encodings");
	if (!label2)
		return cleanup(cont);
	link_last_child(unused, label2);

	adj->unused = ggiWidgetCreateContainerGrid(1, 0, 0, 0);
	if (!adj->unused)
		return cleanup(cont);
	def_enc_count = get_default_encodings(&def_enc);
	adj->unused->gravity = GWT_GRAV_NORTH;
	adj->unused->pad.t = 5;
	link_last_child(unused, adj->unused);

	ggiWidgetSendControl(adj->used, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_Y);
	ggiWidgetSendControl(adj->used, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_MULTI);
	for (i = 0; i < cx->allowed_encodings; ++i) {
		if (-32 <= cx->allow_encoding[i] &&
			cx->allow_encoding[i] <= -23)
		{
			continue;
		}
		if (-256 <= cx->allow_encoding[i] &&
			cx->allow_encoding[i] <= -247)
		{
			continue;
		}
		list = create_item(ctx, cx->allow_encoding[i],
			lookup_encoding(cx->allow_encoding[i]),
			adj->unused, adj->used, NULL);
		if (!list)
			return cleanup(cont);
		ggiWidgetSendControl(adj->used, GWT_CONTROLFLAG_NONE,
			"INSERTROW", GWT_LAST_CHILD);
		link_last_child(adj->used, list);
	}

	ggiWidgetSendControl(adj->unused, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_Y);
	ggiWidgetSendControl(adj->unused, GWT_CONTROLFLAG_NONE,
		"SETOPTIONS", GWT_GRID_OPTION_SELECT_MULTI);
	for (j = 0; j < def_enc_count; ++j) {
		for (i = 0; i < cx->allowed_encodings; ++i) {
			if (cx->allow_encoding[i] == def_enc[j])
				break;
		}
		if (i < cx->allowed_encodings)
			continue;
		if (-32 <= def_enc[j] && def_enc[j] <= -23)
			continue;
		if (-256 <= def_enc[j] && def_enc[j] <= -247)
			continue;
		list = create_item(ctx, def_enc[j],
			lookup_encoding(def_enc[j]),
			adj->used, adj->unused, NULL);
		if (!list)
			return cleanup(cont);
		ggiWidgetSendControl(adj->unused, GWT_CONTROLFLAG_NONE,
			"INSERTROW", GWT_LAST_CHILD);
		link_last_child(adj->unused, list);
	}

	move_left->ctx = ctx;
	move_left->to = adj->used;
	move_left->from = adj->unused;
	move_right->ctx = ctx;
	move_right->to = adj->unused;
	move_right->from = adj->used;

	adj->min_y = &list->min.y;

	return cont;
}

static ggi_widget_t
misc_page(struct ctx *ctx)
{
	ggi_widget_t cont;
	ggi_widget_t item;
	ggi_widget_t atom;

	cont = ggiWidgetCreateContainerStack(10, NULL);
	if (!cont)
		return NULL;
	cont->pad.t = 15;
	cont->gravity = GWT_GRAV_NORTH;

	item = ggiWidgetCreateContainerLine(40, NULL);
	if (!item)
		return cleanup(cont);
	item->gravity = GWT_GRAV_WEST;
	link_last_child(cont, item);

	atom = pixfmt_widget(ctx);
	if (!atom)
		return cleanup(cont);
	link_last_child(item, atom);

	atom = endian_widget(ctx);
	if (!atom)
		return cleanup(cont);
	link_last_child(item, atom);

	item = protocol_widget(ctx);
	if (!item)
		return cleanup(item);
	item->gravity = GWT_GRAV_WEST;
	link_last_child(cont, item);

	return cont;
}

static ggi_widget_t
tab_button(const char *text, struct tab_ctrl *tab_ctrl)
{
	ggi_widget_t item;
	ggi_widget_t atom;

	atom = ggiWidgetCreateLabel(text);
	if (!atom)
		return NULL;
	atom->pad.t = atom->pad.b = 3;
	atom->pad.l = atom->pad.r = 5;
	item = ggiWidgetCreateButton(atom);
	if (!item)
		return cleanup(atom);

	ggiWidgetSendControl(item, GWT_CONTROLFLAG_NONE,
		"SETPRESSEDTYPE", GWT_FRAMETYPE_TAB_ACTIVE);
	ggiWidgetSendControl(item, GWT_CONTROLFLAG_NONE,
		"SETSELECTEDTYPE", GWT_FRAMETYPE_TAB_INACTIVE);
	ggiWidgetSendControl(item, GWT_CONTROLFLAG_NONE,
		"SETDESELECTEDTYPE", GWT_FRAMETYPE_TAB_INACTIVE);
	ggiWidgetSendControl(item, GWT_CONTROLFLAG_NONE,
		"SETSTICKY", tab_ctrl);

	item->callback = cb_page;
	item->callbackpriv = tab_ctrl;

	return item;
}

int
get_connection(struct connection *cx)
{
	ggi_widget_t cont;
	ggi_widget_t item;
	ggi_widget_t atom;
	ggi_widget_t vnc_server;
	ggi_widget_t expert = NULL;
	ggi_widget_t visualanchor;
	struct ctx ctx = {
		NULL, 0, { 0, 0, 0, 0 }, 5, "", 0, 0, 0, 0,
		NULL, NULL, NULL, NULL, NULL
	};
	struct adjust adj = { NULL, NULL, NULL, NULL, NULL, NULL };
	int i;
	int page;
	int active;
	int result = 0;
	struct tab_ctrl tab_ctrl;

	ctx.cx = cx;
	ctx.expert = cx->expert;
	adj.cx = cx;

	if (cx->auto_reconnect && cx->server_port)
		return 0;

	cx->width = 314;
	cx->height = 314;
#if !defined HAVE_ZLIB && !defined HAVE_OPENSSL
	cx->height -= 40;
#endif

	if (open_visual(cx))
		return 2;
	select_mode(cx);

	ggiCheckMode(cx->stem, &cx->mode);

	if (ggiSetMode(cx->stem, &cx->mode)) {
		close_visual(cx);
		return 1;
	}

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	if (set_title(cx)) {
		close_visual(cx);
		return 2;
	}

	ggiSetFlags(cx->stem, GGIFLAG_ASYNC);
	ggiSetColorfulPalette(cx->stem);

	visualanchor = cx->visualanchor;

	ctx.dialog = ggiWidgetCreateContainerStack(10, NULL);
	if (!ctx.dialog) {
		result = -2;
		goto out;
	}
	ctx.dialog->gravity = GWT_GRAV_NORTH;
	ctx.dialog->pad.t = ctx.dialog->pad.b = 10;
	ctx.dialog->pad.l = ctx.dialog->pad.r = 10;

	item = ggiWidgetCreateContainerLine(0, NULL);
	if (!item) {
		result = -2;
		goto out;
	}
	item->gravity = GWT_GRAV_WEST;
	link_last_child(ctx.dialog, item);

	atom = ggiWidgetCreateColoredPatch(4, 2,
		&(ggiWidgetGetPalette(item)->col[GWT_COLOR_SHADOW_LIGHT]));
	if (!atom) {
		result = -2;
		goto out;
	}
	atom->gravity = GWT_GRAV_SOUTH;
	link_last_child(item, atom);

	ctx.tab = tab_ctrl.tab = ggiWidgetCreateContainerLine(0, NULL);
	if (!tab_ctrl.tab) {
		result = -2;
		goto out;
	}
	link_last_child(item, tab_ctrl.tab);

	atom = tab_button("Server", &tab_ctrl);
	if (!atom) {
		result = -2;
		goto out;
	}
	active = 1;
	atom->statevar = &active;
	GWT_SET_ICHANGED(atom);
	link_last_child(tab_ctrl.tab, atom);

	ctx.security_page = tab_button("Security", &tab_ctrl);
	if (!ctx.security_page) {
		result = -2;
		goto out;
	}
	link_last_child(tab_ctrl.tab, ctx.security_page);

	ctx.encoding_page = tab_button("Encodings", &tab_ctrl);
	if (!ctx.encoding_page) {
		result = -2;
		goto out;
	}
	link_last_child(tab_ctrl.tab, ctx.encoding_page);

	atom = tab_button("Misc", &tab_ctrl);
	if (!atom) {
		result = -2;
		goto out;
	}
	link_last_child(tab_ctrl.tab, atom);

	atom = ggiWidgetCreateColoredPatch(10, 2,
		&(ggiWidgetGetPalette(item)->col[GWT_COLOR_SHADOW_LIGHT]));
	if (!atom) {
		result = -2;
		goto out;
	}
	atom->gravity = GWT_GRAV_SOUTH;
	link_last_child(item, atom);

	tab_ctrl.sheet = ggiWidgetCreateContainerCard(NULL);
	if (!tab_ctrl.sheet) {
		result = -2;
		goto out;
	}
	page = 0;
	tab_ctrl.sheet->statevar = &page;
	tab_ctrl.sheet->callback = cb_sheet;
	link_last_child(ctx.dialog, tab_ctrl.sheet);

	item = server_page(&ctx, &vnc_server, &expert);
	if (!item) {
		result = -2;
		goto out;
	}
	link_last_child(tab_ctrl.sheet, item);
	item = security_page(&ctx, &adj);
	if (!item) {
		result = -2;
		goto out;
	}
	link_last_child(tab_ctrl.sheet, item);
	item = encodings_page(&ctx, &adj);
	if (!item) {
		result = -2;
		goto out;
	}
	link_last_child(tab_ctrl.sheet, item);
	item = misc_page(&ctx);
	if (!item) {
		result = -2;
		goto out;
	}
	link_last_child(tab_ctrl.sheet, item);

	cont = ggiWidgetCreateContainerLine(4, NULL);
	if (!cont) {
		result = -2;
		goto out;
	}
	cont->gravity = GWT_GRAV_EAST;
	link_last_child(ctx.dialog, cont);

	atom = ggiWidgetCreateLabel("About");
	if (!atom) {
		result = -2;
		goto out;
	}
	atom->pad.t = atom->pad.b = 3;
	atom->pad.l = atom->pad.r = 16;
	item = ggiWidgetCreateButton(atom);
	if (!item) {
		ggiWidgetDestroy(atom);
		result = -2;
		goto out;
	}
	item->callback = cb_about;
	item->callbackpriv = &ctx;
	link_last_child(cont, item);

	atom = ggiWidgetCreateLabel("Cancel");
	if (!atom) {
		result = -2;
		goto out;
	}
	atom->pad.t = atom->pad.b = 3;
	atom->pad.l = atom->pad.r = 12;
	item = ggiWidgetCreateButton(atom);
	if (!item) {
		ggiWidgetDestroy(atom);
		result = -2;
		goto out;
	}
	item->callback = cb_cancel;
	item->callbackpriv = &ctx;
	item->hotkey.sym = GIIUC_Escape;
	link_last_child(cont, item);

	atom = ggiWidgetCreateLabel("Connect");
	if (!atom) {
		result = -2;
		goto out;
	}
	atom->pad.t = atom->pad.b = 3;
	atom->pad.l = atom->pad.r = 8;
	ctx.connect = ggiWidgetCreateButton(atom);
	if (!item) {
		ggiWidgetDestroy(ctx.connect);
		result = -2;
		goto out;
	}
	ctx.connect->callback = cb_ok;
	ctx.connect->callbackpriv = &ctx;
	ggiWidgetSetTreeState(ctx.connect, GWT_STATE_INACTIVE, 1);
	link_last_child(cont, ctx.connect);

	cb_sheet(tab_ctrl.sheet, GWT_CB_STATECHANGE, NULL, NULL);
	ggiWidgetFocus(vnc_server, NULL, NULL);

	{
		ggi_widget_t res;

		res = attach_and_fit_visual(cx,
			ctx.dialog, adjust_size, &adj);

		if (!res) {
			result = 1;
			goto out;
		}
		if (set_title(cx)) {
			result = 2;
			goto out;
		}
		ctx.dialog = res;
	}

	cb_expert(expert, GWT_CB_STATECHANGE, NULL, NULL);
	ggiWidgetRedrawWidgets(visualanchor);

	while (!ctx.done) {
		gii_event event;
		giiEventRead(cx->stem, &event, emAll);

#ifdef GGIWMHFLAG_CATCH_CLOSE
		switch (event.any.type) {
		case evFromAPI:
			if (event.fromapi.api_id == libggiwmh->id) {
				switch (event.fromapi.code) {
				case GII_SLI_CODE_WMH_CLOSEREQUEST:
					debug(1, "quiting\n");
					ctx.done = -1;
					break;
				}
			}
			break;
		}
#endif

		ggiWidgetProcessEvent(visualanchor, &event);
		ggiWidgetRedrawWidgets(visualanchor);
	}

	if (ctx.done != 1) {
		result = ctx.done;
		goto out;
	}

	for (i = 0; ggiWidgetGetChild(adj.used, i); ++i);
	cx->allowed_encodings = i + !!cx->compression + !!cx->quality;
	free(cx->allow_encoding);
	cx->allow_encoding = malloc(cx->allowed_encodings * sizeof(int32_t));
	if (!cx->allow_encoding) {
		result = -2;
		goto out;
	}

	for (i = 0; (cont = ggiWidgetGetChild(adj.used, i)); ++i) {
		struct move_encodings *move = cont->callbackpriv;
		cx->allow_encoding[i] = move->encoding;
	}

	if (cx->compression)
		cx->allow_encoding[i++] = cx->compression_level - 256;
	if (cx->quality)
		cx->allow_encoding[i++] = cx->quality_level - 32;

	for (i = 0; ggiWidgetGetChild(adj.sec_used, i); ++i);
	free(cx->allow_security);
	cx->allow_security = malloc(++i * sizeof(*cx->allow_security));
	if (!cx->allow_security) {
		result = -2;
		goto out;
	}

	for (i = 0; (cont = ggiWidgetGetChild(adj.sec_used, i)); ++i) {
		struct move_encodings *move = cont->callbackpriv;
		cx->allow_security[i] = move->encoding;
	}
	cx->allow_security[i] = 0;

out:
	if (ctx.dialog) {
		ggiWidgetUnlinkChild(visualanchor,
			GWT_UNLINK_BY_WIDGETPTR, ctx.dialog);
		if (expert) {
			cx->expert = ctx.expert;
			if (!ctx.expert) {
				ctx.expert = 1;
				cb_expert(expert,
					GWT_CB_STATECHANGE, NULL, NULL);
			}
		}
		ggiWidgetDestroy(ctx.dialog);
	}
	if (result)
		close_visual(cx);
	else
		update_recent_file(cx);

	return result;
}
