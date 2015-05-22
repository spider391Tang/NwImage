/*
******************************************************************************

   Present a file transfer dialog to the user using ggiwidgets.

   The MIT License

   Copyright (C) 2008-2010 Peter Rosin  [peda@lysator.liu.se]

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
#include <time.h>
#include <sys/stat.h>
#include <ggi/gii.h>
#include <ggi/gii-events.h>
#include <ggi/gii-keyboard.h>
#include <ggi/ggi.h>
#include <ggi/ggi_widget.h>
#include <ggi/ggi_widget_highlevel.h>
#include <ggi/ggi_widget_fileselector.h>
#include <ggi/ggi_widget_image.h>

#include "vnc.h"
#include "vnc-debug.h"
#include "vnc-endian.h"
#include "dialog.h"

#define DIRECTORY 1

struct file_entry {
	int flags;
	char *filename;
#ifdef GG_HAVE_INT64
	uint64_t filesize;
	uint64_t mod;
#else
	uint32_t filesize;
	uint32_t mod;
#endif
};

struct file_transfer {
	char *local_path;
	char *remote_path;
	ggi_widget_t local;
	ggi_widget_t remote;
	ggi_widget_t feedback;
	double progress;
#ifdef GG_HAVE_INT64
	uint64_t current;
	uint64_t total;
#else
	uint32_t current;
	uint32_t total;
#endif
	struct file_entry *files;
	FILE *download;
	FILE *upload;
	int wait_remote;
	int done;
};

static int file_v1_upload_resync(struct connection *cx);
static int file_download_data(struct connection *cx);
static int file_upload_data(struct connection *cx);

static int
set_feedback(struct connection *cx, const char *message)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	ggi_widget_t child;

	debug(1, "user feedback: \"%s\"\n", message);

	if (!ft->feedback)
		return 1;

	child = ggiWidgetUnlinkChild(ft->feedback, 0);
	ggiWidgetDestroy(child);

	child = ggiWidgetCreateLabel(message);
	if (!child)
		return -1;
	ggiWidgetLinkChild(ft->feedback, 0, child);
	GWT_SET_ICHANGED(ft->feedback);

	return 0;
}

static int
progress_feedback(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	ggi_widget_t child;
	ggi_widget_t progress;

	if (!ft->feedback)
		return 1;

	child = ggiWidgetUnlinkChild(ft->feedback, 0);
	ggiWidgetDestroy(child);

	child = ggiWidgetCreateFrame(GWT_FRAMEGROUP_GROUPING, 2,
		GWT_FRAMETYPE_3D_IN, NULL);
	if (!child)
		return -1;
	ggiWidgetLinkChild(ft->feedback, 0, child);

	progress = ggiWidgetCreateProgressbar(0, 400, 14);
	if (!progress)
		return -1;
	progress->statevar = &ft->progress;
	ggiWidgetLinkChild(child, 0, progress);
	GWT_SET_ICHANGED(child);

	return 0;
}

static int
order_file_entries_alpha(const void *s1, const void *s2)
{
	const struct file_entry *f1 = s1;
	const struct file_entry *f2 = s2;

	if ((f1->flags & DIRECTORY) && !(f2->flags & DIRECTORY))
		return -1;
	if (!(f1->flags & DIRECTORY) && (f2->flags & DIRECTORY))
		return 1;
	return strcmp(f1->filename, f2->filename);
}

static void
file_close(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct connection *cx = widget->callbackpriv;
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	ft->done = 1;
}

static int
fill_remote_widget(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	struct file_entry *fe;
	ggi_widget_t scroller;

	for (fe = ft->files; fe->filename; ++fe) {
		ggi_widget_t child;
		if (fe->flags & DIRECTORY) {
			ggi_widget_t item;
			child = ggiWidgetCreateContainerLine(2, NULL);
			if (!child)
				return -1;
			child->gravity = GWT_GRAV_WEST;
			item = ggiWidgetCreateImage(ggiWidgetImageCommon(
				GWT_COMMON_IMAGE_FOLDER_16x14,
				GWT_STYLE_DEFAULT));
			if (!item) {
				ggiWidgetDestroy(child);
				return -1;
			}
			item->gravity = GWT_GRAV_SOUTH;
			ggiWidgetLinkChild(child, GWT_LAST_CHILD, item);
			item = ggiWidgetCreateLabel(fe->filename);
			if (!item) {
				ggiWidgetDestroy(child);
				return -1;
			}
			item->gravity = GWT_GRAV_SOUTH | GWT_GRAV_WEST;
			ggiWidgetLinkChild(child, GWT_LAST_CHILD, item);
			child->callbackpriv = fe;
		}
		else {
			child = ggiWidgetCreateLabel(fe->filename);
			if (!child)
				return -1;
			child->gravity = GWT_GRAV_SOUTH | GWT_GRAV_WEST;
			child->callbackpriv = fe;
		}
		ggiWidgetLinkChild(ft->remote, GWT_LAST_CHILD, child);
		GWT_SET_ICHANGED(ft->remote);
	}
	for (scroller = ft->remote; scroller; scroller = ggiWidgetGetParent(scroller)) {
		if (!strcmp(scroller->id, "scroller"))
			break;
	}
	if (!scroller)
		return 0;

	ggiWidgetSendControl(scroller, GWT_CONTROLFLAG_NONE,
		"SETPOS", 0.0, 0.0);
	return 0;
}

static int
file_v1_list_data(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t flags;
	uint16_t files;
	uint16_t len;
	uint16_t raw;
	uint16_t i;
	char *ptr;
	char *file;
	uint8_t *metadata;
	struct file_entry *fe;
	int virt_files = 0;

	debug(2, "file_v1_list_data\n");

	if (cx->input.data[cx->input.rpos] != 130)
		return vnc_unexpected(cx);

	if (cx->input.wpos < cx->input.rpos + 8)
		return 0;

	flags = cx->input.data[cx->input.rpos + 1];
	files = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
	len = get16_hilo(&cx->input.data[cx->input.rpos + 4]);
	raw = get16_hilo(&cx->input.data[cx->input.rpos + 6]);

	debug(2, "flags %0x, files %d, len %d, raw-len %d\n",
		flags, files, len, raw);

	if (flags & 0x0f)
		return close_connection(cx, -1);
	if (len != raw)
		return close_connection(cx, -1);

	if (cx->input.wpos < cx->input.rpos + 8 + 8 * files + raw)
		return 0;

	if (raw < files)
		return close_connection(cx, -1);
	if (raw && cx->input.data[cx->input.rpos + 8 + 8 * files + raw - 1])
		return close_connection(cx, -1);

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	if (flags & 0x20) {
		set_feedback(cx, "Unable to open remote directory");
		goto done;
	}

	set_feedback(cx, "");

	if (ft->files) {
		for (fe = ft->files; fe->filename; ++fe)
			free(fe->filename);
		free(ft->files);
	}
	ft->files = calloc(2 + files + 1, sizeof(*ft->files));
	if (!ft->files)
		return close_connection(cx, -1);

	ptr = (char *)&cx->input.data[cx->input.rpos + 8 + 8 * files];
	file = ptr;

	while (ggiWidgetGetChild(ft->remote, 0)) {
		ggi_widget_t child;
		child = ggiWidgetUnlinkChild(ft->remote, GWT_LAST_CHILD);
		ggiWidgetDestroy(child);
	}
	GWT_SET_ICHANGED(ft->remote);

	fe = ft->files;
	if (strcmp(ft->remote_path, "/")) {
		fe->filename = strdup(".");
		if (!fe->filename)
			return close_connection(cx, -1);
		fe->flags = DIRECTORY;
		++fe;
		++virt_files;
		fe->filename = strdup("..");
		if (!fe->filename)
			return close_connection(cx, -1);
		fe->flags = DIRECTORY;
		++fe;
		++virt_files;
	}

	metadata = &cx->input.data[cx->input.rpos + 8];
	for (i = 0; i < files && file < &ptr[raw]; ++i, metadata += 8) {
		uint32_t size = get32_hilo(metadata);
		uint32_t mod = get32_hilo(metadata + 4);
		time_t t = mod;

		debug(2, "\"%s\" %u %u, %s", file, size, mod,
			t >= 0 ? asctime(gmtime(&t)) : "neg date\n");

		fe->filename = strdup(file);
		if (!fe->filename)
			return close_connection(cx, -1);
		if (!~size)
			fe->flags = DIRECTORY;
		else
			fe->filesize = size;
		fe->mod = mod;
		++fe;

		file += strlen(file) + 1;
	}

	if (i != files)
		return close_connection(cx, -1);

	fe = ft->files;
	qsort(fe, files + virt_files, sizeof(*fe), order_file_entries_alpha);

	if (fill_remote_widget(cx))
		return close_connection(cx, -1);

done:
	cx->input.rpos += 8 + 8 * files + raw;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_v1_list_data_dummy(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t flags;
	uint16_t files;
	uint16_t len;
	uint16_t raw;

	debug(2, "file_v1_list_data_dummy\n");

	if (cx->input.data[cx->input.rpos] != 130)
		return vnc_unexpected(cx);

	if (cx->input.wpos < cx->input.rpos + 8)
		return 0;

	flags = cx->input.data[cx->input.rpos + 1];
	files = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
	len = get16_hilo(&cx->input.data[cx->input.rpos + 4]);
	raw = get16_hilo(&cx->input.data[cx->input.rpos + 6]);

	debug(2, "flags %0x, files %d, len %d, raw-len %d\n",
		flags, files, len, raw);

	if (flags & 0x0f)
		return close_connection(cx, -1);
	if (len != raw)
		return close_connection(cx, -1);

	if (cx->input.wpos < cx->input.rpos + 8 + 8 * files + raw)
		return 0;

	if (raw < files)
		return close_connection(cx, -1);
	if (raw && cx->input.data[cx->input.rpos + 8 + 8 * files + raw - 1])
		return close_connection(cx, -1);

	debug(1, "resynced with remote.\n");
	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	cx->input.rpos += 8 + 8 * files + raw;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_v1_download_data(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t flags;
	uint16_t raw;
	uint16_t len;
	uint8_t *data;

	debug(2, "file_v1_download_data\n");

	if (cx->input.wpos < cx->input.rpos + 6)
		return 0;

	flags = cx->input.data[cx->input.rpos + 1];
	raw = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
	len = get16_hilo(&cx->input.data[cx->input.rpos + 4]);

	debug(2, "flags %0x, raw-len %d, len %d\n",
		flags, raw, len);

	if (flags & 0x0f)
		return close_connection(cx, -1);
	if (len != raw)
		return close_connection(cx, -1);

	if (cx->input.wpos < cx->input.rpos + 6 + raw)
		return 0;

	if (!raw) {
		uint32_t mod;
		time_t t;

		if (cx->input.wpos < cx->input.rpos + 10)
			return 0;

		/* Huh? Why is this not in network byte order? */
		mod = get32_lohi(&cx->input.data[cx->input.rpos + 6]);
		t = mod;

		debug(1, "got file download data (mod %u), %s",
			mod, t >= 0 ? asctime(gmtime(&t)) : "neg date\n");

		if (ft->download) {
			set_feedback(cx, "Download complete");

			fclose(ft->download);
			ft->download = NULL;
		}

		cx->encoding_def[tight_file].action = vnc_unexpected;
		ft->wait_remote = 0;

		cx->input.rpos += 10;
		remove_dead_data(&cx->input);
		cx->action = vnc_wait;
		return 1;
	}

	debug(2, "got file download data (raw %d)\n", raw);

	if (ft->download) {
		if (ft->feedback && ft->total) {
			ggi_widget_t progress;
			progress = ggiWidgetGetChild(ft->feedback, 0);
			while (ggiWidgetGetChild(progress, 0))
				progress = ggiWidgetGetChild(progress, 0);
			GWT_SET_ICHANGED(progress);
		}
		ft->current += raw;
		if (ft->total)
			ft->progress = (double)ft->current / ft->total;

		data = &cx->input.data[6];
		while (raw) {
			int res = fwrite(data, 1, raw, ft->download);
			if (res == raw)
				break;
			raw -= res;
			data += res;
			if (!ferror(ft->download))
				continue;
			set_feedback(cx, "Error writing to download file");
			fclose(ft->download);
			ft->download = NULL;
		}
	}

	cx->input.rpos += 6 + raw;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_v1_download_failed(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint16_t len;
	char *reason;

	debug(2, "file_v1_download_failed\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	len = get16_hilo(&cx->input.data[cx->input.rpos + 2]);

	if (cx->input.wpos < cx->input.rpos + 4 + len)
		return 0;

	reason = malloc(len + 1);
	memcpy(reason, &cx->input.data[cx->input.rpos + 4], len);
	reason[len] = '\0';
	debug(1, "got file download failure \"%s\"\n", reason);
	free(reason);
	reason = NULL;

	if (ft->download) {
		set_feedback(cx, "Download failed");
		fclose(ft->download);
		ft->download = NULL;
	}

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	cx->input.rpos += 4 + len;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_v1_last_request_failed(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t request;
	uint16_t len;
	char *reason;

	debug(2, "file_v1_last_request_failed\n");

	if (cx->input.data[cx->input.rpos] != 135)
		return vnc_unexpected(cx);

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	request = cx->input.data[cx->input.rpos + 1];
	len = get16_hilo(&cx->input.data[cx->input.rpos + 2]);

	if (request != 131)
		/* Only expect failure of download requests. */
		return vnc_unexpected(cx);

	if (cx->input.wpos < cx->input.rpos + 4 + len)
		return 0;

	reason = malloc(len + 1);
	memcpy(reason, &cx->input.data[cx->input.rpos + 4], len);
	reason[len] = '\0';
	debug(1, "got last request failure \"%s\"\n", reason);
	free(reason);
	reason = NULL;

	if (ft->download) {
		set_feedback(cx, "Request failed");
		fclose(ft->download);
		ft->download = NULL;
	}

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	cx->input.rpos += 4 + len;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_v1_download_data_or_failed(struct connection *cx)
{
	switch (cx->input.data[cx->input.rpos]) {
	case 131:
		return file_v1_download_data(cx);
	case 133:
		return file_v1_download_failed(cx);
	case 135:
		return file_v1_last_request_failed(cx);
	}

	return vnc_unexpected(cx);
}

static int
file_v1_upload_failed(struct connection *cx, const char *reason)
{
	uint8_t *fuf;
	size_t len = strlen(reason);

	debug(1, "file_v1_upload_failed reason \"%s\"\n", reason);

	if (len > 0xffff) {
		debug(1, "reason too long\n");
		close_connection(cx, -1);
		return -1;
	}
	fuf = malloc(4 + len);
	if (!fuf) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}
	fuf[0] = 135;
	fuf[1] = 0;
	insert16_hilo(&fuf[2], len);
	memcpy((char *)&fuf[4], reason, len);

	if (safe_write(cx, fuf, 4 + len)) {
		free(fuf);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(fuf);

	return 0;
}

static int
file_v1_list_request(struct connection *cx, const char *dir)
{
	uint8_t *flr;
	size_t len = strlen(dir);

	if (len > 0xffff) {
		debug(1, "path too long\n");
		close_connection(cx, -1);
		return -1;
	}
	flr = malloc(4 + len);
	if (!flr) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}
	flr[0] = 130;
	flr[1] = 0;
	insert16_hilo(&flr[2], len);
	memcpy((char *)&flr[4], dir, len);

	if (safe_write(cx, flr, 4 + len)) {
		free(flr);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(flr);

	cx->encoding_def[tight_file].action = file_v1_list_data;

	return 0;
}

static int
file_v1_upload_cancel(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint16_t len;
	char *reason;

	debug(1, "file_v1_upload_cancel\n");

	if (cx->input.data[cx->input.rpos] != 132)
		return vnc_unexpected(cx);

	if (ft->upload) {
		fclose(ft->upload);
		ft->upload = NULL;

		set_feedback(cx, "Upload cancelled by remote");
		file_v1_upload_failed(cx, "Upload cancelled.");
	}

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	len = get16_hilo(&cx->input.data[cx->input.rpos + 2]);

	if (cx->input.wpos < cx->input.rpos + 4 + len)
		return 0;

	reason = malloc(len + 1);
	memcpy(reason, &cx->input.data[cx->input.rpos + 4], len);
	reason[len] = '\0';
	debug(1, "got file upload cancel \"%s\"\n", reason);
	free(reason);
	reason = NULL;

	if (cx->encoding_def[tight_file].action != file_v1_upload_resync) {
		debug(1, "resync with remote using a file list request.\n");
		file_v1_list_request(cx, "/");
		cx->encoding_def[tight_file].action = file_v1_upload_resync;
	}

	cx->input.rpos += 4 + len;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return !cx->close_connection;
}

static int
file_v1_upload_resync(struct connection *cx)
{
	switch (cx->input.data[cx->input.rpos]) {
	case 130:
		return file_v1_list_data_dummy(cx);
	case 132:
		return file_v1_upload_cancel(cx);
	}

	return vnc_unexpected(cx);
}

static int
file_v1_download_request(struct connection *cx,
	const char *path, const char *file)
{
	uint8_t *fdlr;
	size_t pathlen = strlen(path);
	size_t filelen = strlen(file);
	int slash = pathlen && path[pathlen - 1] != '/';

	if (pathlen + slash + filelen > 0xffff) {
		debug(1, "path too long\n");
		close_connection(cx, -1);
		return -1;
	}
	fdlr = malloc(8 + pathlen + slash + filelen);
	if (!fdlr) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}
	fdlr[0] = 131;
	fdlr[1] = 0;
	insert16_hilo(&fdlr[2], pathlen + slash + filelen);
	insert32_hilo(&fdlr[4], 0);
	memcpy((char *)&fdlr[8], path, pathlen);
	if (slash)
		fdlr[8 + pathlen] = '/';
	memcpy((char *)&fdlr[8 + pathlen + slash], file, filelen);

	if (safe_write(cx, fdlr, 8 + pathlen + slash + filelen)) {
		free(fdlr);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(fdlr);

	cx->encoding_def[tight_file].action = file_v1_download_data_or_failed;

	return 0;
}

static int
file_v1_upload_request(struct connection *cx,
	const char *path, const char *file)
{
	uint8_t *fulr;
	size_t pathlen = strlen(path);
	size_t filelen = strlen(file);
	int slash = pathlen && path[pathlen - 1] != '/';

	if (cx->no_input)
		return 1;

	if (pathlen + slash + filelen > 0xffff) {
		debug(1, "path too long\n");
		close_connection(cx, -1);
		return -1;
	}
	fulr = malloc(8 + pathlen + slash + filelen);
	if (!fulr) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}
	fulr[0] = 132;
	fulr[1] = 0;
	insert16_hilo(&fulr[2], pathlen + slash + filelen);
	insert32_hilo(&fulr[4], 0);
	memcpy((char *)&fulr[8], path, pathlen);
	if (slash)
		fulr[8 + pathlen] = '/';
	memcpy((char *)&fulr[8 + pathlen + slash], file, filelen);

	if (safe_write(cx, fulr, 8 + pathlen + slash + filelen)) {
		free(fulr);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(fulr);

	cx->encoding_def[tight_file].action = file_v1_upload_cancel;

	return 0;
}

static int
file_v1_upload_data(struct connection *cx, uint8_t *data, size_t len)
{
	uint8_t *fud;

	debug(2, "file_v1_upload_data (len %u)\n", len);

	if (len > 0xffff) {
		debug(1, "data too long\n");
		close_connection(cx, -1);
		return -1;
	}
	fud = malloc(6 + len);
	if (!fud) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}
	fud[0] = 133;
	fud[1] = 0;
	insert16_hilo(&fud[2], len);
	insert16_hilo(&fud[4], len);
	memcpy((char *)&fud[6], data, len);

	if (safe_write(cx, fud, 6 + len)) {
		free(fud);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(fud);

	return 0;
}

static int
file_v1_upload_data_last(struct connection *cx, uint32_t mod)
{
	uint8_t fud[10];
	time_t t = mod;

	debug(1, "file_v1_upload_data_last (mod %u), %s",
		mod, t >= 0 ? asctime(gmtime(&t)) : "neg date\n");

	fud[0] = 133;
	fud[1] = 0;
	insert16_hilo(&fud[2], 0);
	insert16_hilo(&fud[4], 0);
	insert32_hilo(&fud[6], mod);

	if (safe_write(cx, fud, sizeof(fud))) {
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	return 0;
}

static int
file_list_reply(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t compression;
	uint32_t files;
	uint32_t len;
	uint32_t raw;
	uint32_t i;
	uint8_t *ptr;
	uint8_t *file;
	struct file_entry *fe;
	int virt_files = 0;

	debug(2, "file_list_reply\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return vnc_unexpected(cx);

	switch (get32_hilo(&cx->input.data[cx->input.rpos])) {
	case 0xfc000103:
		break;
	case 0xfc000119:
		set_feedback(cx, "Unable to open remote directory");
		if (cx->input.wpos < cx->input.rpos + 8)
			return 0;
		len = get32_hilo(&cx->input.data[cx->input.rpos + 4]);
		if (cx->input.wpos < cx->input.rpos + 8 + len)
			return 0;
		cx->input.rpos += 8 + len;
		remove_dead_data(&cx->input);
		cx->action = vnc_wait;
		return 1;
	default:
		return vnc_unexpected(cx);
	}

	if (cx->input.wpos < cx->input.rpos + 13)
		return 0;

	compression = cx->input.data[cx->input.rpos + 4];
	len = get32_hilo(&cx->input.data[cx->input.rpos + 5]);
	raw = get32_hilo(&cx->input.data[cx->input.rpos + 9]);

	debug(2, "compression %02x, len %u, raw-len %u\n",
		compression, len, raw);

	if (compression)
		return close_connection(cx, -1);
	if (len != raw)
		return close_connection(cx, -1);
	if (raw < 4)
		return close_connection(cx, -1);

	if (cx->input.wpos < cx->input.rpos + 13 + raw)
		return 0;

	files = get32_hilo(&cx->input.data[cx->input.rpos + 13]);

	if (raw < files * 22)
		return close_connection(cx, -1);

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	set_feedback(cx, "");

	if (ft->files) {
		for (fe = ft->files; fe->filename; ++fe)
			free(fe->filename);
		free(ft->files);
	}
	ft->files = calloc(2 + files + 1, sizeof(*ft->files));
	if (!ft->files)
		return close_connection(cx, -1);

	ptr = &cx->input.data[cx->input.rpos + 13];
	file = ptr + 4;

	while (ggiWidgetGetChild(ft->remote, 0)) {
		ggi_widget_t child;
		child = ggiWidgetUnlinkChild(ft->remote, GWT_LAST_CHILD);
		ggiWidgetDestroy(child);
	}
	GWT_SET_ICHANGED(ft->remote);

	fe = ft->files;
	if (strcmp(ft->remote_path, "/")) {
		fe->filename = strdup(".");
		if (!fe->filename)
			return close_connection(cx, -1);
		fe->flags = DIRECTORY;
		++fe;
		++virt_files;
		fe->filename = strdup("..");
		if (!fe->filename)
			return close_connection(cx, -1);
		fe->flags = DIRECTORY;
		++fe;
		++virt_files;
	}

	for (i = 0; i < files && file + 22 < &ptr[raw]; ++i) {
#ifdef GG_HAVE_INT64
		uint64_t size = get64_hilo(file);
		uint64_t mod = get64_hilo(file + 8);
		time_t t = mod / 1000;
#else
		uint32_t size =
			get32_hilo(file) ? 0xffffffff : get32_hilo(file + 4);
		uint32_t mod = get32_hilo(file + 8);
		mod %= 1000;
		mod = (mod << 16) | get16_hilo(file + 10);
		time_t t = (mod / 1000) << 16;
		mod %= 1000;
		mod = (mod << 16) | get16_hilo(file + 12);
		t |= mod / 1000;
#endif
		uint16_t flags = get16_hilo(file + 16);
		uint32_t name_len = get32_hilo(file + 18);

		if (file + 18 + name_len > &ptr[raw])
			break;
		fe->filename = malloc(name_len + 1);
		if (!fe->filename)
			break;
		memcpy(fe->filename, file + 22, name_len);
		fe->filename[name_len] = 0;

		debug(2, "\"%s\" %u %u, %s", fe->filename, (int)size, (int)mod,
			t >= 0 ? asctime(gmtime(&t)) : "neg date\n");

		fe->flags = flags;
		fe->filesize = size;
#ifdef GG_HAVE_INT64
		fe->mod = mod;
#else
		fe->mod = t;
#endif
		++fe;

		file += 22 + name_len;
	}

	if (i != files)
		return close_connection(cx, -1);

	fe = ft->files;
	qsort(fe, files + virt_files, sizeof(*fe), order_file_entries_alpha);

	if (fill_remote_widget(cx))
		return close_connection(cx, -1);

	cx->input.rpos += 13 + raw;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_list_request(struct connection *cx, const char *dir)
{
	uint8_t *flr;
	size_t len;

	if (cx->file_transfer == 1)
		return file_v1_list_request(cx, dir);

	len = strlen(dir);
	flr = malloc(9 + len);
	if (!flr) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}

	insert32_hilo(&flr[0], 0xfc000102);
	flr[4] = 0;
	insert32_hilo(&flr[5], len);
	memcpy(&flr[9], dir, len);

	if (safe_write(cx, flr, 9 + len)) {
		free(flr);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(flr);

	cx->encoding_def[tight_file].action = file_list_reply;

	return 0;
}

static int
file_download_data_request(struct connection *cx)
{
	uint8_t fddr[9];

	debug(2, "file_download_start_reply\n");

	insert32_hilo(fddr, 0xfc00010e);
	fddr[4] = 0;
	insert32_hilo(&fddr[5], 32768);

	if (safe_write(cx, fddr, 9)) {
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	cx->encoding_def[tight_file].action = file_download_data;
	return 0;
}

static int
file_download_start_reply(struct connection *cx)
{
	debug(2, "file_download_start_reply\n");

	if (file_download_data_request(cx))
		return -1;

	cx->input.rpos += 4;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 0;
}

static int
file_download_data_reply(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t compression;
	uint32_t len;
	uint32_t raw;
	uint8_t *data;

	debug(2, "file_download_data_reply\n");

	if (cx->input.wpos < cx->input.rpos + 13)
		return 0;

	compression = cx->input.data[cx->input.rpos + 4];
	len = get32_hilo(&cx->input.data[cx->input.rpos + 5]);
	raw = get32_hilo(&cx->input.data[cx->input.rpos + 9]);

	debug(2, "compression %0x, len %d, raw-len %d\n",
		compression, len, raw);

	if (compression)
		return close_connection(cx, -1);
	if (len != raw)
		return close_connection(cx, -1);

	if (cx->input.wpos < cx->input.rpos + 13 + raw)
		return 0;

	debug(2, "got file download data (raw %d)\n", raw);

	cx->input.rpos += 13 + raw;

	if (ft->download) {
		if (ft->feedback && ft->total) {
			ggi_widget_t progress;
			progress = ggiWidgetGetChild(ft->feedback, 0);
			while (ggiWidgetGetChild(progress, 0))
				progress = ggiWidgetGetChild(progress, 0);
			GWT_SET_ICHANGED(progress);
		}
		ft->current += raw;
		if (ft->total)
			ft->progress = (double)ft->current / ft->total;

		data = &cx->input.data[13];
		while (raw) {
			int res = fwrite(data, 1, raw, ft->download);
			if (res == raw)
				break;
			raw -= res;
			data += res;
			if (!ferror(ft->download))
				continue;
			set_feedback(cx, "Error writing to download file");
			fclose(ft->download);
			ft->download = NULL;
			break;
		}
		
	}

	if (file_download_data_request(cx))
		return -1;

	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_download_data_end(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t flags;
#ifdef GG_HAVE_INT64
	uint64_t mod;
#else
	uint32_t mod;
#endif
	time_t t;

	debug(2, "file_download_data_end\n");

	if (cx->input.wpos < cx->input.rpos + 13)
		return 0;

	flags = cx->input.data[cx->input.rpos + 4];
#ifdef GG_HAVE_INT64
	mod = get64_hilo(&cx->input.data[cx->input.rpos + 5]);
	t = mod / 1000;
#else
	mod = get32_hilo(&cx->input.data[cx->input.rpos + 5]);
	mod %= 1000;
	mod = (mod << 16) | get16_hilo(&cx->input.data[cx->input.rpos + 7]);
	t = (mod / 1000) << 16;
	mod %= 1000;
	mod = (mod << 16) | get16_hilo(&cx->input.data[cx->input.rpos + 9]);
	t |= mod / 1000;
#endif
	debug(1, "got file download end (mod %u), %s",
		(int)mod, t >= 0 ? asctime(gmtime(&t)) : "neg date\n");

	if (ft->download) {
		set_feedback(cx, "Download complete");

		fclose(ft->download);
		ft->download = NULL;
	}

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	cx->input.rpos += 13;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_download_failed(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint32_t len;
	char *reason;

	debug(2, "file_download_failed\n");

	if (cx->input.wpos < cx->input.rpos + 8)
		return 0;

	len = get32_hilo(&cx->input.data[cx->input.rpos + 4]);

	if (cx->input.wpos < cx->input.rpos + 8 + len)
		return 0;

	reason = malloc(len + 1);
	memcpy(reason, &cx->input.data[cx->input.rpos + 8], len);
	reason[len] = '\0';
	debug(1, "got file download failure \"%s\"\n", reason);
	free(reason);
	reason = NULL;

	if (ft->download) {
		set_feedback(cx, "Download failed");
		fclose(ft->download);
		ft->download = NULL;
	}

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	cx->input.rpos += 8 + len;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_download_data(struct connection *cx)
{
	if (cx->input.wpos < cx->input.rpos + 4)
		return vnc_unexpected(cx);

	switch (get32_hilo(&cx->input.data[cx->input.rpos])) {
	case 0xfc00010d:
		return file_download_start_reply(cx);
	case 0xfc00010f:
		return file_download_data_reply(cx);
	case 0xfc000110:
		return file_download_data_end(cx);
	case 0xfc000119:
		return file_download_failed(cx);
	}

	return vnc_unexpected(cx);
}

static int
file_download_request(struct connection *cx,
	const char *path, const char *file)
{
	uint8_t *fdlr;
	size_t pathlen;
	size_t filelen;
	int slash;

	if (cx->file_transfer == 1)
		return file_v1_download_request(cx, path, file);

	pathlen = strlen(path);
	filelen = strlen(file);
	slash = pathlen && path[pathlen - 1] != '/';

	if (pathlen + slash + filelen > 0xffff) {
		debug(1, "path too long\n");
		close_connection(cx, -1);
		return -1;
	}
	fdlr = malloc(8 + pathlen + slash + filelen + 8);
	if (!fdlr) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}
	insert32_hilo(fdlr, 0xfc00010c);
	insert32_hilo(&fdlr[4], pathlen + slash + filelen);
	memcpy((char *)&fdlr[8], path, pathlen);
	if (slash)
		fdlr[8 + pathlen] = '/';
	memcpy((char *)&fdlr[8 + pathlen + slash], file, filelen);
#ifdef GG_HAVE_INT64
	insert64_hilo(&fdlr[8 + pathlen + slash + filelen], 0);
#else
	insert32_hilo(&fdlr[8 + pathlen + slash + filelen], 0);
	insert32_hilo(&fdlr[8 + pathlen + slash + filelen + 4], 0);
#endif

	if (safe_write(cx, fdlr, 8 + pathlen + slash + filelen + 8)) {
		free(fdlr);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(fdlr);

	cx->encoding_def[tight_file].action = file_download_data;

	return 0;
}

static int
file_upload_request(struct connection *cx, const char *path, const char *file)
{
	uint8_t *fulr;
	size_t pathlen;
	size_t filelen;
	int slash;

	if (cx->file_transfer == 1)
		return file_v1_upload_request(cx, path, file);

	if (cx->no_input)
		return 1;

	pathlen = strlen(path);
	filelen = strlen(file);
	slash = pathlen && path[pathlen - 1] != '/';

	if (pathlen + slash + filelen > 0xffff) {
		debug(1, "path too long\n");
		close_connection(cx, -1);
		return -1;
	}
	fulr = malloc(17 + pathlen + slash + filelen);
	if (!fulr) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		return -1;
	}
	insert32_hilo(fulr, 0xfc000106);
	insert32_hilo(&fulr[4], pathlen + slash + filelen);
	memcpy((char *)&fulr[8], path, pathlen);
	if (slash)
		fulr[8 + pathlen] = '/';
	memcpy((char *)&fulr[8 + pathlen + slash], file, filelen);
	fulr[8 + pathlen + slash + filelen] = 0;
#ifdef GG_HAVE_INT64
	insert64_hilo(&fulr[8 + pathlen + slash + filelen + 1], 0);
#else
	insert32_hilo(&fulr[8 + pathlen + slash + filelen + 1], 0);
	insert32_hilo(&fulr[8 + pathlen + slash + filelen + 5], 0);
#endif

	if (safe_write(cx, fulr, 17 + pathlen + slash + filelen)) {
		free(fulr);
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	free(fulr);

	cx->encoding_def[tight_file].action = file_upload_data;

	return 0;
}

static int
file_upload_end_request(struct connection *cx, time_t t)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t buf[14];
#ifdef GG_HAVE_INT64
	uint64_t mod = t;
#else
	uint32_t mod = t;
#endif

	if (!ft->upload)
		return 0;

	insert32_hilo(buf, 0xfc00010a);
	insert16_hilo(&buf[4], 0);
#ifdef GG_HAVE_INT64
	insert64_hilo(&buf[6], mod * 1000);
#else
	{
		uint32_t tmplo = (mod & 0xffff) * 1000;
		uint32_t tmphi = (mod >> 16) * 1000;
		insert32_hilo(&buf[6], ((tmplo >> 16) + tmphi) >> 16);
		insert32_hilo(&buf[10], tmplo + (tmphi << 16));
	}
#endif

	if (safe_write(cx, buf, sizeof(buf))) {
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	cx->encoding_def[tight_file].action = file_upload_data;

	return 0;
}

static int
file_upload_data_request(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t buf[13 + 4096];
	size_t res;

	if (!ft->upload)
		return 0;

	res = fread(buf + 13, 1, sizeof(buf) - 13, ft->upload);
	if (ferror(ft->upload)) {
		fclose(ft->upload);
		ft->upload = NULL;
		set_feedback(cx, "Upload failed with read error");
		return file_upload_end_request(cx, time(NULL));
	}
	if (!res)
		return file_upload_end_request(cx, time(NULL));

	insert32_hilo(buf, 0xfc000108);
	buf[4] = 0;
	insert32_hilo(&buf[5], res);
	insert32_hilo(&buf[9], res);

	if (safe_write(cx, buf, 13 + res)) {
		debug(1, "write failed\n");
		close_connection(cx, -1);
		return -1;
	}

	cx->encoding_def[tight_file].action = file_upload_data;

	if (ft->feedback && ft->total) {
		ggi_widget_t progress;
		progress = ggiWidgetGetChild(ft->feedback, 0);
		while (ggiWidgetGetChild(progress, 0))
			progress = ggiWidgetGetChild(progress, 0);
		GWT_SET_ICHANGED(progress);
	}
	ft->current += res;
	if (ft->total)
		ft->progress = (double)ft->current / ft->total;

	return 0;
}

static int
file_upload_start_reply(struct connection *cx)
{
	debug(2, "file_upload_start_reply\n");

	if (file_upload_data_request(cx))
		return -1;

	cx->input.rpos += 4;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 0;
}

static int
file_upload_data_reply(struct connection *cx)
{
	debug(2, "file_upload_data_reply\n");

	if (file_upload_data_request(cx))
		return -1;

	cx->input.rpos += 4;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 0;
}

static int
file_upload_end_reply(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;

	debug(2, "file_upload_end_reply\n");

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	if (ft->upload) {
		fclose(ft->upload);
		ft->upload = NULL;
		set_feedback(cx, "Upload complete");
	}

	cx->input.rpos += 4;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 0;
}

static int
file_upload_failed(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint32_t len;
	char *reason;

	debug(2, "file_upload_failed\n");

	if (cx->input.wpos < cx->input.rpos + 8)
		return 0;

	len = get32_hilo(&cx->input.data[cx->input.rpos + 4]);

	if (cx->input.wpos < cx->input.rpos + 8 + len)
		return 0;

	reason = malloc(len + 1);
	memcpy(reason, &cx->input.data[cx->input.rpos + 8], len);
	reason[len] = '\0';
	debug(1, "got file upload failure \"%s\"\n", reason);
	free(reason);
	reason = NULL;

	if (ft->upload) {
		set_feedback(cx, "Upload failed");
		fclose(ft->upload);
		ft->upload = NULL;
	}

	cx->encoding_def[tight_file].action = vnc_unexpected;
	ft->wait_remote = 0;

	cx->input.rpos += 8 + len;
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
file_upload_data(struct connection *cx)
{
	if (cx->input.wpos < cx->input.rpos + 4)
		return vnc_unexpected(cx);

	switch (get32_hilo(&cx->input.data[cx->input.rpos])) {
	case 0xfc000107:
		return file_upload_start_reply(cx);
	case 0xfc000109:
		return file_upload_data_reply(cx);
	case 0xfc00010b:
		return file_upload_end_reply(cx);
	case 0xfc000119:
		return file_upload_failed(cx);
	}

	return vnc_unexpected(cx);
}

static void
local_cb(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct connection *cx = widget->callbackpriv;
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	gwt_fileentry_t **selfiles = widget->statevar;
	char *dir;

	if (cbt != GWT_CB_ACTIVATE)
		return;
	if (!selfiles[0] || selfiles[1])
		return;
	if (!(selfiles[0]->flags & GWT_FE_FL_ISDIR))
		return;

	debug(1, "change local dir \"%s\"\n", selfiles[0]->filename);

	if (!strcmp(selfiles[0]->filename, ".")) {
		ggiWidgetSendControl(widget, GWT_CONTROLFLAG_NONE,
			"SETDIR", ft->local_path);
		return;
	}

	if (!strcmp(selfiles[0]->filename, "..")) {
		char *last = strrchr(ft->local_path, '/');
		if (!last)
			return;
		if (last == ft->local_path) {
			if (!last[1])
				return;
			++last;
		}
		*last = '\0';
		ggiWidgetSendControl(widget, GWT_CONTROLFLAG_NONE,
			"SETDIR", ft->local_path);
		return;
	}

	dir = realloc(ft->local_path,
		strlen(ft->local_path) +
		strlen(selfiles[0]->filename) + 2);
	if (!dir)
		return;
	ft->local_path = dir;
	if (ft->local_path[0] != '/' || ft->local_path[1])
		strcat(ft->local_path, "/");
	strcat(ft->local_path, selfiles[0]->filename);
	ggiWidgetSendControl(widget, GWT_CONTROLFLAG_NONE,
		"SETDIR", ft->local_path);
}

static void
remote_cb(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct connection *cx = widget->callbackpriv;
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	int selected = 0;
	ggi_widget_t active = NULL;
	int i;
	struct file_entry *fe;
	char *path;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	if (ft->wait_remote)
		return;

	for (i = 0; ggiWidgetGetChild(ft->remote, i); ++i) {
		if (!((uint8_t *)ft->remote->statevar)[i])
			continue;
		++selected;
		active = ggiWidgetGetChild(ft->remote, i);
	}

	if (selected != 1) {
		debug(1, "more than one selected\n");
		return;
	}

#ifdef ggiWidgetGetChild
	/* using macro kludge, therefore I see internal widgets */
	active = ggiWidgetGetChild(active, 0);
#endif
	fe = active->callbackpriv;
	if (!fe || !(fe->flags & DIRECTORY)) {
		debug(1, "not a dir\n");
		return;
	}

	if (!strcmp(fe->filename, ".")) {
		debug(1, "reload remote dir \"%s\"\n", fe->filename);
		if (file_list_request(cx, ft->remote_path))
			return;
		set_feedback(cx, "Waiting for remote directory...");
		ft->wait_remote = 1;
		return;
	}

	if (!strcmp(fe->filename, "..")) {
		char *last = strrchr(ft->remote_path, '/');
		debug(1, "change remote dir \"%s\"\n", fe->filename);
		if (!last)
			return;
		if (last == ft->remote_path) {
			if (!last[1])
				return;
			++last;
		}
		*last = '\0';
		if (file_list_request(cx, ft->remote_path))
			return;
		set_feedback(cx, "Waiting for remote directory...");
		ft->wait_remote = 1;
		return;
	}

	debug(1, "change remote dir \"%s\"\n", fe->filename);
	path = realloc(ft->remote_path,
		strlen(ft->remote_path) +
		strlen(fe->filename) + 2);
	if (!path)
		return;
	ft->remote_path = path;
	if (ft->remote_path[0] != '/' || ft->remote_path[1])
		strcat(ft->remote_path, "/");
	strcat(ft->remote_path, fe->filename);
	if (file_list_request(cx, ft->remote_path))
		return;
	set_feedback(cx, "Waiting for remote directory...");
	ft->wait_remote = 1;
}

int
file_upload_fragment(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	uint8_t buf[4096];
	size_t res;
	gii_event ev;

	if (cx->file_transfer != 1)
		return 0;

	if (cx->write_drained) {
		debug(2, "output buffer drained\n");
		cx->write_drained = NULL;
	}

	if (!ft->upload)
		return 0;

	res = fread(buf, 1, sizeof(buf), ft->upload);
	if (ferror(ft->upload)) {
		fclose(ft->upload);
		ft->upload = NULL;
		ft->wait_remote = 0;
		set_feedback(cx, "Upload failed with read error");
		return file_v1_upload_failed(cx, "Read error.");
	}
	if (!res) {
		fclose(ft->upload);
		ft->upload = NULL;
		ft->wait_remote = 0;
		if (file_v1_upload_data_last(cx, time(NULL)))
			return -1;
		set_feedback(cx, "Upload complete");
		return 0;
	}

	if (file_v1_upload_data(cx, buf, res))
		return -1;

	if (ft->feedback && ft->total) {
		ggi_widget_t progress;
		progress = ggiWidgetGetChild(ft->feedback, 0);
		while (ggiWidgetGetChild(progress, 0))
			progress = ggiWidgetGetChild(progress, 0);
		GWT_SET_ICHANGED(progress);
	}
	ft->current += res;
	if (ft->total)
		ft->progress = (double)ft->current / ft->total;

	if (cx->output.wpos >= 65536) {
		debug(2, "wait for output buffer to drain\n");
		cx->write_drained = file_upload_fragment;
		return 0;
	}

	ev.any.target = GII_EV_TARGET_QUEUE;
	ev.any.size = sizeof(gii_cmd_nodata_event);
	ev.any.type = evCommand;
	ev.cmd.code = UPLOAD_FILE_FRAGMENT_CMD;

	giiEventSend(cx->stem, &ev);
	return 0;
}

static void
file_upload(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct connection *cx = widget->callbackpriv;
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	gwt_fileentry_t **selfiles = ft->local->statevar;
	char *file;
	size_t pathlen;
	struct stat st;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	if (cx->no_input)
		return;

	if (ft->wait_remote)
		return;

	if (!selfiles[0] || selfiles[1]) {
		debug(1, "not one selected\n");
		return;
	}
	if (selfiles[0]->flags & (GWT_FE_FL_ISDIR | GWT_FE_FL_ISSPECIAL_MASK))
	{
		debug(1, "not a file\n");
		return;
	}

	debug(1, "upload \"%s\" from \"%s\"\n",
		selfiles[0]->filename, ft->local_path);

	pathlen = strlen(ft->local_path);
	file = malloc(pathlen + strlen(selfiles[0]->filename) + 2);
	if (!file) {
		debug(1, "out of memory\n");
		return;
	}
#ifdef _WIN32
	if (pathlen >= 3 &&
		ft->local_path[0] == '/' && ft->local_path[2] == ':')
	{
		strcpy(file, ft->local_path + 1);
	}
	else
#endif
	strcpy(file, ft->local_path);
	if (pathlen && ft->local_path[pathlen - 1] != '/')
		strcat(file, "/");
	strcat(file, selfiles[0]->filename);

	if (stat(file, &st)) {
		debug(1, "Failed to stat \"%s\".\n", file);
		free(file);
		return;
	}
	ft->upload = fopen(file, "rb");
	if (!ft->upload) {
		debug(1, "Failed to open \"%s\" for reading.\n", file);
		free(file);
		return;
	}

	free(file);

	if (file_upload_request(cx, ft->remote_path, selfiles[0]->filename))
		return;

	ft->progress = 0.0;
	ft->current = 0;
	ft->total = st.st_size < 0 ? 0 : st.st_size;
	if (ft->total)
		progress_feedback(cx);
	else
		set_feedback(cx, "Uploading file...");

	ft->wait_remote = 1;

	if (cx->file_transfer == 1)
		file_upload_fragment(cx);
}

static void
file_download(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct connection *cx = widget->callbackpriv;
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	int selected = 0;
	ggi_widget_t active = NULL;
	int i;
	struct file_entry *fe;
	char *file;
	size_t pathlen;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	if (ft->wait_remote) {
		debug(1, "pending request\n");
		return;
	}

#ifdef _WIN32
	if (!strcmp(ft->local_path, "/")) {
		debug(1, "can't download to virtual root\n");
		return;
	}
#endif

	for (i = 0; ggiWidgetGetChild(ft->remote, i); ++i) {
		if (!((uint8_t *)ft->remote->statevar)[i])
			continue;
		++selected;
		active = ggiWidgetGetChild(ft->remote, i);
	}

	if (selected != 1) {
		debug(1, "more than one selected\n");
		return;
	}

#ifdef ggiWidgetGetChild
	/* using macro kludge, therefore I see internal widgets */
	active = ggiWidgetGetChild(active, 0);
#endif
	fe = active->callbackpriv;
	if (!fe || (fe->flags & DIRECTORY)) {
		debug(1, "not a file\n");
		return;
	}

	debug(1, "download \"%s\" from \"%s\"\n",
		fe->filename, ft->remote_path);

	pathlen = strlen(ft->local_path);
	file = malloc(pathlen + strlen(fe->filename) + 2);
	if (!file) {
		debug(1, "out of memory\n");
		return;
	}
#ifdef _WIN32
	if (pathlen >= 3 &&
		ft->local_path[0] == '/' && ft->local_path[2] == ':')
	{
		strcpy(file, ft->local_path + 1);
	}
	else
#endif
	strcpy(file, ft->local_path);
	if (pathlen && ft->local_path[pathlen - 1] != '/')
		strcat(file, "/");
	strcat(file, fe->filename);

	ft->download = fopen(file, "wb");
	if (!ft->download) {
		debug(1, "Failed to open \"%s\" for writing.\n", file);
		free(file);
		return;
	}
	free(file);

	if (file_download_request(cx, ft->remote_path, fe->filename))
		return;

	ft->progress = 0.0;
	ft->current = 0;
	ft->total = fe->filesize;
	if (ft->total)
		progress_feedback(cx);
	else
		set_feedback(cx, "Downloading file...");

	ft->wait_remote = 1;
}

static void
file_transfer_end(struct connection *cx)
{
	struct file_transfer *ft = cx->encoding_def[tight_file].priv;
	struct file_entry *fe;

	if (!ft)
		return;

	if (ft->files) {
		for (fe = ft->files; fe->filename; ++fe)
			free(fe->filename);
		free(ft->files);
	}

	if (ft->download)
		fclose(ft->download);
	if (ft->upload)
		fclose(ft->upload);

	free(cx->encoding_def[tight_file].priv);
	cx->encoding_def[tight_file].priv = NULL;
	cx->encoding_def[tight_file].action = vnc_unexpected;
}

static int
file_transfer_init(struct connection *cx)
{
	struct file_transfer *ft;

	if (cx->encoding_def[tight_file].priv)
		return 0;

	debug(1, "file_transfer_init\n");

	ft = malloc(sizeof(*ft));
	if (!ft) {
		close_connection(cx, -1);
		return -1;
	}
	memset(ft, 0, sizeof(*ft));

	cx->encoding_def[tight_file].priv = ft;
	cx->encoding_def[tight_file].end = file_transfer_end;

	cx->encoding_def[tight_file].action = vnc_unexpected;
	return 0;
}

static int
no_dotdot_in_root(ggi_widget_t self,
	const char *directory, gwt_fileentry_t *file)
{
	if (strcmp(file->filename, ".."))
		return 0;
	return !strcmp(directory, "/");
}

int
show_file_transfer(struct connection *cx)
{
	ggi_widget_t clip;
	ggi_widget_t item;
	ggi_widget_t line;
	ggi_widget_t stack;
	ggi_widget_t button;
	ggi_widget_t dlg = NULL;
	struct file_transfer *ft;
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

	debug(1, "show_file_transfer\n");

	if (file_transfer_init(cx)) {
		close_connection(cx, -1);
		return -1;
	}
	ft = cx->encoding_def[tight_file].priv;
	ft->done = 0;

	if (!ft->remote_path)
		ft->remote_path = strdup("/");
	if (!ft->remote_path) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		goto destroy_dlg;
	}
	if (!ft->wait_remote) {
		if (file_list_request(cx, ft->remote_path))
			goto destroy_dlg;
		ft->wait_remote = 1;
	}
	else
		ft->wait_remote = 2;

	dlg = ggiWidgetCreateContainerStack(2, NULL);
	if (!dlg)
		goto destroy_dlg;
	dlg->pad.t = dlg->pad.l = dlg->pad.b = dlg->pad.r = 10;

	line = ggiWidgetCreateContainerLine(20, NULL);
	if (!line)
		goto destroy_dlg;
	line->pad.b = 5;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, line);

	if (!ft->local_path)
		ft->local_path = strdup("/");
	if (!ft->local_path) {
		debug(1, "out of memory\n");
		close_connection(cx, -1);
		goto destroy_dlg;
	}

	clip = ggiWidgetCreateContainerClipframe(235, 315, NULL);
	if (!clip)
		goto destroy_dlg;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, clip);
	ft->local = ggiWidgetCreateFileSelector(220, 300, ft->local_path,
		no_dotdot_in_root, ggiWidgetFileSelectorSortDirAlpha);
	if (!ft->local)
		goto destroy_dlg;
	ft->local->callback = local_cb;
	ft->local->callbackpriv = cx;
	ft->local->gravity = GWT_GRAV_NORTH | GWT_GRAV_WEST;
	ggiWidgetLinkChild(clip, GWT_LAST_CHILD, ft->local);

	stack = ggiWidgetCreateContainerStack(10, NULL);
	if (!stack)
		goto destroy_dlg;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, stack);

	if (!cx->no_input) {
		button = ggiWidgetMakeImagebutton(&right);
		if (!button)
			goto destroy_dlg;
		button->callback = file_upload;
		button->callbackpriv = cx;
		ggiWidgetLinkChild(stack, GWT_LAST_CHILD, button);
	}

	button = ggiWidgetMakeImagebutton(&left);
	if (!button)
		goto destroy_dlg;
	button->callback = file_download;
	button->callbackpriv = cx;
	ggiWidgetLinkChild(stack, GWT_LAST_CHILD, button);

	clip = ggiWidgetCreateContainerClipframe(235, 315, NULL);
	if (!clip)
		goto destroy_dlg;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, clip);
	ft->remote = ggiWidgetCreateContainerSelectlist(1, NULL);
	if (!ft->remote)
		goto destroy_dlg;
	ft->remote->pad.l = ft->remote->pad.r = 1;
	ft->remote->callbackpriv = cx;
	ft->remote->callback = remote_cb;
	ft->remote->gravity = GWT_GRAV_NORTH | GWT_GRAV_STRETCH_X;
	item = ggiWidgetCreateContainerScroller(
		220, 300,
		10, 10, 10,
		0.01, 0.01,
		GWT_SCROLLER_OPTION_X_AUTO |
		GWT_SCROLLER_OPTION_Y_AUTO |
		GWT_SCROLLER_OPTION_X_AUTOBARSIZE |
		GWT_SCROLLER_OPTION_Y_AUTOBARSIZE,
		ft->remote);
	if (!item) {
		ggiWidgetDestroy(ft->remote);
		goto destroy_dlg;
	}
	button = ggiWidgetCreateFrame(GWT_FRAMEGROUP_GROUPING, 2,
		GWT_FRAMETYPE_3D_IN, item);
	if (!button) {
		ggiWidgetDestroy(item);
		goto destroy_dlg;
	}
	GWT_WIDGET_MAKE_NOT_FOCUSABLE(button);
	button->gravity = GWT_GRAV_NORTH | GWT_GRAV_WEST;
	ggiWidgetLinkChild(clip, GWT_LAST_CHILD, button);

	ft->feedback = ggiWidgetCreateContainerClipframe(450, 25, NULL);
	if (!ft->feedback)
		goto destroy_dlg;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, ft->feedback);
	if (ft->download || ft->upload) {
		int res;
		if (ft->total)
			res = progress_feedback(cx);
		else if (ft->download)
			res = set_feedback(cx, "Downloading file...");
		else
			res = set_feedback(cx, "Uploading file...");
		if (res)
			goto destroy_dlg;
	}
	else if (set_feedback(cx, "Waiting for remote directory..."))
		goto destroy_dlg;

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
	button->callback = file_close;
	button->callbackpriv = cx;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, button);

	if (ft->wait_remote == 2) {
		ft->wait_remote = 1;
		if (fill_remote_widget(cx))
			goto destroy_dlg;
	}

	ggiWidgetFocus(button, NULL, NULL);

	debug(1, "popup_dialog\n");
	dlg = popup_dialog(cx, dlg, &ft->done, NULL, NULL);

destroy_dlg:
	ggiWidgetDestroy(dlg);
	ft->feedback = NULL;
	if (cx->close_connection)
		return -1;
	return ft->done;
}
