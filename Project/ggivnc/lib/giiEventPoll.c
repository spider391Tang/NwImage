/*
******************************************************************************

   giiEventPoll for ggivnc, GII 1.x does not support input-fdselect.

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

#include "vnc.h"
#include "vnc-compat.h"
#include <ggi/ggi-unix.h>

static struct gg_instance *the_one_instance;

int
giiEventPoll(ggi_visual_t vis, gii_event_mask mask, struct timeval *tv)
{
	observe_cb *cb = the_one_instance->cb;
	struct connection *cx = the_one_instance->arg;
	ggi_event_mask mask_orig = mask;
	struct gii_fdselect_fd fd;
	fd_set rfds;
	fd_set wfds;

	do {
		mask = mask_orig;
		FD_ZERO(&rfds);
		if (cx->want_read)
			FD_SET(cx->sfd, &rfds);
		FD_ZERO(&wfds);
		if (cx->want_write)
			FD_SET(cx->sfd, &wfds);

		ggiEventSelect(vis, &mask, cx->sfd + 1,
			&rfds, &wfds, NULL, NULL);

		if (FD_ISSET(cx->sfd, &rfds)) {
			fd.mode = GII_FDSELECT_READ;
			fd.fd = cx->sfd;
			cb(cx, GII_FDSELECT_READY, &fd);
		}
		if (FD_ISSET(cx->sfd, &wfds)) {
			fd.mode = GII_FDSELECT_WRITE;
			fd.fd = cx->sfd;
			cb(cx, GII_FDSELECT_READY, &fd);
		}

		if (cx->close_connection)
			break;
	} while (!mask);

	return mask;
}

struct gg_instance *
ggPlugModule(void *api, ggi_visual_t stem, const char *name,
	const char *argstr, void *argptr)
{
	the_one_instance = malloc(sizeof(*the_one_instance));
	if (!the_one_instance)
		return NULL;

	the_one_instance->channel = the_one_instance;
	return the_one_instance;
}

void
ggObserve(void *channel, observe_cb *cb, void *arg)
{
	struct gg_instance *instance = channel;

	instance->cb = cb;
	instance->arg = arg;
}

void
ggControl(void *channel, uint32_t code, void *arg)
{
}

void
ggClosePlugin(struct gg_instance *instance)
{
	free(instance);
	the_one_instance = NULL;
}
