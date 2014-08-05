/*
******************************************************************************

   Present a menu to the user using ggiwidgets.

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
#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif
#include <ggi/ggi_widget.h>

#include "vnc.h"
#include "dialog.h"

static void
menu_close_menu(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 1;
}

static void
menu_close(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 3;
}

static void
menu_send_f8(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 2;
}

static void
menu_refresh_screen(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	vnc_update_request(0);
	*done = 1;
}

static void
menu_ctrl_alt_del(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 5;
}

static void
menu_hotkeys(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 6;
}

static void
menu_paste(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 7;
}

static void
menu_about(ggi_widget_t widget, ggiWidgetCallbackType cbt)
{
	int *done = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	*done = 4;
}

static ggi_widget_t
menu_button(const char *text,
	void (*hook)(struct ggiWidget *self, ggiWidgetCallbackType CBTYPE),
	void *data)
{
	ggi_widget_t label, button;

	label = ggiWidgetCreateLabel(text);
	if (!label)
		return NULL;
	label->pad.t = label->pad.b = label->pad.r = 1;
	label->pad.l = 11;
	label->gravity = GWT_GRAV_WEST;

	button = ggiWidgetCreateButton(label);
	if (!button) {
		label->destroy(label);
		return NULL;
	}

	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETPRESSEDTYPE", GWT_FRAMETYPE_3D_IN);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETSELECTEDTYPE", GWT_FRAMETYPE_3D_OUT);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETDESELECTEDTYPE", GWT_FRAMETYPE_NONE);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETFOCUSTYPE", GWT_FRAMETYPE_NONE);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_PASSTOCHILDRENTILFOUND,
		"SETFRAMELINEWIDTH", GWT_FRAMEGROUP_FOCUS, 0);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETFOCUSONENTER", 1);

	button->gravity = GWT_GRAV_STRETCH_X;
	button->callback = hook;
	button->callbackpriv = data;

	return button;
}

static ggi_widget_t
menu_check(const char *text, int check,
	void (*hook)(struct ggiWidget *self, ggiWidgetCallbackType CBTYPE),
	void *data)
{
	static struct ggiWidgetImage checked = {
		{ 8, 8 },
		GWT_IF_BITMAP,
		(const unsigned char *)"\x01\x03\x07\x0e\xdc\xf8\xf0\xe0",
		GWT_IF_NONE, NULL
	};
	ggi_widget_t image, label, content, button;

	label = ggiWidgetCreateLabel(text);
	if (!label)
		return NULL;
	label->pad.t = label->pad.l = label->pad.b = label->pad.r = 1;
	label->gravity = GWT_GRAV_WEST;

	if (check) {
		image = ggiWidgetCreateImage(&checked);
		if (!image) {
			label->destroy(label);
			return NULL;
		}
		image->pad.t = image->pad.l = image->pad.b = image->pad.r = 1;
		image->gravity = GWT_GRAV_WEST;

		content = ggiWidgetCreateContainerLine(0, image, label, NULL);
		if (!content) {
			image->destroy(image);
			label->destroy(label);
			return NULL;
		}
		content->gravity = GWT_GRAV_WEST;
	}
	else {
		content = label;
		content->pad.l = 11;
	}

	button = ggiWidgetCreateButton(content);
	if (!button) {
		content->destroy(content);
		return NULL;
	}

	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETPRESSEDTYPE", GWT_FRAMETYPE_3D_IN);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETSELECTEDTYPE", GWT_FRAMETYPE_3D_OUT);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETDESELECTEDTYPE", GWT_FRAMETYPE_NONE);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETFOCUSTYPE", GWT_FRAMETYPE_NONE);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_PASSTOCHILDRENTILFOUND,
		"SETFRAMELINEWIDTH", GWT_FRAMEGROUP_FOCUS, 0);
	ggiWidgetSendControl(button, GWT_CONTROLFLAG_NONE,
		"SETFOCUSONENTER", 1);

	button->gravity = GWT_GRAV_STRETCH_X;
	button->callback = hook;
	button->callbackpriv = data;

	return button;
}

#define R "\x03\xff\x01\x01"
#define B "\x10\x26"

int
show_menu(void)
{
	ggi_widget_t item;
	ggi_widget_t menu;
	int done = 0;

	menu = ggiWidgetCreateContainerStack(1, NULL);
	if (!menu)
		return 1;
	menu->pad.t = menu->pad.l = menu->pad.b = menu->pad.r = 4;

	item = menu_button("Close menu", menu_close_menu, &done);
	if (!item)
		goto destroy_menu;
	menu->linkchild(menu, LAST_CHILD, item);
	item = menu_button(R"C"B"lose " PACKAGE_NAME, menu_close, &done);
	if (!item)
		goto destroy_menu;
	item->hotkey.label = 'C';
	menu->linkchild(menu, LAST_CHILD, item);
	item = menu_button("Send "R"F8"B, menu_send_f8, &done);
	if (!item)
		goto destroy_menu;
	item->hotkey.sym = GIIK_F8;
	menu->linkchild(menu, LAST_CHILD, item);
	item = menu_button(R"R"B"efresh Screen", menu_refresh_screen, &done);
	if (!item)
		goto destroy_menu;
	item->hotkey.label = 'R';
	menu->linkchild(menu, LAST_CHILD, item);
	item = menu_button("Send Ctrl-Alt-Del", menu_ctrl_alt_del, &done);
	if (!item)
		goto destroy_menu;
	menu->linkchild(menu, LAST_CHILD, item);
#ifdef GGIWMHFLAG_GRAB_HOTKEYS
	item = menu_check("Grab "R"H"B"otkeys",
		ggiWmhGetFlags(g.stem) & GGIWMHFLAG_GRAB_HOTKEYS,
		menu_hotkeys, &done);
	if (!item)
		goto destroy_menu;
	item->hotkey.label = 'H';
	menu->linkchild(menu, LAST_CHILD, item);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	item = menu_button(R"P"B"aste Clipboard", menu_paste, &done);
	if (!item)
		goto destroy_menu;
	item->hotkey.label = 'P';
	menu->linkchild(menu, LAST_CHILD, item);
#endif
	item = menu_button(R"A"B"bout...", menu_about, &done);
	if (!item)
		goto destroy_menu;
	item->hotkey.label = 'A';
	menu->linkchild(menu, LAST_CHILD, item);

	menu = popup_dialog(menu, &done);

destroy_menu:
	menu->destroy(menu);

	return done - 1;
}
