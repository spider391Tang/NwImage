/*
******************************************************************************

   VNC viewer open connection dialog handling.

   Copyright (C) 2007, 2008 Peter Rosin  [peda@lysator.liu.se]

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

#include <stdio.h>
#include <string.h>
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

#include "vnc.h"
#include "vnc-debug.h"
#include "dialog.h"

struct move_encodings_t {
	int32_t encoding;
	ggi_widget_t to;
	ggi_widget_t from;
};

struct ctx {
	int done;
	int pixfmt[4];
	int pf_opt;
	char pf_custom[30];
	int endian_opt;
	int protocol_opt;
	int family_opt;
};


static void
cb_ok(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	struct ctx *ctx = widget->callbackpriv;
	char server[1024];

	if (cbt != GWT_CB_ACTIVATE)
		return;

	if (!g.server[0])
		return;

	strcpy(server, g.server);
	if (parse_port())
		goto fixup;

	if (ctx->pixfmt[0])
		strcpy(g.wire_pixfmt, "local");
	else if (ctx->pixfmt[1])
		strcpy(g.wire_pixfmt, "server");
	else if (ctx->pixfmt[2]) {
		switch(ctx->pf_opt) {
		case 0:
			strcpy(g.wire_pixfmt, "p5r1g1b1");
			break;
		case 1:
			strcpy(g.wire_pixfmt, "p2r2g2b2");
			break;
		case 2:
			strcpy(g.wire_pixfmt, "r3g3b2");
			break;
		case 3:
			strcpy(g.wire_pixfmt, "c8");
			break;
		case 4:
			strcpy(g.wire_pixfmt, "p1r5g5b5");
			break;
		case 5:
			strcpy(g.wire_pixfmt, "r5g6b5");
			break;
		case 6:
			strcpy(g.wire_pixfmt, "p8r8g8b8");
			break;
		default:
			goto fixup;
		}
	}
	else if (ctx->pixfmt[3]) {
		ggstrlcpy(g.wire_pixfmt, ctx->pf_custom,
			sizeof(g.wire_pixfmt));
		if (canonicalize_pixfmt(g.wire_pixfmt, sizeof(g.wire_pixfmt)))
			goto fixup;
	}
	else
		goto fixup;

	switch (ctx->endian_opt) {
	case 0:
		g.wire_endian = -2;
		break;
	case 1:
		g.wire_endian = -3;
		break;
	case 2:
		g.wire_endian = 1;
		break;
	case 3:
		g.wire_endian = 0;
		break;
	default:
		goto fixup;
	}

	switch (ctx->protocol_opt) {
	case 0:
		g.max_protocol = 3;
		break;
	case 1:
		g.max_protocol = 7;
		break;
	case 2:
		g.max_protocol = 8;
		break;
	default:
		goto fixup;
	}

	switch (ctx->family_opt) {
	case 0:
		g.net_family = 0;
		break;
	case 1:
		g.net_family = 4;
		break;
	case 2:
		g.net_family = 6;
		break;
	default:
		goto fixup;
	}

	ctx->done = 1;
	return;

fixup:
	strcpy(g.server, server);
}

static void
cb_cancel(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	struct ctx *ctx = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	ctx->done = -1;
}

static void move_encodings(ggi_widget_t widget, ggiWidgetCallbackType cbt);

static ggi_widget_t
create_item(int32_t encoding, ggi_widget_t to, ggi_widget_t from,
	struct move_encodings_t *me)
{
	ggi_widget_t item;

	item = ggiWidgetCreateLabel(lookup_encoding(encoding));
	item->gravity = GWT_GRAV_WEST;
	item->pad.t = item->pad.l = 1;
	item->pad.b = item->pad.r = 1;

	if (!me)
		me = malloc(sizeof(*me));
	me->encoding = encoding;
	me->to = to;
	me->from = from;

	item->callbackpriv = me;
	item->callback = move_encodings;

	item->state |= GWT_STATE_IS_SELECTABLE;

	return item;
}

static void
move_encodings(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	struct move_encodings_t *move = widget->callbackpriv;
	int i;
	ggi_widget_t child, list;
	ggi_widget_t to, from;
	struct move_encodings_t *me;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	to = move->to;
	from = move->from;

	i = 0;
	for (;;) {
		child = from->getchild(from, i);
		if (!child)
			break;

		if (!(child->state & GWT_STATE_IS_SELECTED)) {
			++i;
			continue;
		}

		me = child->callbackpriv;

		ggiWidgetSendControl(from, GWT_CONTROLFLAG_NONE,
			"DELETEROW", i);
		SET_ICHANGED(from);

		list = create_item(me->encoding, me->from, me->to, me);

		ggiWidgetSendControl(to, GWT_CONTROLFLAG_NONE,
			"INSERTROW", LAST_CHILD);
		to->linkchild(to, LAST_CHILD, list);
		SET_ICHANGED(to);
	}
}

struct adjust {
	int count;
	int *min_y;
	int *opt_y;
};

static void
adjust_size(ggi_widget_t widget, void *data)
{
	struct adjust *adj = data;
	int extra = adj->count - g.encoding_count;

	if (extra > g.encoding_count)
		extra = g.encoding_count;
	widget->min.y += *adj->min_y * extra;
	widget->opt.y += *adj->opt_y * extra;
}

static void
integer(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	double *state = (double *)widget->statevar;
	*state = floor(*state);
}

static ggi_widget_t
checkbox_widget(void)
{
	ggi_widget_t label;
	ggi_widget_t check;
	ggi_widget_t number;
	ggi_widget_t shared_session;
	ggi_widget_t no_input;
	ggi_widget_t auto_enc;
	ggi_widget_t compression = NULL;
#ifdef HAVE_JPEG
	ggi_widget_t quality;
#endif
	ggi_widget_t checkboxes;
	int i;

	label = ggiWidgetCreateLabel("Request Shared Session");
	label->pad.l = 2;
	shared_session = ggiWidgetCreateCheckbox(label, NULL);
	shared_session->gravity = GWT_GRAV_WEST;
	shared_session->statevar = &g.shared;

	label = ggiWidgetCreateLabel("View Only (inputs ignored)");
	label->pad.l = 2;
	no_input = ggiWidgetCreateCheckbox(label, NULL);
	no_input->gravity = GWT_GRAV_WEST;
	no_input->statevar = &g.no_input;

	label = ggiWidgetCreateLabel("Automatic encoding selection");
	label->pad.l = 2;
	auto_enc = ggiWidgetCreateCheckbox(label, NULL);
	auto_enc->gravity = GWT_GRAV_WEST;
	auto_enc->statevar = &g.auto_encoding;

#ifdef HAVE_ZLIB
	label = ggiWidgetCreateLabel("Compression");
	label->pad.l = 2;
	check = ggiWidgetCreateCheckbox(label, NULL);
	check->gravity = GWT_GRAV_WEST;
	check->statevar = &g.compression;
	number = ggiWidgetCreateOdometer(25, 40.0, 320.0, 0.0, 9.0, 8, 0);
	number->statevar = &g.compression_level;
	number->callback = integer;
	compression = ggiWidgetCreateContainerLine(2, check, number, NULL);
	compression->gravity = GWT_GRAV_WEST;

	g.compression = 0;
	g.compression_level = 0.0;
	for (i = 0; i < g.encoding_count; ++i) {
		if (-256 > g.encoding[i] || g.encoding[i] > -247)
			continue;
		g.compression = 1;
		g.compression_level = g.encoding[i] + 256;
	}

#ifdef HAVE_JPEG
	label = ggiWidgetCreateLabel("Quality");
	label->pad.l = 2;
	check = ggiWidgetCreateCheckbox(label, NULL);
	check->gravity = GWT_GRAV_WEST;
	check->statevar = &g.quality;
	number = ggiWidgetCreateOdometer(25, 40.0, 320.0, 0.0, 9.0, 8, 0);
	number->statevar = &g.quality_level;
	number->callback = integer;
	quality = ggiWidgetCreateContainerLine(2, check, number, NULL);
	quality->gravity = GWT_GRAV_WEST;

	g.quality = 0;
	g.quality_level = 0.0;
	for (i = 0; i < g.encoding_count; ++i) {
		if (-32 > g.encoding[i] || g.encoding[i] > -23)
			continue;
		g.quality = 1;
		g.quality_level = g.encoding[i] + 32;
	}

	compression = ggiWidgetCreateContainerLine(
		20, compression, quality, NULL);
#endif /* HAVE_JPEG */
#endif /* HAVE_ZLIB */

	checkboxes = ggiWidgetCreateContainerStack(
		2, shared_session, no_input, auto_enc, compression, NULL);
	checkboxes->pad.t = 3;
	checkboxes->gravity = GWT_GRAV_WEST;

	return checkboxes;
}

static ggi_widget_t
pixfmt_widget(struct ctx *ctx)
{
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
	ggi_widget_t item;
	ggi_widget_t h1, h2;
	ggi_widget_t l1;
	ggi_widget_t o1;
	ggi_widget_t l2;
	ggi_widget_t o2;
	ggi_widget_t d3;
	ggi_widget_t o3;
	ggi_widget_t t4;
	ggi_widget_t o4;
	ggi_widget_t s;
	int i;

	ctx->pf_opt = 5;
	if (!g.wire_pixfmt[0])
		ctx->pixfmt[0] = 1;
	else if (!strcmp(g.wire_pixfmt, "local"))
		ctx->pixfmt[0] = 1;
	else if (!strcmp(g.wire_pixfmt, "server"))
		ctx->pixfmt[1] = 1;
	else if (!strcmp(g.wire_pixfmt, "p5r1g1b1")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 0;
	}
	else if (!strcmp(g.wire_pixfmt, "p2r2g2b2")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 1;
	}
	else if (!strcmp(g.wire_pixfmt, "r3g3b2")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 2;
	}
	else if (!strcmp(g.wire_pixfmt, "c8")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 3;
	}
	else if (!strcmp(g.wire_pixfmt, "p1r5g5b5")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 4;
	}
	else if (!strcmp(g.wire_pixfmt, "r5g6b5")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 5;
	}
	else if (!strcmp(g.wire_pixfmt, "p8r8g8b8")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 6;
	}
	else if (!strcmp(g.wire_pixfmt, "weird")) {
		ctx->pixfmt[2] = 1;
		ctx->pf_opt = 5;
	}
	else {
		ctx->pixfmt[3] = 1;
		ggstrlcpy(ctx->pf_custom, g.wire_pixfmt,
			sizeof(ctx->pf_custom));
	}

	h1 = ggiWidgetCreateLabel("Pixel");
	h2 = ggiWidgetCreateLabel("Format");
	l1 = ggiWidgetCreateLabel("Local");
	o1 = ggiWidgetCreateRadiobutton(ctx->pixfmt, l1, NULL);
	l2 = ggiWidgetCreateLabel("Server");
	o2 = ggiWidgetCreateRadiobutton(ctx->pixfmt, l2, NULL);
	d3 = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	for (i = 0; options[i]; ++i) {
		item = ggiWidgetCreateLabel(options[i]);
		GWT_WIDGET_MAKE_SELECTABLE(item);
		if (i == ctx->pf_opt)
			GWT_WIDGET_MAKE_SELECTED(item);
		d3->linkchild(d3, LAST_CHILD, item);
	}
	o3 = ggiWidgetCreateRadiobutton(ctx->pixfmt, d3, NULL);
	t4 = ggiWidgetCreateText(ctx->pf_custom, 8, 29);
	o4 = ggiWidgetCreateRadiobutton(ctx->pixfmt, t4, NULL);
	s  = ggiWidgetCreateContainerStack(
		0, h1, h2, o1, o2, o3, o4, NULL);

	h1->pad.b = 1;
	h2->pad.b = 5;
	l1->pad.l = l1->pad.t = l1->pad.b = 2;
	o1->gravity = GWT_GRAV_WEST;
	o1->statevar = &ctx->pixfmt[0];
	l2->pad.l = l2->pad.t = l2->pad.b = 2;
	o2->gravity = GWT_GRAV_WEST;
	o2->statevar = &ctx->pixfmt[1];
	d3->statevar = &ctx->pf_opt;
	o3->gravity = GWT_GRAV_WEST;
	o3->statevar = &ctx->pixfmt[2];
	o4->gravity = GWT_GRAV_WEST;
	o4->statevar = &ctx->pixfmt[3];
	s->gravity = GWT_GRAV_NORTH;

	return s;
}

static ggi_widget_t
endian_widget(struct ctx *ctx)
{
	const char *options[] = {
		"Local ",
		"Server",
		"Big   ",
		"Little",
		NULL
	};
	ggi_widget_t item;
	ggi_widget_t header;
	ggi_widget_t option;
	ggi_widget_t endian;
	int i;

	switch (g.wire_endian) {
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

	header = ggiWidgetCreateLabel("Endian");
	option = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	for (i = 0; options[i]; ++i) {
		item = ggiWidgetCreateLabel(options[i]);
		GWT_WIDGET_MAKE_SELECTABLE(item);
		if (i == ctx->endian_opt)
			GWT_WIDGET_MAKE_SELECTED(item);
		option->linkchild(option, LAST_CHILD, item);
	}
	endian = ggiWidgetCreateContainerStack(0, header, option, NULL);

	header->pad.b = 2;
	option->statevar = &ctx->endian_opt;
	endian->gravity = GWT_GRAV_NORTH;

	return endian;
}

static ggi_widget_t
protocol_widget(struct ctx *ctx)
{
	const char *options[] = {
		"3.3",
		"3.7",
		"3.8",
		NULL
	};
	ggi_widget_t item;
	ggi_widget_t header;
	ggi_widget_t option;
	ggi_widget_t protocol;
	int i;

	switch (g.max_protocol) {
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

	header = ggiWidgetCreateLabel("Protocol");
	option = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	for (i = 0; options[i]; ++i) {
		item = ggiWidgetCreateLabel(options[i]);
		GWT_WIDGET_MAKE_SELECTABLE(item);
		if (i == ctx->protocol_opt)
			GWT_WIDGET_MAKE_SELECTED(item);
		option->linkchild(option, LAST_CHILD, item);
	}
	protocol = ggiWidgetCreateContainerStack(0, header, option, NULL);

	header->pad.b = 2;
	option->statevar = &ctx->protocol_opt;
	protocol->gravity = GWT_GRAV_NORTH;

	return protocol;
}

#ifdef HAVE_GETADDRINFO
static ggi_widget_t
family_widget(struct ctx *ctx)
{
	const char *options[] = {
		"Any ",
		"IPv4",
#ifdef PF_INET6
		"IPv6",
#endif
		NULL
	};
	ggi_widget_t item;
	ggi_widget_t header;
	ggi_widget_t option;
	ggi_widget_t family;
	int i;

	switch (g.net_family) {
	case 4:
		ctx->family_opt = 1;
		break;
	case 6:
		ctx->family_opt = 2;
		break;
	default:
		ctx->family_opt = 0;
	}

	header = ggiWidgetCreateLabel("Protocol Family");
	option = ggiWidgetCreateContainerDropdownList(
		GWT_DROPDOWNLIST_OPTION_SELECT, NULL);
	for (i = 0; options[i]; ++i) {
		item = ggiWidgetCreateLabel(options[i]);
		GWT_WIDGET_MAKE_SELECTABLE(item);
		if (i == ctx->family_opt)
			GWT_WIDGET_MAKE_SELECTED(item);
		option->linkchild(option, LAST_CHILD, item);
	}
	family = ggiWidgetCreateContainerLine(0, header, option, NULL);

	header->pad.r = 10;
	option->statevar = &ctx->family_opt;
	family->gravity = GWT_GRAV_NORTH;

	return family;
}
#endif /* HAVE_GETADDRINFO */

int
get_connection(void)
{
	ggi_widget_t prompt;
	ggi_widget_t vnc_server;
	ggi_widget_t vnc_server_help;
#ifdef HAVE_GETADDRINFO
	ggi_widget_t protocol_family;
#endif
	ggi_widget_t checkboxes;
	ggi_widget_t encodings;
	ggi_widget_t unused_encodings;
	ggi_widget_t list = NULL;
	ggi_widget_t grid;
	ggi_widget_t pixfmt;
	ggi_widget_t endian;
	ggi_widget_t protocol;
	ggi_widget_t params;
	ggi_widget_t conn;
	ggi_widget_t cancel;
	ggi_widget_t buttons;
	ggi_widget_t dialog;
	ggi_widget_t visualanchor;
	struct move_encodings_t move_left;
	struct move_encodings_t move_right;
	static char server[1024];
	struct ctx ctx = { 0, { 0, 0, 0, 0 }, 5, "", 0, 0, 0 };
	int i;
	int32_t *encoding;
	const int32_t *def_enc;
	int encoding_count;
	int result = 0;
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

	g.width = 400;
	g.height = 308;
#ifdef HAVE_GETADDRINFO
	g.height += 16;
#endif

	if (open_visual())
		return 2;
	select_mode();

	ggiCheckMode(g.stem, &g.mode);

	if (ggiSetMode(g.stem, &g.mode)) {
		close_visual();
		return 1;
	}

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	if (set_title()) {
		close_visual();
		return 2;
	}

	ggiSetFlags(g.stem, GGIFLAG_ASYNC);
	ggiSetColorfulPalette(g.stem);

	visualanchor = g.visualanchor;

	prompt = ggiWidgetCreateLabel("VNC Server");
	memset(server, 0, sizeof(server));
	vnc_server = ggiWidgetCreateText(server, 36, sizeof(server) - 1);
	vnc_server->callback = cb_ok;
	vnc_server->callbackpriv = &ctx;
	vnc_server_help = ggiWidgetCreateLabel(
		"(host, host:display or host::port)");
	g.server = server;
#ifdef HAVE_GETADDRINFO
	protocol_family = family_widget(&ctx);
#endif

	checkboxes = checkbox_widget();

	{
		ggi_widget_t used, unused;
		ggi_widget_t move;
		ggi_widget_t label1, label2;
		ggi_widget_t button1, button2;
		int j;

		encodings = ggiWidgetCreateContainerGrid(1, 0, 0, 0);
		unused_encodings = ggiWidgetCreateContainerGrid(1, 0, 0, 0);
		move_left.to = encodings;
		move_left.from = unused_encodings;
		move_right.to = unused_encodings;
		move_right.from = encodings;

		label1 = ggiWidgetCreateLabel("Requested");
		label1->pad.b = 1;
		label2 = ggiWidgetCreateLabel("Encodings");
		encodings->gravity = GWT_GRAV_NORTH;
		ggiWidgetSendControl(encodings, GWT_CONTROLFLAG_NONE,
			"SETOPTIONS", GWT_GRID_OPTION_SELECT_Y);
		ggiWidgetSendControl(encodings, GWT_CONTROLFLAG_NONE,
			"SETOPTIONS", GWT_GRID_OPTION_SELECT_MULTI);
		for (i = 0; i < g.encoding_count; ++i) {
			if (-32 <= g.encoding[i] && g.encoding[i] <= -23)
				continue;
			if (-256 <= g.encoding[i] && g.encoding[i] <= -247)
				continue;
			list = create_item(g.encoding[i],
				unused_encodings, encodings, NULL);
			ggiWidgetSendControl(encodings, GWT_CONTROLFLAG_NONE,
				"INSERTROW", LAST_CHILD);
			encodings->linkchild(encodings, LAST_CHILD, list);
		}
		encodings->pad.t = 5;
		used = ggiWidgetCreateContainerStack(
			0, label1, label2, encodings, NULL);
		used->gravity = GWT_GRAV_NORTH;

		label1 = ggiWidgetCreateLabel("Leftover");
		label1->pad.b = 1;
		label2 = ggiWidgetCreateLabel("Encodings");
		encoding_count = get_default_encodings(&def_enc);
		unused_encodings->gravity = GWT_GRAV_NORTH;
		ggiWidgetSendControl(unused_encodings, GWT_CONTROLFLAG_NONE,
			"SETOPTIONS", GWT_GRID_OPTION_SELECT_Y);
		ggiWidgetSendControl(unused_encodings, GWT_CONTROLFLAG_NONE,
			"SETOPTIONS", GWT_GRID_OPTION_SELECT_MULTI);
		for (j = 0; j < encoding_count; ++j) {
			for (i = 0; i < g.encoding_count; ++i) {
				if (g.encoding[i] == def_enc[j])
					break;
			}
			if (i < g.encoding_count)
				continue;
			if (-32 <= def_enc[j] && def_enc[j] <= -23)
				continue;
			if (-256 <= def_enc[j] && def_enc[j] <= -247)
				continue;
			list = create_item(def_enc[j],
				encodings, unused_encodings, NULL);
			ggiWidgetSendControl(unused_encodings,
				GWT_CONTROLFLAG_NONE,
				"INSERTROW", LAST_CHILD);
			unused_encodings->linkchild(unused_encodings,
				LAST_CHILD, list);
		}
		unused_encodings->pad.t = 5;
		unused = ggiWidgetCreateContainerStack(
			0, label1, label2, unused_encodings, NULL);
		unused->gravity = GWT_GRAV_NORTH;

		button1 = ggiWidgetMakeImagebutton(&left);
		button1->callback = move_encodings;
		button1->callbackpriv = &move_left;
		button2 = ggiWidgetMakeImagebutton(&right);
		button2->callback = move_encodings;
		button2->callbackpriv = &move_right;
		move = ggiWidgetCreateContainerStack(
			5, button1, button2, NULL);
		move->pad.t = 20;
		move->gravity = GWT_GRAV_NORTH;

		grid = ggiWidgetCreateContainerGrid(4, 1, 10, 0);
		grid->linkchild(grid, BY_XYPOS, used, 0, 0);
		grid->linkchild(grid, BY_XYPOS, move, 1, 0);
		grid->linkchild(grid, BY_XYPOS, unused, 2, 0);
		grid->pad.t = 10;
	}

	pixfmt = pixfmt_widget(&ctx);
	endian = endian_widget(&ctx);
	protocol = protocol_widget(&ctx);
	pixfmt = ggiWidgetCreateContainerStack(10,
		pixfmt, endian, protocol, NULL);
	pixfmt->gravity = GWT_GRAV_NORTH;
	grid->linkchild(grid, BY_XYPOS, pixfmt, 3, 0);

	params = ggiWidgetCreateContainerStack(4,
		prompt, vnc_server, vnc_server_help,
#ifdef HAVE_GETADDRINFO
		protocol_family,
#endif
		checkboxes, grid, NULL);

	conn = ggiWidgetCreateLabel("Connect");
	conn->pad.t = conn->pad.b = 3;
	conn->pad.l = conn->pad.r = 8;
	conn = ggiWidgetCreateButton(conn);
	conn->callback = cb_ok;
	conn->callbackpriv = &ctx;

	cancel = ggiWidgetCreateLabel("Cancel");
	cancel->pad.t = cancel->pad.b = 3;
	cancel->pad.l = cancel->pad.r = 12;
	cancel = ggiWidgetCreateButton(cancel);
	cancel->callback = cb_cancel;
	cancel->callbackpriv = &ctx;
	cancel->hotkey.sym = GIIUC_Escape;

	buttons = ggiWidgetCreateContainerStack(4, conn, cancel, NULL);
	buttons->gravity = GWT_GRAV_NORTH;
	buttons->pad.t = 10;

	dialog = ggiWidgetCreateContainerLine(10, params, buttons, NULL);
	dialog->gravity = GWT_GRAV_NORTH;
	dialog->pad.t = 10;
	dialog->pad.l = params->pad.r = 2;
	dialog->pad.b = 2;

	ggiWidgetFocus(vnc_server);

	{
		ggi_widget_t res;
		struct adjust adj;
		adj.count = encoding_count;
		adj.min_y = &list->min.y;
		adj.opt_y = &list->opt.y;

		res = attach_and_fit_visual(dialog, adjust_size, &adj);
		if (!res) {
			close_visual();
			result = 1;
			goto out;
		}
		if (set_title()) {
			close_visual();
			result = 2;
			goto out;
		}
		dialog = res;
	}

	while (!ctx.done) {
		gii_event event;
		giiEventRead(g.stem, &event, emAll);

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

	visualanchor->unlinkchild(visualanchor, UNLINK_BY_WIDGETPTR, dialog);

	if (ctx.done < 0) {
		close_visual();
		result = -1;
		goto out;
	}

	for (i = 0; encodings->getchild(encodings, i); ++i);
	g.encoding_count = i + !!g.compression + !!g.quality;
	encoding = (int32_t *)malloc(g.encoding_count * sizeof(int32_t));
	if (!encoding) {
		close_visual();
		result = -2;
		goto out;
	}

	for (i = 0; (list = encodings->getchild(encodings, i)); ++i) {
		struct move_encodings_t *move = list->callbackpriv;
		encoding[i] = move->encoding;
	}

	if (g.compression)
		encoding[i++] = g.compression_level - 256;
	if (g.quality)
		encoding[i++] = g.quality_level - 32;

	if (g.encoding != def_enc)
		free(g.encoding);
	g.encoding = encoding;

out:
	for (i = 0; (list = encodings->getchild(encodings, i)); ++i)
		free(list->callbackpriv);
	i = 0;
	for (; (list = unused_encodings->getchild(unused_encodings, i)); ++i)
		free(list->callbackpriv);

	dialog->destroy(dialog);

	return result;
}
