/*
******************************************************************************

   VNC viewer gii pseudo-encoding.

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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <ggi/gg.h>
#include <ggi/gii.h>
#include <ggi/ggi.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"


typedef uint16_t (read16_func)(const uint8_t *);
typedef uint32_t (read32_func)(const uint8_t *);

int
gii_inject(gii_event *ev)
{
	gii_device *device;
	uint8_t buf[248] = {
		253,
#ifdef GG_BIG_ENDIAN
		0x80,
		0, ev->any.size - 12
#else
		0,
		ev->any.size - 12, 0
#endif
	};

	if (!g.gii)
		return 0;

	if (g.no_input)
		return 0;

	if ((1 << ev->any.type) & emValuator) {
		ev->any.size = 28 + ev->val.count * 4;
#ifdef GG_BIG_ENDIAN
		buf[3] = ev->any.size - 12;
#else
		buf[2] = ev->any.size - 12;
#endif
	}
	else if (!((1 << ev->any.type) & (emCommand | emKey | emPointer)))
		return 0;

	device_FOREACH(device) {
		if (!device->remote_origin)
			continue;
		if (device->local_origin == ev->cmd.origin)
			break;
	}
	if (!device)
		return 0;

	debug(1, "inject event: size %d, type %d\n",
		ev->any.size, ev->any.type);

	memcpy(&buf[4], ev, 4);
	buf[4] -= 12;
	memcpy(&buf[8], &device->remote_origin, 4);
	memcpy(&buf[12], ((uint8_t *)ev) + sizeof(gii_any_event),
		ev->any.size - sizeof(gii_any_event));

	return safe_write(g.sfd, buf, ev->any.size - 8) ? -1 : 1;
}

int
gii_create_device(uint32_t origin, struct gii_cmddata_devinfo *dev)
{
	uint32_t i;
	struct gii_cmddata_valinfo val;
	int size;
	uint8_t *buf;
	uint8_t *dst;
	int res;
	gii_device *device;

	/* XXX Should test for the device origin... */
	if (!strcmp(dev->devname, "File descriptor input"))
		return -1;

	device_FOREACH(device) {
		if (device->local_origin == origin)
			return -1;
	}

	if ((65536 - sizeof(*dev)) / 116 < dev->num_valuators) {
		debug(1, "device too big\n");
		return -1;
	}
	size = sizeof(*dev) + dev->num_valuators * 116;
	buf = malloc(4 + size);
	if (!buf) {
		debug(1, "memory shortage\n");
		return -1;
	}

	buf[0] = 253;
#ifdef GG_BIG_ENDIAN
	buf[1] = 0x82;
	buf[2] = size >> 8;
	buf[3] = size;
#else
	buf[1] = 2;
	buf[2] = size;
	buf[3] = size >> 8;
#endif

	dst = buf + 4;
	memcpy(dst, dev, sizeof(*dev));
	dst += sizeof(*dev);
	for (i = 0; i < dev->num_valuators; ++i, dst += 116) {
		int32_t phystype;
		if (giiQueryValInfo(g.stem, origin, i, &val)) {
			debug(1, "no valuator?\n");
			free(buf);
			return -1;
		}

		phystype = val.phystype;
		memcpy(dst, &val, 96);
		memcpy(&dst[96], &phystype, 4);
		memcpy(&dst[100], &val.SI_add, 16);
	}

	device = malloc(sizeof(*device));
	device->local_origin = origin;
	device->remote_origin = 0;
	device_INSERT(device);

	res = safe_write(g.sfd, buf, 4 + size);
	free(buf);
	if (res) {
		debug(1, "write failed\n");
		return -1;
	}

	debug(1, "gii device created \"%s\"\n", dev->devname);

	return 0;
}

int
gii_delete_device(gii_event *ev)
{
	gii_device *device;
	uint8_t buf[8] = {
		253,
#ifdef GG_BIG_ENDIAN
		0x83,
		0, 4
#else
		3,
		4, 0
#endif
	};

	if (g.no_input)
		return 0;

	device_FOREACH(device) {
		if (!device->remote_origin)
			continue;
		if (device->local_origin == ev->cmd.origin)
			break;
	}
	if (!device)
		return 0;

	debug(1, "delete gii device\n");

	memcpy(&buf[4], &device->remote_origin, 4);

	device_REMOVE(device);
	free(device);

	return safe_write(g.sfd, buf, sizeof(buf));
}

static int
gii_version(read16_func *read16, uint16_t length)
{
	uint16_t min_version;
	uint16_t max_version;
	struct gii_source_iter src_iter;
	struct gii_device_iter dev_iter;
	uint8_t buf[6] = {
		253,
		0x81,
		0, 2,
		0, 1
	};

	if (length < 4) {
		debug(1, "short gii version length\n");
		goto done;
	}

	if (g.gii) {
		debug(1, "repeat gii version\n");
		exit(1);
	}

	min_version = read16(&g.input.data[g.input.rpos]);
	max_version = read16(&g.input.data[g.input.rpos + 2]);

	if (min_version > 1) {
		debug(1, "high gii version\n");
		goto done;
	}

	if (min_version > max_version) {
		debug(1, "invalid gii version range\n");
		exit(1);
	}

	if (max_version < 1) {
		/* impossible, but hey... */
		debug(1, "low gii version\n");
		goto done;
	}

	if (safe_write(g.sfd, buf, sizeof(buf))) {
		debug(1, "write failed\n");
		goto done;
	}

	g.gii = 1;

	giiIterFillSources(&src_iter, g.stem);
	giiIterSources(&src_iter);
	GG_ITER_FOREACH(&src_iter) {
		giiIterFillDevices(&dev_iter,
			g.stem, src_iter.src, src_iter.origin);
		giiIterDevices(&dev_iter);
		GG_ITER_FOREACH(&dev_iter)
			gii_create_device(dev_iter.origin, &dev_iter.devinfo);
	}

done:
	g.input.rpos += length;
	remove_dead_data();
	g.action = vnc_wait;
	return 1;
}

static int
gii_device_created(read32_func *read32, uint16_t length)
{
	uint32_t remote_origin;
	gii_device *device_iter;
	gii_device *device;

	if (length != 4) {
		debug(1, "bad device created length\n");
		exit(1);
	}

	if (!g.gii) {
		debug(1, "gii not initialized\n");
		exit(1);
	}

	/* find the earliest pending device create request */
	device = NULL;
	device_FOREACH(device_iter) {
		if (device_iter->remote_origin)
			break;
		device = device_iter;
	}
	if (!device || device->remote_origin) {
		debug(1, "no pending device creation requests\n");
		exit(1);
	}

	remote_origin = read32(&g.input.data[g.input.rpos]);

	if (!remote_origin) {
		debug(1, "device creation failed\n");
		device_REMOVE(device);
		free(device);
	}
	else {
		debug(1, "device created successfully\n");
		device->remote_origin = remote_origin;
	}

	g.input.rpos += length;
	remove_dead_data();
	g.action = vnc_wait;
	return 1;
}

int
gii_receive(void)
{
	uint8_t type;
	read16_func *read16;
	read32_func *read32;
	uint16_t length;

	debug(1, "gii_receive\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	type = g.input.data[g.input.rpos + 1] & 0x7f;

#ifdef GG_BIG_ENDIAN
	if (~g.input.data[g.input.rpos + 1] & 0x80) {
		read16 = get16_r;
		read32 = get32_r;
	}
#else
	if (g.input.data[g.input.rpos + 1] & 0x80) {
		read16 = get16_r;
		read32 = get32_r;
	}
#endif
	else {
		read16 = get16;
		read32 = get32;
	}
		
	length = read16(&g.input.data[g.input.rpos + 2]);

	if (g.input.wpos < g.input.rpos + 4 + length)
		return 0;

	g.input.rpos += 4;

	switch (type) {
	case 1: /* version */
		return gii_version(read16, length);

	case 2: /* device creation result */
		return gii_device_created(read32, length);

	default:
		debug(1, "got unknown gii type %d\n", type);
		exit(1);
	}

	return 1;
}
