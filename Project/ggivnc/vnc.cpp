/*
******************************************************************************

   VNC viewer using the RFB protocol.

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
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <errno.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifdef HAVE_WINDOWS_H
# include <windows.h>
# include <io.h>
#endif
#ifdef HAVE_WINSOCK2_H
# include <winsock2.h>
#endif
#ifdef HAVE_WINSOCK_H
# include <winsock.h>
#endif
#ifdef HAVE_WS2TCPIP_H
# include <ws2tcpip.h>
#endif
#ifdef HAVE_WSPIAPI_H
# include <wspiapi.h>
#endif
#endif

#include <ggi/gg.h>
#include <ggi/gii.h>
#include <ggi/ggi.h>
#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif
#ifdef HAVE_WIDGETS
#include <ggi/ggi_widget.h>
#endif
#ifdef HAVE_INPUT_FDSELECT
#include <ggi/gii-keyboard.h>
#include <ggi/input/fdselect.h>
#else
#include <ggi/keyboard.h>
#endif


extern "C" {
#include "vnc.h"
#include "handshake.h"
#include "vnc-compat.h"
}
#include "vnc-endian.h"
#include "vnc-debug.h"
#include "scrollbar.h"

#include <QDebug>
#include <QImage>
#include "../MLVNC/MLVNC.h"

typedef boost::signals2::signal <void()> BufferRenderedSignalType;

static BufferRenderedSignalType gBufferRenderedEvent;
static unsigned char* gTargetFrameBuffer = NULL;
static std::string gPixformat = "p8b8g8r8";
bool gGgiVncRenderStop = true;

int ggivnc_debug_level;

boost::signals2::connection connectToGgivncBufferRenderedSignal
    (
    const BufferRenderedSignalType::slot_type& aSlot
    )
{
    return gBufferRenderedEvent.connect( aSlot );
}

void setGgivncRenderStop( bool stop )
{
    gGgiVncRenderStop = stop;
}

void setGgivncTargetFrameBuffer( unsigned char* buf )
{
    gTargetFrameBuffer = buf;
}

void setGgivncPixFormat( const std::string& pixformat )
{
    gPixformat = pixformat;
}


/* Given an RFB maximum color value, deduce how many bits are needed
 * in the GGI color mask.
 */
int
color_bits(uint16_t max)
{
	int bits = 0;

	while (max) {
		max >>= 1;
		++bits;
	}

	return bits;
}

/* Given a GGI color mask, deduce the highest color value that
 * can be stored. Assumes that the bits in the mask are
 * consecutive.
 */
static int
color_max(ggi_pixel mask)
{
	while (mask && !(mask & 1))
		mask >>= 1;

	return mask;
}

/* Given a GGI color mask, deduce how far you have to shift
 * left in order to move a color value to the correct position.
 * Assumes that the bits in the mask are consecutive.
 */
static int
color_shift(ggi_pixel mask)
{
	int shift = 0;

	while (mask && !(mask & 1)) {
		mask >>= 1;
		++shift;
	}

	return shift;
}


/* Parse a pixfmt string into its components.
 * Typical pixfmt strings are "r8g8b8p8", "r5g6b5" or "c8".
 */
static int
parse_pixfmt(const char *pixfmt, int *c_max,
	int *r_max, int *g_max, int *b_max,
	int *r_shift, int *g_shift, int *b_shift,
	int *size, int *depth)
{
	unsigned long bits;
	int p = 0;
	int *tmp = NULL;
	char *end;

	*c_max = *r_max = *g_max = *b_max = 0;
	*size = *depth = 0;

	while (*pixfmt) {
		switch (*pixfmt) {
		case 'p':	/* pad */
			if (tmp || *c_max)
				return -1;
			tmp = &p;
			break;
		case 'r':	/* red */
			if (tmp || *r_max || *c_max)
				return -1;
			tmp = r_max;
			*r_shift = 0;
			break;
		case 'g':	/* green */
			if (tmp || *g_max || *c_max)
				return -1;
			tmp = g_max;
			*g_shift = 0;
			break;
		case 'b':	/* blue */
			if (tmp || *b_max || *c_max)
				return -1;
			tmp = b_max;
			*b_shift = 0;
			break;
		case 'c':	/* clut */
			if (tmp || *r_max || *g_max || *b_max || *c_max)
				return -1;
			tmp = c_max;
			break;

		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (!tmp)
				return -1;

			bits = strtoul(pixfmt, &end, 10);
			pixfmt = end - 1;

			if (tmp != &p)
				*depth += bits;
			if (tmp != r_max)
				*r_shift += bits;
			if (tmp != g_max)
				*g_shift += bits;
			if (tmp != b_max)
				*b_shift += bits;
			*size += bits;

			*tmp = (1 << bits) - 1;

			tmp = NULL;
			break;

		default:
			return -1;
		}
		++pixfmt;
	}

	if (*c_max > 255)
		return -1;
	if (!*r_max)
		*r_shift = 0;
	if (!*g_max)
		*g_shift = 0;
	if (!*b_max)
		*b_shift = 0;

	if (!*depth)
		return -1;

	return 0;
}

/* Cope with some non-standard snprintf implementations... */
static inline int
my_snprintf(char *buf, int size, const char *fmt, ...)
{
	va_list ap;
	int res;

	if (!size) /* No space, don't even try */
		return 0;
	va_start(ap, fmt);
	res = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	if (res >= size) /* Some versions return how much is needed... */
		return size - 1;
	if (res < 0) /* ...and some versions error out on truncation */
		return size - 1;
	return res;
}

/* Produce a pixfmt string, given a GGI pixelformat structure.
 * Typical pixfmt strings are "r8g8b8p8", "r5g6b5" or "c8".
 * The pixfmt string is designed to be able to represent the
 * pixel format used in the RFB protocol, with the exception
 * of the endianess.
 * If the GGI pixelformat is not representable in RFB terms,
 * The pixfmt string is set to "weird".
 */
int
generate_pixfmt(char *pixfmt, int count, const ggi_pixelformat *ggi_pf)
{
	int idx;
	int c_max;
	int r_max, g_max, b_max;
	int c_shift;
	int r_shift, g_shift, b_shift;
	int rgb_bits;
	char color;
	int *max, *shift;
	int i;
	int size;

	debug(2, "generate_pixfmt\n");

	debug(2, "  size=%d\n", ggi_pf->size);
	debug(2, "  depth=%d\n", ggi_pf->depth);
	debug(2, "  mask c=%08x r=%08x g=%08x b=%08x\n",
		ggi_pf->clut_mask,
		ggi_pf->red_mask, ggi_pf->green_mask, ggi_pf->blue_mask);

	if (!ggi_pf->size)
		goto weird_pixfmt;
	if (ggi_pf->size == 24)
		goto weird_pixfmt;
	if (ggi_pf->size > 32)
		goto weird_pixfmt;
	if (ggi_pf->size & 7)
		goto weird_pixfmt;
	if (ggi_pf->depth > ggi_pf->size)
		goto weird_pixfmt;

	c_max = color_max(ggi_pf->clut_mask);
	r_max = color_max(ggi_pf->red_mask);
	g_max = color_max(ggi_pf->green_mask);
	b_max = color_max(ggi_pf->blue_mask);
	c_shift = color_shift(ggi_pf->clut_mask);
	r_shift = color_shift(ggi_pf->red_mask);
	g_shift = color_shift(ggi_pf->green_mask);
	b_shift = color_shift(ggi_pf->blue_mask);

	debug(2, "  max c=%d r=%d g=%d b=%d\n",
		c_max, r_max, g_max, b_max);
	debug(2, "  shift c=%d r=%d g=%d b=%d\n",
		c_shift, r_shift, g_shift, b_shift);

	rgb_bits = color_bits(r_max) + color_bits(g_max) + color_bits(b_max);

	debug(2, "  rgb_bits=%d\n", rgb_bits);

	if (!color_bits(c_max) ^ !!rgb_bits)
		goto weird_pixfmt;

	if (rgb_bits) {
		if (ggi_pf->depth != rgb_bits)
			goto weird_pixfmt;
	}
	else if (color_bits(c_max)) {
		if (ggi_pf->depth != color_bits(c_max))
			goto weird_pixfmt;
		if (c_max > 255)
			goto weird_pixfmt;
	}
	else
		goto weird_pixfmt;

	if (c_max + 1 != 1 << color_bits(c_max))
		goto weird_pixfmt;
	if (r_max + 1 != 1 << color_bits(r_max))
		goto weird_pixfmt;
	if (g_max + 1 != 1 << color_bits(g_max))
		goto weird_pixfmt;
	if (b_max + 1 != 1 << color_bits(b_max))
		goto weird_pixfmt;

	size = ggi_pf->size;
	idx = 0;
	pixfmt[idx] = '\0';

	if (!rgb_bits) {
		int pad, bits;
		bits = color_bits(c_max);
		pad = size - bits - c_shift;
		if (pad)
			idx += my_snprintf(&pixfmt[idx], count - idx,
				"p%d", pad);
		idx += my_snprintf(&pixfmt[idx], count - idx, "c%d", bits);
		size -= pad + bits;
		if (size)
			goto weird_pixfmt;
		return 0;
	}

	for (i = 0; i < 3; ++i) {
		int pad, bits;
		if (r_shift >= g_shift && r_shift >= b_shift) {
			color = 'r';
			shift = &r_shift;
			max = &r_max;
		}
		else if (g_shift >= b_shift) {
			color = 'g';
			shift = &g_shift;
			max = &g_max;
		}
		else {
			color = 'b';
			shift = &b_shift;
			max = &b_max;
		}

		bits = color_bits(*max);
		pad = size - bits - *shift;
		if (pad)
			idx += my_snprintf(&pixfmt[idx], count - idx,
				"p%d", pad);
		*shift = -1;
		idx += my_snprintf(&pixfmt[idx], count - idx,
			"%c%d", color, bits);
		size -= pad + bits;
	}
	if (size)
		idx += my_snprintf(&pixfmt[idx], count - idx, "p%d", size);
	return 0;

weird_pixfmt:
	snprintf(pixfmt, count, "weird");
	return -1;
}

/* Given a pixfmt string from the user, add missing padding bits
 * etc so that a string compare can be used to find compatible
 * pixel formats.
 */
int
canonicalize_pixfmt(char *pixfmt, int count)
{
	int c_max;
	int r_max, g_max, b_max;
	int r_shift, g_shift, b_shift;
	int size, depth;
	ggi_pixelformat ggi_pf;
	int res;

	res = parse_pixfmt(pixfmt, &c_max,
		&r_max, &g_max, &b_max,
		&r_shift, &g_shift, &b_shift,
		&size, &depth);
	if (res)
		return res;

	/* pad upper end to vnc compatible size */
	size = (size + 7) & ~7;
	if (size == 24)
		size = 32;

	memset(&ggi_pf, 0, sizeof(ggi_pf));
	ggi_pf.size = size;
	ggi_pf.depth = depth;
	ggi_pf.clut_mask = c_max;
	ggi_pf.red_mask = r_max << r_shift;
	ggi_pf.green_mask = g_max << g_shift;
	ggi_pf.blue_mask = b_max << b_shift;

	return generate_pixfmt(pixfmt, count, &ggi_pf);
}

void
remove_dead_data(struct buffer *buf)
{
	if (!buf->rpos)
		return;

	memmove(buf->data, buf->data + buf->rpos, buf->wpos - buf->rpos);

	buf->wpos -= buf->rpos;
	buf->rpos = 0;
}

int
buffer_reserve(struct buffer *buf, int size)
{
	uint8_t *tmp;

	if (buf->size >= size)
		return 0;

	if (buf->data)
        tmp = (uint8_t*)realloc(buf->data, size);
	else
        tmp = (uint8_t*)malloc(size);

	if (!tmp)
		return -1;

	buf->data = tmp;
	buf->size = size;
	return 0;
}

int
close_connection(struct connection *cx, int code)
{
	if (!cx->close_connection)
		cx->close_connection = code;
	return 0;
}

/* Set the title of the window, normally "<remote host> - ggivnc", but
 * simply "ggivnc" if the remote hostname is not yet known.
 */
int
set_title(struct connection *cx)
{
#ifdef HAVE_WMH
	const char *text = cx->name ? cx->name : cx->server;
	if (text && text[0]) {
		char *title = malloc(strlen(text) + 3 + 6 + 1);
		if (!title)
			return -1;
		strcpy(title, text);
		strcat(title, " - ggivnc");
		ggiWmhSetTitle(cx->stem, title);
		ggiWmhSetIconTitle(cx->stem, title);
		free(title);
	}
	else {
		ggiWmhSetTitle(cx->stem, "ggivnc");
		ggiWmhSetIconTitle(cx->stem, "ggivnc");
	}
#endif /* HAVE_WMH */
	return 0;
}

/* Tell libgii that we are interrested in read ready callbacks */
void
vnc_want_read(struct connection *cx)
{
	if (cx->fdselect) {
		struct gg_instance *fdselect = (struct gg_instance*)cx->fdselect;
		struct gii_fdselect_fd fd;
		fd.fd = cx->sfd;
		fd.mode = GII_FDSELECT_READ;
		ggControl(fdselect->channel, GII_FDSELECT_ADD, &fd);
	}
	cx->want_read = 1;
}

/* Tell libgii that we are interrested in write ready callbacks */
void
vnc_want_write(struct connection *cx)
{
	if (cx->fdselect) {
		struct gg_instance *fdselect = (struct gg_instance*)cx->fdselect;
		struct gii_fdselect_fd fd;
		fd.fd = cx->sfd;
		fd.mode = GII_FDSELECT_WRITE;
		ggControl(fdselect->channel, GII_FDSELECT_ADD, &fd);
	}
	cx->want_write = 1;
}

/* Tell libgii that we're not interrested in read ready callbacks */
void
vnc_stop_read(struct connection *cx)
{
	if (cx->fdselect) {
		struct gg_instance *fdselect = (struct gg_instance*)cx->fdselect;
		struct gii_fdselect_fd fd;
		fd.fd = cx->sfd;
		fd.mode = GII_FDSELECT_READ;
		ggControl(fdselect->channel, GII_FDSELECT_DEL, &fd);
	}
	cx->want_read = 0;
}

/* Tell libgii that we're not interrested in write ready callbacks */
void
vnc_stop_write(struct connection *cx)
{
	if (cx->fdselect) {
		struct gg_instance *fdselect = (struct gg_instance*)cx->fdselect;
		struct gii_fdselect_fd fd;
		fd.fd = cx->sfd;
		fd.mode = GII_FDSELECT_WRITE;
		ggControl(fdselect->channel, GII_FDSELECT_DEL, &fd);
	}
	cx->want_write = 0;
}

/* write(2) wrapper that:
 * 1. Retries interrupted calls.
 * 2. Retries with the remaining bits on partial writes.
 * 3. Registers a completion callback, when a write would block.
 * 4. Returns the number of bytes written, or negative on a
 *    "hard" error.
 */
static int
vnc_safe_write(struct connection *cx, const void *buf, int count)
{
	int res;
	int written = 0;

again:
	res = write(cx->sfd, buf, count);

	if (res == count)
		return res + written;

	if (res > 0) {
		count -= res;
		buf = (const uint8_t *)buf + res;
		written += res;
		goto again;
	}

	switch (errno) {
	case EINTR:
		goto again;
	case EAGAIN:
#ifdef EWOULDBLOCK
#if EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
#endif /* EWOULDBLOCK */
#ifdef WSAEWOULDBLOCK
#if EAGAIN != WSAEWOULDBLOCK
	case WSAEWOULDBLOCK:
#endif
#endif /* WSAEWOULDBLOCK */
		vnc_want_write(cx);
		return written;

	default:
		debug(1, "write error (%d, \"%s\").\n",
			errno, strerror(errno));
		return -1;
	}
}

/* If data is queued up already, just add the new data to the end of
 * the data to be written. If no data is previously queued up, try
 * to write the supplied buffer using the write routine from the
 * connection context. In case of a partial write, store the rest of
 * the buffer for later transmission.
 * Returns zero if data is transmitted or queued up, and negative if
 * the write routine reports a "hard" error (i.e. error codes
 * indicating interrupted calls are not "hard" errors).
 */
int
safe_write(struct connection *cx, const void *buf, int count)
{
	int res = cx->output.wpos ? 0 : cx->safe_write(cx, buf, count);

	if (res == count)
		return 0;

	if (res < 0)
		return -1;

	count -= res;
	buf = (const uint8_t *)buf + res;

	if (cx->output.wpos + count > cx->output.size) {
		if (buffer_reserve(&cx->output,
			cx->output.wpos + count + 1024))
		{
			return -1;
		}
	}
	memcpy(cx->output.data + cx->output.wpos, buf, count);
	cx->output.wpos += count;

	return 0;
}

/* Called when the socket is ready for reading */
static int
vnc_read_ready(struct connection *cx)
{
	ssize_t len;
	int request;

	debug(2, "read\n");

	if (cx->input.wpos == cx->input.size) {
		if (buffer_reserve(&cx->input, cx->input.size + 65536)) {
			close_connection(cx, -1);
			return 0;
		}
	}

	if (cx->max_read && cx->input.size - cx->input.rpos > cx->max_read) {
		request = cx->max_read + cx->input.rpos - cx->input.wpos;
		if (request <= 0) {
			debug(1, "don't read\n");
			vnc_stop_read(cx);
			return 0;
		}
	}
	else
		request = cx->input.size - cx->input.wpos;
	len = read(cx->sfd, cx->input.data + cx->input.wpos, request);

	if (len <= 0) {
		debug(1, "read error %d \"%s\"\n", errno, strerror(errno));
		close_connection(cx, -1);
		return 0;
	}

	debug(3, "len=%li\n", len);

	if (cx->bw.counting) {
		if (!cx->bw.count)
			bandwidth_start(cx, len);
		else
			bandwidth_update(cx, len);
	}

	cx->input.wpos += len;

	while (cx->action(cx));

	return 0;
}

/* Called when the socket is ready for writing */
static int
vnc_write_ready(struct connection *cx)
{
	int res;

	debug(2, "write rpos %d wpos %d\n", cx->output.rpos, cx->output.wpos);

	res = cx->safe_write(cx, cx->output.data, cx->output.wpos);

	if (res == cx->output.wpos) {
		vnc_stop_write(cx);

		cx->output.rpos = 0;
		cx->output.wpos = 0;

		return 0;
	}

	if (res >= 0) {
		cx->output.rpos += res;
		remove_dead_data(&cx->output);
		return 0;
	}

	/* error */
	close_connection(cx, -1);
	return 0;
}

static void
destroy_wire_stem(struct connection *cx)
{
	if (!cx->wire_stem)
		return;

	ggiClose(cx->wire_stem);
#ifdef HAVE_GGNEWSTEM
	ggDelStem(cx->wire_stem);
#endif
	cx->wire_stem = NULL;
	memcpy(&cx->wire_mode, &cx->mode, sizeof(ggi_mode));
}

static int
create_wire_stem(struct connection *cx)
{
	int c_max;
	int r_max, g_max, b_max;
	int r_shift, g_shift, b_shift;
	int size, depth;
	char *target;

	if (parse_pixfmt(cx->wire_pixfmt, &c_max,
		&r_max, &g_max, &b_max,
		&r_shift, &g_shift, &b_shift,
		&size, &depth))
	{
		return -1;
	}

	if (c_max)
		target = strdup("display-memory");
	else {
		const char display_memory[] =
			"display-memory:-pixfmt=";

		target = (char*)malloc(sizeof(display_memory)
			+ strlen(cx->wire_pixfmt) + 1);
		if (target) {
			strcpy(target, display_memory);
			strcat(target, cx->wire_pixfmt);
		}
	}
	if (!target)
		return -1;

#ifdef HAVE_GGNEWSTEM
	cx->wire_stem = ggNewStem(libggi, NULL);
	if (cx->wire_stem) {
		if (ggiOpen(cx->wire_stem, target, NULL) < 0) {
			ggDelStem(cx->wire_stem);
			cx->wire_stem = NULL;
		}
	}
#else
	cx->wire_stem = ggiOpen(target, NULL);
#endif

	free(target);
	if (!cx->wire_stem)
		return -1;

	memset(&cx->wire_mode, 0, sizeof(cx->wire_mode));
	cx->wire_mode.frames = 1;
	cx->wire_mode.visible.x = cx->width;
	cx->wire_mode.visible.y = cx->height;
	cx->wire_mode.virt.x = cx->width;
	cx->wire_mode.virt.y = cx->height;
	cx->wire_mode.size.x = cx->wire_mode.size.y = GGI_AUTO;
	GT_SETDEPTH(cx->wire_mode.graphtype, depth);
	GT_SETSIZE(cx->wire_mode.graphtype, size);
	if (c_max)
		GT_SETSCHEME(cx->wire_mode.graphtype, GT_PALETTE);
	else
		GT_SETSCHEME(cx->wire_mode.graphtype, GT_TRUECOLOR);
/*
	if (cx->wire_endian != cx->local_endian)
		GT_SETSUBSCHEME(cx->wire_mode.graphtype,
			GT_SUB_REVERSE_ENDIAN);
*/
	cx->wire_mode.dpp.x = cx->wire_mode.dpp.y = 1;

	if (ggiSetMode(cx->wire_stem, &cx->wire_mode) < 0) {
		destroy_wire_stem(cx);
		return -1;
	}

	return 0;
}

static int
delete_wire_stem(struct connection *cx)
{
	debug(1, "delete_wire_stem\n");

	ggiSetWriteFrame(cx->stem, 1 - ggiGetDisplayFrame(cx->stem));
	ggiSetReadFrame(cx->stem, 1 - ggiGetDisplayFrame(cx->stem));

	ggiCrossBlit(cx->wire_stem,
		0, 0,
		cx->width, cx->height,
		cx->stem,
		cx->offset.x, cx->offset.y);

	ggiSetWriteFrame(cx->stem, ggiGetDisplayFrame(cx->stem));

	ggiCopyBox(cx->stem,
		0, 0,
		cx->width, cx->height,
		0, 0);
	ggiFlush(cx->stem);

	ggiSetWriteFrame(cx->stem, 1 - ggiGetDisplayFrame(cx->stem));

	destroy_wire_stem(cx);

	if (cx->stem_change)
		return cx->stem_change(cx);

	return 0;
}

static int
add_wire_stem(struct connection *cx)
{
	debug(1, "add_wire_stem\n");

	if (create_wire_stem(cx))
		return -1;

	ggiCrossBlit(cx->stem,
		cx->offset.x, cx->offset.y,
		cx->width, cx->height,
		cx->wire_stem,
		0, 0);

	ggiSetWriteFrame(cx->stem, ggiGetDisplayFrame(cx->stem));
	ggiSetReadFrame(cx->stem, ggiGetDisplayFrame(cx->stem));

	if (cx->stem_change)
		return cx->stem_change(cx);

	return 0;
}

static int
need_wire_stem(struct connection *cx, const char *pixfmt)
{
	int c_max;
	int r_max, g_max, b_max;
	int r_shift, g_shift, b_shift;
	int size, depth;

	debug(2, "need_wire_stem\n");

	if (parse_pixfmt(pixfmt, &c_max,
		&r_max, &g_max, &b_max,
		&r_shift, &g_shift, &b_shift,
		&size, &depth))
	{
		return -1;
	}

	if (cx->mode.frames < 2)
		return 1;
	if (!strcmp(cx->local_pixfmt, "weird"))
		return 1;
	if (strcmp(pixfmt, cx->local_pixfmt))
		return 1;
	if (c_max) /* Assume that the palettes do not match */
		return 1;
	if (ggiGetPixelFormat(cx->stem)->flags & ~GGI_PF_REVERSE_ENDIAN)
		return 1;
	if (GT_SUBSCHEME(cx->mode.graphtype) & ~GT_SUB_REVERSE_ENDIAN)
		return 1;
	/*
	if (cx->wire_endian != cx->local_endian)
		return 1;
	*/
	return 0;
}

static int
vnc_set_pixel_format(struct connection *cx)
{
	int c_max;
	int r_max, g_max, b_max;
	int r_shift, g_shift, b_shift;
	int size, depth;
	uint8_t buf[20];
	int crossblit;

	debug(2, "set_pixel_format\n");

	if (parse_pixfmt(cx->wire_pixfmt, &c_max,
		&r_max, &g_max, &b_max,
		&r_shift, &g_shift, &b_shift,
		&size, &depth))
	{
		return -1;
	}

	size = (size + 7) & ~7;
	if (size == 24)
		size = 32;
	if (size > 32)
		return -1;

	buf[ 0] = 0;
	buf[ 1] = 0;
	buf[ 2] = 0;
	buf[ 3] = 0;
	buf[ 4] = size;
	buf[ 5] = depth;
	buf[ 6] = cx->wire_endian;
	buf[ 7] = c_max ? 0 : 1;
	insert16_hilo(&buf[ 8], r_max);
	insert16_hilo(&buf[10], g_max);
	insert16_hilo(&buf[12], b_max);
	buf[14] = r_shift;
	buf[15] = g_shift;
	buf[16] = b_shift;
	buf[17] = 0;
	buf[18] = 0;
	buf[19] = 0;

	memcpy(&cx->wire_mode, &cx->mode, sizeof(ggi_mode));
	destroy_wire_stem(cx);

	if (need_wire_stem(cx, cx->wire_pixfmt))
		crossblit = 1;
	else if (cx->scrollx || cx->scrolly) {
		cx->wire_stem_flags |= 1;
		crossblit = 1;
	}
	else
		crossblit = 0;

	if (crossblit) {
		if (create_wire_stem(cx))
			return -1;
	}

	if (safe_write(cx, buf, sizeof(buf))) {
		destroy_wire_stem(cx);
		return -1;
	}

	return 0;
}

int
vnc_set_encodings(struct connection *cx)
{
	int res;
	uint8_t *buf;
	uint16_t i;

	buf = (uint8_t*)malloc(4 + 4 * cx->encoding_count);
	if (!buf)
		return -1;

	buf[0] = 2;
	buf[1] = 0;
	insert16_hilo(&buf[2], cx->encoding_count);

	debug(1, "set_encodings\n");
	for (i = 0; i < cx->encoding_count; ++i) {
		insert32_hilo(&buf[4 + 4 * i], cx->encoding[i]);

		debug(1, "%d: %s\n",
			cx->encoding[i], lookup_encoding(cx->encoding[i]));
	}

	res = safe_write(cx, buf, 4 + 4 * cx->encoding_count);
	free(buf);

	return res;
}

int
vnc_update_request(struct connection *cx, int incremental)
{
	uint8_t buf[10] = { 3 };

	buf[1] = incremental;
	insert16_hilo(&buf[2], 0);
	insert16_hilo(&buf[4], 0);
	insert16_hilo(&buf[6], cx->width);
	insert16_hilo(&buf[8], cx->height);

	debug(2, "update_request (%dx%d %s)\n",
		cx->width, cx->height,
		incremental ? "incr" : "full");

	return safe_write(cx, buf, sizeof(buf));
}

static int
vnc_key(struct connection *cx, int down, uint32_t key)
{
	uint8_t buf[8] = { 4 };

	buf[1] = down;
	insert16_hilo(&buf[2], 0);
	insert32_hilo(&buf[4], key);

	if (cx->no_input)
		return 0;

	debug(2, "key %08x %s\n", key, down ? "down" : "up");

	return safe_write(cx, buf, sizeof(buf));
}

static int
vnc_pointer(struct connection *cx, int buttons, int x, int y)
{
	uint8_t buf[6] = { 5 };

	buf[1] = buttons;
	insert16_hilo(&buf[2], x);
	insert16_hilo(&buf[4], y);

	if (cx->no_input)
		return 0;

	debug(2, "pointer\n");

	return safe_write(cx, buf, sizeof(buf));
}

#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
static int
vnc_send_cut_text(struct connection *cx)
{
	size_t cut_len;
	uint8_t *buf;
	uint8_t *cut;
	const size_t max_len = 0x10000;
	int res;

	if (cx->no_input)
		return 0;

	/* Grab UTF-8 text from the WMH clipboard */
	if (ggiWmhClipboardOpen(cx->stem, GGIWMH_CLIPBOARD_GET) != GGI_OK)
		return 0;
	res = ggiWmhClipboardGet(cx->stem, GGIWMH_MIME_TEXT_UTF8,
		NULL, &cut_len);

	if (cut_len > max_len)
		cut_len = max_len;
	buf = malloc(cut_len + 1);
	cut = malloc(8 + cut_len);
	if (!buf || !cut) {
		ggiWmhClipboardClose(cx->stem);
		res = 0;
		goto out;
	}
	res = ggiWmhClipboardGet(cx->stem,
		GGIWMH_MIME_TEXT_UTF8, buf, &cut_len);
	if (res == GGI_OK || res == GGI_ENOSPACE)
		res = 0;
	if (cut_len > max_len)
		cut_len = max_len;
	ggiWmhClipboardClose(cx->stem);
	if (res) {
		res = 0;
		goto out;
	}

	if (!cut_len)
		goto out;

	buf[cut_len] = '\0';

	/* Convert the UTF-8 text to ISO-8859-1 */
	{
		iconv_t cd;
		ICONV_CONST char *in = (char *)buf;
		size_t inlen = cut_len;
		char *out = (char *)&cut[8];
		size_t outlen = cut_len;
		cd = iconv_open("ISO-8859-1", "UTF-8");
		if (!cd)
			goto out;
		iconv(cd, &in, &inlen, &out, &outlen);
		iconv_close(cd);
		cut_len -= outlen;
	}

	if (!cut_len)
		goto out;

	/* Transmit the ISO-8859-1 text to the remote end */
	insert32_lohi(&cut[0], 6);
	insert32_hilo(&cut[4], cut_len);

	res = safe_write(cx, cut, 8 + cut_len);

out:
	if (cut)
		free(cut);
	if (buf)
		free(buf);

	return res;
}
#endif /* GGIWMHFLAG_CLIPBOARD_CHANGE */

int
vnc_unexpected(struct connection *cx)
{
	debug(1, "unexpected protocol request\n");
	return close_connection(cx, -1);
}

static void
render_update(struct connection *cx)
{
	int d_frame, w_frame;

	d_frame = ggiGetDisplayFrame(cx->stem);
	w_frame = ggiGetWriteFrame(cx->stem);

	if (cx->wire_stem) {
		debug(2, "crossblit\n");
		ggiCrossBlit(cx->wire_stem,
			cx->slide.x, cx->slide.y,
			cx->area.x, cx->area.y,
			cx->stem,
			cx->offset.x, cx->offset.y);
	}
	if (cx->flush_hook)
		cx->flush_hook(cx->flush_hook_data);

	ggiSetDisplayFrame(cx->stem, w_frame);

	ggiFlush(cx->stem);

	// if (d_frame == w_frame)
	// 	return;

	// ggiSetWriteFrame(cx->stem, d_frame);
	// ggiCopyBox(cx->stem,
	// 	cx->offset.x, cx->offset.y,
	// 	cx->width, cx->height,
	// 	cx->offset.x, cx->offset.y);
	// ggiSetReadFrame(cx->stem, d_frame);

	// if (cx->post_flush_hook)
	// 	cx->post_flush_hook(cx->flush_hook_data);

	if (d_frame != w_frame)
        {
            ggiSetWriteFrame(cx->stem, d_frame);
            ggiCopyBox(cx->stem,
                    cx->offset.x, cx->offset.y,
                    cx->width, cx->height,
                    cx->offset.x, cx->offset.y);
            ggiSetReadFrame(cx->stem, d_frame);

            if (cx->post_flush_hook)
                    cx->post_flush_hook(cx->flush_hook_data);

        }

        // static int count = 0;
        // qDebug() << "update_frame" << ++count;
        int numbufs = ggiDBGetNumBuffers( cx->stem );
        const ggi_directbuffer *db;
        db = ggiDBGetBuffer( cx->stem, 0 );
        // if( !db->type & GGI_DB_SIMPLE_PLB )
        // {
        //     qDebug() << "We don't handle anything but simple pixel-linear buffer";
        // }

        // qDebug() << "[vnc.cpp] gBufferRenderedEvent";
        gBufferRenderedEvent();

}

static void
set_scrollbars(struct connection *cx)
{
	if (cx->slide.x + cx->area.x > cx->width)
		cx->slide.x = cx->width - cx->area.x;
	if (cx->slide.x < 0)
		cx->slide.x = 0;
	cx->sx = cx->slide.x;

	if (cx->slide.y + cx->area.y > cx->height)
		cx->slide.y = cx->height - cx->area.y;
	if (cx->slide.y < 0)
		cx->slide.y = 0;
	cx->sy = cx->slide.y;
}

static int
vnc_resize(struct connection *cx, ggi_mode *mode)
{
	int del_wire_stem = 0;

	mode->virt.x = mode->visible.x;
	mode->virt.y = mode->visible.y;

	if (mode->visible.x == cx->mode.visible.x &&
		mode->visible.y == cx->mode.visible.y)
	{
		return 0;
	}
	if ((mode->visible.x < cx->width || mode->visible.y < cx->height) &&
		cx->mode.visible.x >= cx->width &&
		cx->mode.visible.y >= cx->height)
	{
		if (!cx->wire_stem) {
			if (add_wire_stem(cx))
				return -1;
			cx->wire_stem_flags |= 1;
		}
	}
	else if ((cx->mode.visible.x < cx->width ||
		  cx->mode.visible.y < cx->height) &&
		mode->visible.x >= cx->width &&
		mode->visible.y >= cx->height)
	{
		if (cx->wire_stem && cx->wire_stem_flags) {
			cx->wire_stem_flags &= ~1;
			if (!cx->wire_stem_flags)
				del_wire_stem = 1;
		}
	}
	debug(2, "resize to %dx%d\n", mode->visible.x, mode->visible.y);

	scrollbar_destroy(cx);
	ggiCheckMode(cx->stem, mode);
	ggiSetMode(cx->stem, mode);
#ifdef HAVE_WMH
	ggiWmhAllowResize(cx->stem, 40, 40, cx->width, cx->height, 1, 1);
#endif
#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	cx->mode = *mode;
	cx->scrollx = 0;
	cx->scrolly = 0;
	cx->area.x = cx->width;
	cx->area.y = cx->height;
	scrollbar_area(cx);

	if (cx->mode.visible.x - cx->scrolly * SCROLL_SIZE > cx->width)
		cx->offset.x = (cx->mode.visible.x - cx->scrolly * SCROLL_SIZE -
			cx->width) / 2;
	else
		cx->offset.x = 0;
	if (cx->mode.visible.y - cx->scrollx * SCROLL_SIZE > cx->height)
		cx->offset.y = (cx->mode.visible.y - cx->scrollx * SCROLL_SIZE -
			cx->height) / 2;
	else
		cx->offset.y = 0;

#ifndef HAVE_WIDGETS
	if (cx->mode.visible.x < cx->width)
		cx->scrollx = 1;
	if (cx->mode.visible.y < cx->height)
		cx->scrolly = 1;
	cx->area = cx->mode.visible;
#endif

	set_scrollbars(cx);

	scrollbar_create(cx);

	if (del_wire_stem)
		return delete_wire_stem(cx);

	if (cx->wire_stem) {
		debug(3, "resize crossblit\n");
		render_update(cx);
	}

	return 0;
}

int
wire_mode_switch(struct connection *cx,
	const char *pixfmt, int endian, ggi_coord wire_size)
{
	ggi_cmddata_switchrequest swreq;
	ggi_mode current_mode;
	int current_width, current_height;
	int add_wire = 0;
	int del_wire = 0;
	int do_need_wire_stem;
	int did_need_wire_stem;

	if (cx->width == wire_size.x && cx->height == wire_size.y
		&& !strcmp(pixfmt, cx->wire_pixfmt)
		/*&& endian == cx->wire_endian*/)
	{
		debug(1, "same mode\n");
		return 0;
	}

	do_need_wire_stem = need_wire_stem(cx, pixfmt) ? 2 : 0;
	if (cx->mode.visible.x < wire_size.x ||
		cx->mode.visible.y < wire_size.y)
	{
		do_need_wire_stem |= 1;
	}

	did_need_wire_stem = cx->wire_stem_flags;
	if (!did_need_wire_stem)
		did_need_wire_stem |= cx->wire_stem ? 2 : 0;
	if (cx->mode.visible.x < cx->width || cx->mode.visible.y < cx->height)
		did_need_wire_stem |= 1;

	debug(1, "wire_mode_switch %dx%d, old %s %dx%d %d, new %s %dx%d %d\n",
		cx->mode.visible.x, cx->mode.visible.y,
		cx->wire_pixfmt, cx->width, cx->height, did_need_wire_stem,
		pixfmt, wire_size.x, wire_size.y, do_need_wire_stem);

	render_update(cx);

	if (!(did_need_wire_stem & do_need_wire_stem & 2)) {
		if (did_need_wire_stem != do_need_wire_stem) {
			del_wire = did_need_wire_stem;
			add_wire = do_need_wire_stem;
		}
	}
	else if (strcmp(pixfmt, cx->wire_pixfmt))
		del_wire = add_wire = 2;

	if (del_wire) {
		cx->wire_stem_flags = 0;
		if (delete_wire_stem(cx))
			return -1;
	}
	if (add_wire)
		cx->wire_stem_flags =
			(do_need_wire_stem & 2) ? 0 : (add_wire & 1);

	scrollbar_destroy(cx);
	cx->scrollx = 0;
	cx->scrolly = 0;
	cx->area = wire_size;
	cx->width = cx->area.x;
	cx->height = cx->area.y;
	scrollbar_area(cx);

	if (cx->mode.visible.x - cx->scrolly * SCROLL_SIZE > cx->width) {
		ggi_color black = { 0, 0, 0, 0 };
		int d = cx->mode.visible.x
			- cx->scrolly * SCROLL_SIZE - cx->width;
		cx->offset.x = d / 2;
		ggiSetGCForeground(cx->stem, ggiMapColor(cx->stem, &black));
		ggiDrawBox(cx->stem, 0, 0, cx->offset.x, cx->mode.visible.y);
		ggiDrawBox(cx->stem,
			cx->mode.visible.x - (d + 1) / 2, 0,
			cx->mode.visible.x, cx->mode.visible.y);
		if (cx->mode.frames >= 2) {
			int w_frame = ggiGetWriteFrame(cx->stem);
			ggiSetWriteFrame(cx->stem, 1 - w_frame);
			ggiDrawBox(cx->stem,
				0, 0, cx->offset.x, cx->mode.visible.y);
			ggiDrawBox(cx->stem,
				cx->mode.visible.x - (d + 1) / 2, 0,
				cx->mode.visible.x, cx->mode.visible.y);
			ggiSetWriteFrame(cx->stem, w_frame);
		}
	}
	else
		cx->offset.x = 0;
	if (cx->mode.visible.y - cx->scrollx * SCROLL_SIZE > cx->height) {
		ggi_color black = { 0, 0, 0, 0 };
		int d = cx->mode.visible.y
			- cx->scrollx * SCROLL_SIZE - cx->height;
		cx->offset.y = d / 2;
		ggiSetGCForeground(cx->stem, ggiMapColor(cx->stem, &black));
		ggiDrawBox(cx->stem, 0, 0, cx->mode.visible.x, cx->offset.y);
		ggiDrawBox(cx->stem,
			0, cx->mode.visible.y - (d + 1) / 2,
			cx->mode.visible.x, cx->mode.visible.y);
		if (cx->mode.frames >= 2) {
			int w_frame = ggiGetWriteFrame(cx->stem);
			ggiSetWriteFrame(cx->stem, 1 - w_frame);
			ggiDrawBox(cx->stem,
				0, 0, cx->mode.visible.x, cx->offset.y);
			ggiDrawBox(cx->stem,
				0, cx->mode.visible.y - (d + 1) / 2,
				cx->mode.visible.x, cx->mode.visible.y);
			ggiSetWriteFrame(cx->stem, w_frame);
		}
	}
	else
		cx->offset.y = 0;

#ifndef HAVE_WIDGETS
	if (cx->mode.visible.x < cx->width)
		cx->scrollx = 1;
	if (cx->mode.visible.y < cx->height)
		cx->scrolly = 1;
	cx->area = cx->mode.visible;
#endif

	set_scrollbars(cx);

	strcpy(cx->wire_pixfmt, pixfmt);

	if (cx->wire_stem) {
		cx->wire_mode.visible.x = cx->width;
		cx->wire_mode.visible.y = cx->height;
		cx->wire_mode.virt.x = cx->width;
		cx->wire_mode.virt.y = cx->height;
		cx->wire_mode.size.x = cx->wire_mode.size.y = GGI_AUTO;

		debug(1, "resize wire stem\n");
		if (ggiSetMode(cx->wire_stem, &cx->wire_mode) < 0) {
			debug(1, "Unable to change wire mode\n");
			return -1;
		}

		if (cx->stem_change) {
			if (cx->stem_change(cx))
				return -1;
		}
	}
	else if (add_wire) {
		if (add_wire_stem(cx))
			return -1;
	}

	scrollbar_create(cx);

	if (cx->flush_hook)
		cx->flush_hook(cx->flush_hook_data);

	/* Change mode by injecting a "fake" mode switch request. */
	current_mode = cx->mode;
	current_width = cx->width;
	current_height = cx->height;

	cx->width = wire_size.x;
	cx->height = wire_size.y;
	select_mode(cx);

	swreq.request = GGI_REQSW_MODE;
	swreq.mode = cx->mode;

	cx->mode = current_mode;
	cx->width = current_width;
	cx->height = current_height;

	ggiCheckMode(cx->stem, &swreq.mode);

	if (swreq.mode.frames != cx->mode.frames)
		return 1;
	if (swreq.mode.graphtype != cx->mode.graphtype)
		return 1;

#ifdef HAVE_INPUT_FDSELECT
	{
		gii_event ev;
		ev.any.target = GII_EV_TARGET_QUEUE;
		ev.any.size = sizeof(gii_cmd_nodata_event) +
			sizeof(ggi_cmddata_switchrequest);
		ev.any.type = evCommand;
		ev.cmd.code = GGICMD_REQUEST_SWITCH;

		memcpy(ev.cmd.data, &swreq, sizeof(swreq));

		giiEventSend(cx->stem, &ev);
	}
	return 0;
#else
	return vnc_resize(cx, &swreq.mode);
#endif
}

int
vnc_update_rect(struct connection *cx)
{
	uint32_t encoding;

	debug(2, "update_rect\n");

	if (!cx->rects) {
		if (cx->bw.count) {
			if (bandwidth_end(cx))
				return close_connection(cx, -1);
		}
		cx->bw.counting = 0;

		if (vnc_update_request(cx, !cx->desktop_size))
			return close_connection(cx, -1);
		cx->desktop_size = 0;
		render_update(cx);
		remove_dead_data(&cx->input);
		cx->action = vnc_wait;
		return 1;
	}

	if (cx->input.wpos < cx->input.rpos + 12)
		return 0;

	cx->x = get16_hilo(&cx->input.data[cx->input.rpos + 0]);
	cx->y = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
	cx->w = get16_hilo(&cx->input.data[cx->input.rpos + 4]);
	cx->h = get16_hilo(&cx->input.data[cx->input.rpos + 6]);

	if (!cx->wire_stem) {
		cx->x += cx->offset.x;
		cx->y += cx->offset.y;
	}

	encoding = get32_hilo(&cx->input.data[cx->input.rpos + 8]);

	cx->input.rpos += 12;

	debug(2, "encoding %d, x=%d y=%d w=%d h=%d\n",
		encoding, cx->x, cx->y, cx->w, cx->h);

	switch (encoding) {
	case 0:
		cx->action = vnc_raw;
		break;

	case 1:
		cx->action = vnc_copyrect;
		break;

	case 2:
		cx->action = cx->encoding_def[rre_encoding].action;
		break;

	case 4:
		cx->action = cx->encoding_def[corre_encoding].action;
		break;

	case 5:
		cx->action = cx->encoding_def[hextile_encoding].action;
		break;

	case 6:
		cx->action = cx->encoding_def[zlib_encoding].action;
		break;

	case 7:
		cx->action = cx->encoding_def[tight_encoding].action;
		break;

	case 8:
		cx->action = cx->encoding_def[zlibhex_encoding].action;
		break;

	case 15:
		cx->action = cx->encoding_def[trle_encoding].action;
		break;

	case 16:
		cx->action = cx->encoding_def[zrle_encoding].action;
		break;

	case -223:
		cx->action = vnc_desktop_size;
		break;

	case -224:
		cx->action = vnc_lastrect;
		break;

	case -307:
		cx->action = vnc_desktop_name;
		break;

	case 0x574d5669:
		cx->action = vnc_wmvi;
		break;

	default:
		cx->action = vnc_unexpected;
	}

	return 1;
}

static int
vnc_update(struct connection *cx)
{
	debug(2, "update\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	cx->rects = get16_hilo(&cx->input.data[cx->input.rpos + 2]);

	debug(2, "rects=%d\n", cx->rects);

	cx->input.rpos += 4;

	cx->bw.counting = cx->auto_encoding;
	cx->bw.count = 0;

	cx->action = vnc_update_rect;
	return 1;
}

static int
vnc_palette(struct connection *cx)
{
	int i;
	int first;
	int count;
	ggi_color clut[256];

	debug(2, "palette\n");

	if (cx->input.wpos < cx->input.rpos + 6)
		return 0;

	first = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
	count = get16_hilo(&cx->input.data[cx->input.rpos + 4]);

	if (cx->input.wpos < cx->input.rpos + 6 + 6 * count)
		return 0;

	cx->input.rpos += 6;
	for (i = 0; i < count; ++i) {
		clut[i].r = get16_hilo(&cx->input.data[cx->input.rpos + 0]);
		clut[i].g = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
		clut[i].b = get16_hilo(&cx->input.data[cx->input.rpos + 4]);
		clut[i].a = 0;
		cx->input.rpos += 6;
	}
	ggiSetPalette(cx->wire_stem, first, count, clut);

	debug(3, "palette crossblit\n");
	render_update(cx);
	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
vnc_bell(struct connection *cx)
{
	debug(2, "bell\n");

	++cx->input.rpos;

	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}


static int
drain_cut_text(struct connection *cx)
{
	const int max_chunk = 0x100000;
	uint32_t rest;
	uint32_t limit;
	int chunk;

	debug(2, "drain-cut-text\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	rest = get32_hilo(&cx->input.data[cx->input.rpos]);

	limit = max_chunk;
	if (cx->input.wpos - (cx->input.rpos + 4) < max_chunk)
		limit = cx->input.wpos - (cx->input.rpos + 4);

	chunk = rest < limit ? rest : limit;

	if (rest > limit) {
		cx->input.rpos += chunk;
		remove_dead_data(&cx->input);
		rest -= limit;
		insert32_hilo(cx->input.data, rest);
		return chunk > 0;
	}

	cx->input.rpos += 4 + chunk;

	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

static int
vnc_cut_text(struct connection *cx)
{
	const uint32_t max_len = 0x10000;
	uint32_t length;
	int len;

	debug(2, "cut_text\n");

	if (cx->input.wpos < cx->input.rpos + 8)
		return 0;

	length = get32_hilo(&cx->input.data[cx->input.rpos + 4]);
	len = length < max_len ? length : max_len;

	if (cx->input.wpos < cx->input.rpos + 8 + len)
		return 0;

#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	if (ggiWmhClipboardOpen(cx->stem, GGIWMH_CLIPBOARD_ADD) == GGI_OK) {
		uint8_t *orig = &cx->input.data[cx->input.rpos + 8];
		uint8_t *data = malloc(2 * len + 1);
		int data_len;
		if (!data)
			goto out;
		{
			iconv_t cd;
			ICONV_CONST char *in = (char *)orig;
			size_t inlen = len;
			char *out = (char *)data;
			size_t outlen = 2 * len + 1;
			cd = iconv_open("UTF-8", "ISO-8859-1");
			if (!cd)
				goto out;
			iconv(cd, &in, &inlen, &out, &outlen);
			iconv_close(cd);
			data_len = 2 * len + 1 - outlen;
		}
		data[data_len++] = 0;
		debug(1, "client_cut_text\n");
		ggiWmhClipboardAdd(cx->stem, GGIWMH_MIME_TEXT_UTF8,
			data, data_len);
		memcpy(data, orig, len);
		data[len] = 0;
		ggiWmhClipboardAdd(cx->stem, GGIWMH_MIME_TEXT_ISO_8859_1,
			data, len + 1);
out:
		if (data)
			free(data);
		ggiWmhClipboardClose(cx->stem);
	}
	else
		debug(1, "unable to open clipboard\n");
#endif /* GGIWMHFLAG_CLIPBOARD_CHANGE */

	if (length > max_len) {
		cx->input.rpos += 4 + len;
		remove_dead_data(&cx->input);
		length -= max_len;
		insert32_hilo(cx->input.data, length);
		cx->action = drain_cut_text;
		return 1;
	}

	cx->input.rpos += 8 + len;

	remove_dead_data(&cx->input);
	cx->action = vnc_wait;
	return 1;
}

int
vnc_wait(struct connection *cx)
{
	debug(2, "wait\n");

	if (cx->input.wpos < cx->input.rpos + 1)
		return 0;

	switch (cx->input.data[cx->input.rpos]) {
	case 0:
		cx->action = vnc_update;
		break;

	case 1:
		cx->action = vnc_palette;
		break;

	case 2:
		cx->action = vnc_bell;
		break;

	case 3:
		cx->action = vnc_cut_text;
		break;

	case 250:
		cx->action = cx->encoding_def[xvp_encoding].action;
		break;

	case 252:
		if (cx->input.wpos < cx->input.rpos + 4)
			return 0;

		switch (get32_hilo(&cx->input.data[cx->input.rpos])) {
		case 0xfc000101: /* file compression reply */
		case 0xfc000103: /* file list reply */
		case 0xfc000105: /* file md5 reply */
		case 0xfc000107: /* file upload start reply */
		case 0xfc000109: /* file upload data reply */
		case 0xfc00010b: /* file upload end reply */
		case 0xfc00010d: /* file download start reply */
		case 0xfc00010f: /* file download data reply */
		case 0xfc000110: /* file download data end */
		case 0xfc000112: /* file mkdir reply */
		case 0xfc000114: /* file rm reply */
		case 0xfc000116: /* file mv reply */
		case 0xfc000118: /* file directory size reply */
		case 0xfc000119: /* file last request failed */
			cx->action = cx->encoding_def[tight_file].action;
			break;
		}
		break;

	case 253:
		cx->action = cx->encoding_def[gii_encoding].action;
		break;

	case 130: /* file list data */
	case 131: /* file download data */
	case 132: /* file upload cancel */
	case 133: /* file download failed */
	case 135: /* file last request failed */
		cx->action = cx->encoding_def[tight_file].action;
		break;

	default:
		debug(1, "got cmd code %d\n", cx->input.data[cx->input.rpos]);
		cx->action = vnc_unexpected;
	}

	return 1;
}

static int
vnc_fdselect(void *arg, uint32_t flag, void *data)
{
	struct connection *cx = (struct connection*)arg;
	struct gii_fdselect_fd *fd = (struct gii_fdselect_fd*)data;

	debug(2, "fdselect\n");

	if (flag != GII_FDSELECT_READY)
		return GGI_OK;

	if (fd->fd != cx->sfd)
		return GGI_OK;

	if (fd->mode & GII_FDSELECT_WRITE) {
		cx->write_ready(cx);

		if (cx->write_drained && !cx->output.wpos)
			cx->write_drained(cx);
	}

	if (fd->mode & GII_FDSELECT_READ)
		cx->read_ready(cx);

	return GGI_OK;
}

static int
xform_gii_key(struct connection *cx, int down, uint32_t key)
{
	if (cx->no_input)
		return 0;

	switch (key) {
	case GIIUC_BackSpace: key = 0xff08;                            break;
	case GIIUC_Tab:       key = 0xff09;                            break;
	case GIIUC_Return:    key = 0xff0d;                            break;
	case GIIUC_Escape:    key = 0xff1b;                            break;
	case GIIK_Insert:     key = 0xff63;                            break;
	case GIIK_Menu:       key = 0xff67;                            break;
	case GIIUC_Delete:    key = 0xffff;                            break;
	case GIIK_Home:       key = 0xff50;                            break;
	case GIIK_End:        key = 0xff57;                            break;
	case GIIK_PageUp:     key = 0xff55;                            break;
	case GIIK_PageDown:   key = 0xff56;                            break;
	case GIIK_Left:       key = 0xff51;                            break;
	case GIIK_Up:         key = 0xff52;                            break;
	case GIIK_Right:      key = 0xff53;                            break;
	case GIIK_Down:       key = 0xff54;                            break;
	case GIIK_F1:         key = 0xffbe;                            break;
	case GIIK_F2:         key = 0xffbf;                            break;
	case GIIK_F3:         key = 0xffc0;                            break;
	case GIIK_F4:         key = 0xffc1;                            break;
	case GIIK_F5:         key = 0xffc2;                            break;
	case GIIK_F6:         key = 0xffc3;                            break;
	case GIIK_F7:         key = 0xffc4;                            break;
	case GIIK_F8:         key = 0xffc5;                            break;
	case GIIK_F9:         key = 0xffc6;                            break;
	case GIIK_F10:        key = 0xffc7;                            break;
	case GIIK_F11:        key = 0xffc8;                            break;
	case GIIK_F12:        key = 0xffc9;                            break;
	case GIIK_ShiftL:     key = 0xffe1;                            break;
	case GIIK_ShiftR:     key = 0xffe2;                            break;
	case GIIK_CtrlL:      key = 0xffe3;                            break;
	case GIIK_CtrlR:      key = 0xffe4;                            break;
	case GIIK_MetaL:      key = 0xffe7;                            break;
	case GIIK_MetaR:      key = 0xffe8;                            break;
	case GIIK_AltL:       key = 0xffe9;                            break;
	case GIIK_AltR:       key = 0xffea;                            break;
	}

	if (key && key < GIIUC_Escape)
		key |= 0x60;

	if (vnc_key(cx, down, key)) {
		close_connection(cx, -1);
		return -1;
	}

	return 0;
}

#ifndef HAVE_WIDGETS
static void
auto_scroll(struct connection *cx, uint8_t buttons, int x, int y)
{
	int move = 0;

	if (!cx->scrollx && !cx->scrolly)
		return;

	if (x < cx->offset.x + 3 && cx->slide.x) {
		move = 1;
		if (cx->slide.x > 8)
			cx->slide.x -= 8;
		else
			cx->slide.x = 0;
	}
	if (x >= cx->offset.x + cx->area.x - 3 &&
		cx->slide.x != cx->width - cx->area.x)
	{
		move = 1;
		if (cx->slide.x < cx->width - cx->area.x - 8)
			cx->slide.x += 8;
		else
			cx->slide.x = cx->width - cx->area.x;
	}
	if (y < cx->offset.y + 3 && cx->slide.y) {
		move = 1;
		if (cx->slide.y > 8)
			cx->slide.y -= 8;
		else
			cx->slide.y = 0;
	}
	if (y >= cx->offset.y + cx->area.y - 3 &&
		cx->slide.y != cx->height - cx->area.y)
	{
		move = 1;
		if (cx->slide.y < cx->height - cx->area.y - 8)
			cx->slide.y += 8;
		else
			cx->slide.y = cx->height - cx->area.y;
	}

	if (!move)
		return;

	debug(3, "auto_scroll crossblit\n");
	render_update(cx);
	if (vnc_pointer(cx, buttons, x + cx->slide.x, y + cx->slide.y))
		close_connection(cx, -1);
}
#endif /* HAVE_WIDGETS */

static int
loop(struct connection *cx)
{
	static uint8_t buttons;
	static int x, y, wheel;
	int done = 0;
	int n;
	int res;
	gii_event event;
	gii_event req_event;
	ggi_cmddata_switchrequest swreq;

#ifdef HAVE_WMH
	ggiWmhAllowResize(cx->stem, 40, 40, cx->width, cx->height, 1, 1);
#endif

again:
	req_event.any.size = 0;

	giiEventPoll(cx->stem, emAll, NULL);
	n = giiEventsQueued(cx->stem, emAll);

	while (n-- && !cx->close_connection) {
		giiEventRead(cx->stem, &event, emAll);

		if (scrollbar_process(cx, &event))
			continue;

		switch(event.any.type) {
		case evKeyPress:
		case evKeyRepeat:
			if (event.key.sym == GIIK_F8 &&
				!(event.key.modifiers & GII_KM_MASK))
			{
#ifdef HAVE_WMH
				ggiWmhAllowResize(cx->stem,
					cx->mode.visible.x,
					cx->mode.visible.y,
					cx->mode.visible.x,
					cx->mode.visible.y,
					1, 1);
#endif
				cx->F8_allow_release = 0;
				switch (show_menu(cx)) {
				case 1:
					xform_gii_key(cx, 1, GIIK_F8);
					xform_gii_key(cx, 0, GIIK_F8);
					break;
				case -2:
				case 2:
					done = 1;
					break;
				case 3:
					if (show_about(cx) == -1)
						done = 1;
					break;
				case 4:
					xform_gii_key(cx, 1, GIIK_CtrlL);
					xform_gii_key(cx, 1, GIIK_AltL);
					xform_gii_key(cx, 1, GIIUC_Delete);
					xform_gii_key(cx, 0, GIIUC_Delete);
					xform_gii_key(cx, 0, GIIK_AltL);
					xform_gii_key(cx, 0, GIIK_CtrlL);
					break;
				case 5:
#ifdef GGIWMHFLAG_GRAB_HOTKEYS
					{
					uint32_t flags =
						ggiWmhGetFlags(cx->stem)
						^ GGIWMHFLAG_GRAB_HOTKEYS;
					ggiWmhSetFlags(cx->stem, flags);
					if (flags != ggiWmhGetFlags(cx->stem))
						debug(1, "No grab\n");
					}
#endif
					break;
				case 6:
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
					if (vnc_send_cut_text(cx))
						close_connection(cx, -1);
#endif
					break;
				case 7:
					if (show_file_transfer(cx) == -1)
						done = 1;
					break;
				case 8:
					switch (show_xvp_menu(cx)) {
					case -2:
						done = 1;
						break;
					case 1:
						xvp_shutdown(cx);
						break;
					case 2:
						xvp_reboot(cx);
						break;
					case 3:
						xvp_reset(cx);
						break;
					}
					break;
				}
#ifdef HAVE_WMH
				ggiWmhAllowResize(cx->stem, 40, 40,
					cx->width, cx->height, 1, 1);
#endif
				break;
			}
			if (event.key.sym == GIIK_F8)
				cx->F8_allow_release = 1;
			if (gii_inject(cx, &event))
				break;
			xform_gii_key(cx, 1, event.key.sym);
			break;

		case evKeyRelease:
			if (event.key.sym == GIIK_F8 && !cx->F8_allow_release)
				break;
			if (gii_inject(cx, &event))
				break;
			xform_gii_key(cx, 0, event.key.sym);
			break;

		case evPtrButtonPress:
			if (gii_inject(cx, &event))
				break;
			switch (event.pbutton.button) {
			case GII_PBUTTON_LEFT:   buttons |= 1; break;
			case GII_PBUTTON_MIDDLE: buttons |= 2; break;
			case GII_PBUTTON_RIGHT:  buttons |= 4; break;
			}
			if (vnc_pointer(cx, buttons,
				x + cx->slide.x, y + cx->slide.y))
			{
				close_connection(cx, -1);
			}
			break;

		case evPtrButtonRelease:
			if (gii_inject(cx, &event))
				break;
			switch (event.pbutton.button) {
			case GII_PBUTTON_LEFT:   buttons &= ~1; break;
			case GII_PBUTTON_MIDDLE: buttons &= ~2; break;
			case GII_PBUTTON_RIGHT:  buttons &= ~4; break;
			}
			if (vnc_pointer(cx, buttons,
				x + cx->slide.x, y + cx->slide.y))
			{
				close_connection(cx, -1);
			}
			break;

		case evPtrAbsolute:
			if (gii_inject(cx, &event))
				break;
			if (event.pmove.x < cx->offset.x)
				break;
			if (event.pmove.x >= cx->offset.x + cx->area.x)
				break;
			if (event.pmove.y < cx->offset.y)
				break;
			if (event.pmove.y >= cx->offset.y + cx->area.y)
				break;
			x = event.pmove.x - cx->offset.x;
			y = event.pmove.y - cx->offset.y;
			if (event.pmove.wheel != wheel) {
				if (event.pmove.wheel > wheel)
					res = vnc_pointer(cx, buttons | 8,
						x + cx->slide.x,
						y + cx->slide.y);
				else
					res = vnc_pointer(cx, buttons | 16,
						x + cx->slide.x,
						y + cx->slide.y);
				if (res)
					close_connection(cx, -1);
				wheel = event.pmove.wheel;
			}
			if (vnc_pointer(cx, buttons,
				x + cx->slide.x, y + cx->slide.y))
			{
				close_connection(cx, -1);
			}
			break;

		case evPtrRelative:
			if (gii_inject(cx, &event))
				break;
			x += event.pmove.x;
			y += event.pmove.y;
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			if (x >= cx->area.x) x = cx->area.x - 1;
			if (y >= cx->area.y) y = cx->area.y - 1;
			if (event.pmove.wheel) {
				if (event.pmove.wheel > 0)
					res = vnc_pointer(cx, buttons | 8,
						x + cx->slide.x,
						y + cx->slide.y);
				else
					res = vnc_pointer(cx, buttons | 16,
						x + cx->slide.x,
						y + cx->slide.y);
				if (res)
					close_connection(cx, -1);
			}
			if (vnc_pointer(cx, buttons,
				x + cx->slide.x, y + cx->slide.y))
			{
				close_connection(cx, -1);
			}
			break;

		case evValAbsolute:
		case evValRelative:
			if (gii_inject(cx, &event))
				break;
			break;

		case evCommand:
			switch (event.cmd.code) {
			case UPLOAD_FILE_FRAGMENT_CMD:
				switch (event.cmd.origin) {
				case GII_EV_ORIGIN_SENDEVENT:
					file_upload_fragment(cx);
					break;
				}
				break;
			case GGICMD_REQUEST_SWITCH:
				memcpy(&swreq, event.cmd.data, sizeof(swreq));
				if (swreq.request == GGI_REQSW_MODE)
					req_event = event;
				break;
#ifdef HAVE_GGNEWSTEM
			case GII_CMDCODE_DEVICE_INFO:
				gii_create_device(cx, event.cmd.origin,
					(struct gii_cmddata_devinfo *)
					event.cmd.data);
				break;
			case GII_CMDCODE_DEVICE_CLOSE:
				gii_delete_device(cx, &event);
				break;
			case GII_CMDCODE_EVENTLOST:
			case GII_CMDCODE_DEVICE_ENABLE:
			case GII_CMDCODE_DEVICE_DISABLE:
				gii_inject(cx, &event);
				break;
#endif /* HAVE_GGNEWSTEM */
			}
			break;

#ifdef GGIWMHFLAG_CATCH_CLOSE
		case evFromAPI:
			if (event.fromapi.api_id == libggiwmh->id) {
				switch (event.fromapi.code) {
				case GII_SLI_CODE_WMH_CLOSEREQUEST:
					debug(1, "quiting\n");
					done = 1;
					break;
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
				case GII_SLI_CODE_WMH_CLIPBOARD_CHANGE:
					debug(1, "server_cut_text\n");
					if (vnc_send_cut_text(cx))
						close_connection(cx, -1);
					break;
#endif /* GGIWMHFLAG_CLIPBOARD_CHANGE */
				}
			}
			break;
#endif /* GGIWMHFLAG_CATCH_CLOSE */
		}
#ifndef HAVE_WIDGETS
		auto_scroll(cx, buttons, x, y);
#endif
	}

	if (cx->close_connection)
		return cx->close_connection;
	if (done)
		return 0;

	if (req_event.any.size) {
		memcpy(&swreq, req_event.cmd.data, sizeof(swreq));
		if (vnc_resize(cx, &swreq.mode)) {
			close_connection(cx, -1);
			return cx->close_connection;
		}
	}

	goto again;
}

void
select_mode(struct connection *cx)
{
	const char *str = getenv("GGI_DEFMODE");

	if (!str) {
		cx->mode.frames = 2;
		cx->mode.visible.x = cx->width;
		cx->mode.visible.y = cx->height;
		cx->mode.virt.x = GGI_AUTO;
		cx->mode.virt.y = GGI_AUTO;
		cx->mode.size.x = GGI_AUTO;
		cx->mode.size.y = GGI_AUTO;
		cx->mode.graphtype = GT_AUTO;
		cx->mode.dpp.x = GGI_AUTO;
		cx->mode.dpp.y = GGI_AUTO;
		return;
	}

	ggiParseMode(str, &cx->mode);

	if (cx->mode.visible.x == GGI_AUTO) {
		cx->mode.visible.x = cx->width;
		if (cx->mode.virt.x != GGI_AUTO) {
			if (cx->mode.visible.x > cx->mode.virt.x)
				cx->mode.visible.x = cx->mode.virt.x;
		}
	}
	if (cx->mode.visible.y == GGI_AUTO) {
		cx->mode.visible.y = cx->height;
		if (cx->mode.virt.y != GGI_AUTO) {
			if (cx->mode.visible.y > cx->mode.virt.y)
				cx->mode.visible.y = cx->mode.virt.y;
		}
	}

	if (cx->mode.frames == GGI_AUTO)
		cx->mode.frames = 2;
}

static int
vnc_connect(struct connection *cx)
{
	struct addrinfo *ai, *gai;
	struct addrinfo hints;
	int res;
	char port[20];

	memset(&hints, '\0', sizeof(hints));
#ifdef AI_ADDRCONFIG
	hints.ai_flags = AI_ADDRCONFIG;
#endif
	hints.ai_socktype = SOCK_STREAM;
	switch (cx->net_family) {
	case 4:
		debug(1, "Using IPv4 only\n");
		hints.ai_family = PF_INET;
		break;
#ifdef PF_INET6
	case 6:
		debug(1, "Using IPv6 only\n");
		hints.ai_family = PF_INET6;
		break;
#endif
	}

	snprintf(port, sizeof(port), "%d", cx->port);

	res = getaddrinfo(cx->server, port, &hints, &gai);
	if (res) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
		return res;
	}

	ai = gai;
	for (ai = gai; ai; ai = ai->ai_next) {
		res = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (res == -1) {
			debug(1, "socket\n");
			continue;
		}
		cx->sfd = res;
		res = connect(cx->sfd, ai->ai_addr, ai->ai_addrlen);
		if (!res)
			break;
		close(cx->sfd);
		cx->sfd = -1;
		debug(1, "connect\n");
	}

	if (!ai)
		fprintf(stderr, "cannot reach %s\n", cx->server);

	freeaddrinfo(gai);
	return res;
}

#define MAXSOCK	3

static int
vnc_listen(struct connection *cx)
{
	struct addrinfo *ai, *gai;
	struct addrinfo hints;
	int s[MAXSOCK];
	int nsock;
	int res, fd;
	char str_port[21];
	struct sockaddr_storage sa;
#ifdef HAVE_SOCKLEN_T
	socklen_t sa_len = sizeof(sa);
#else
	int sa_len = sizeof(sa);
#endif

	sprintf(str_port, "%d", cx->listen);

	memset(&hints, 0, sizeof(hints));
	switch (cx->net_family) {
	case 4:
		debug(1, "Using IPv4 only\n");
		hints.ai_family = PF_INET;
		break;
#ifdef PF_INET6
	case 6:
		debug(1, "Using IPv6 only\n");
		hints.ai_family = PF_INET6;
		break;
#endif
	default:
		hints.ai_family = PF_UNSPEC;
	}
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
#ifdef AI_ADDRCONFIG
	hints.ai_flags |= AI_ADDRCONFIG;
#endif

	fd = -1;
	res = getaddrinfo(cx->bind, str_port, &hints, &gai);
	if (res) {
		debug(1, "getaddrinfo: %s\n", gai_strerror(res));
		return res;
	}

	nsock = 0;
	for (ai = gai; ai && nsock < MAXSOCK; ai = ai->ai_next) {
		s[nsock] = fd = socket(ai->ai_family, ai->ai_socktype,
					ai->ai_protocol);
		if (fd == -1) {
			debug(1, "socket failed\n");
			continue;
		}

		if (bind(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
			debug(1, "bind failed\n");
			close(fd);
			s[nsock] = fd = -1;
			continue;
		}

		if (listen(fd, 3) < 0) {
			debug(1, "listen failed\n");
			close(fd);
			s[nsock] = fd = -1;
			continue;
		}

		nsock++;

		/* ggivnc can deal with only one fd.
		 * So we take the first.
		 */
		break;
	}

	if (!nsock) {
		debug(1, "No socket available\n");
		return -1;
	}

	freeaddrinfo(gai);

	do {
		cx->sfd = accept(fd, (struct sockaddr *)&sa, &sa_len);
	} while (cx->sfd == -1 && errno == EINTR);

	close(fd);
	return cx->sfd == -1 ? -1 : 0;
}

#ifdef HAVE_GGNEWSTEM

#ifndef HAVE_WMH
#define libggiwmh libggi /* attaching ggi twice does no harm */
#endif

int
open_visual(struct connection *cx)
{
	if (cx->stem)
		return 0;

	if (ggInit() < 0)
		return -1;
	cx->stem = ggNewStem(libgii, libggi, libggiwmh, NULL);
	if (!cx->stem)
		goto err_ggexit;
	if (ggiOpen(cx->stem, NULL) < 0)
		goto err_ggdelstem;

	if (cx->gii_input) {
		if (giiOpen(cx->stem, cx->gii_input, NULL) <= 0)
			/* zero is also bad, no match */
			goto err_ggiclose;
	}

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif
#ifdef HAVE_WIDGETS
	cx->visualanchor = ggiWidgetCreateContainerVisualAnchor(cx->stem);
#endif

	set_icon();

	return 0;

err_ggiclose:
	ggiClose(cx->stem);
err_ggdelstem:
	ggDelStem(cx->stem);
	cx->stem = NULL;
err_ggexit:
	ggExit();
	return -1;
}

#else /* HAVE_GGNEWSTEM */

int
open_visual(struct connection *cx)
{
	if (cx->stem)
		return 0;

	if (ggiInit() < 0)
		return -1;
        // Assign target frame buffer to ggi
        // Reference from http://manpages.ubuntu.com/manpages/intrepid/man7/display-memory.7.html
        std::string display = "display-memory:-pixfmt=";
        display += gPixformat;
        display += " pointer";
        //g.stem = e"display-memory:-pixfmt=r5g6b5 pointer", gTargetFrameBuffer );
        qDebug() << "Display: " << display.c_str();
        cx->stem = ggiOpen( display.c_str(), gTargetFrameBuffer );
	if (!cx->stem)
		goto err_ggiexit;

	if (cx->gii_input) {
		gii_input_t inp = giiOpen(cx->gii_input, NULL);
		if (!inp)
			goto err_ggiclose;
		if (!ggiJoinInputs(cx->stem, inp))
			goto err_ggiclose;
	}

#if defined(HAVE_WMH)
	if (ggiWmhInit() < 0)
		goto err_ggiclose;
	if (ggiWmhAttach(cx->stem) < 0)
		goto err_wmhexit;
#endif

	return 0;

#if defined(HAVE_WMH)
err_wmhexit:
	ggiWmhExit();
#endif
err_ggiclose:
	ggiClose(cx->stem);
	cx->stem = NULL;
err_ggiexit:
	ggiExit();
	return -1;
}

#endif /* HAVE_GGNEWSTEM */

void
close_visual(struct connection *cx)
{
	if (!cx->stem)
		return;

#ifdef HAVE_WIDGETS
	{
		ggi_widget_t visualanchor = cx->visualanchor;
		ggiWidgetDestroy(visualanchor);
		cx->visualanchor = NULL;
	}
#endif
	ggiClose(cx->stem);
#if defined(HAVE_WMH) && !defined(HAVE_GGNEWSTEM)
	ggiWmhDetach(cx->stem);
	ggiWmhExit();
#endif
#ifdef HAVE_GGNEWSTEM
	ggDelStem(cx->stem);
	cx->stem = NULL;
	ggExit();
#else
	ggiExit();
#endif
}

int ggivnc_main( int argc, char *argv[] )
{
	struct connection g;
	struct connection *cx = &g;
	int i;
	int status;
	struct gg_instance *fdselect;

	setlocale(LC_ALL, "");

	memset(cx, 0, sizeof(*cx));
	cx->port = 5900;
	cx->shared = 1;
	cx->sfd = -1;
	cx->listen = 0;
	cx->bind = NULL;
	cx->wire_endian = -1;
	cx->auto_encoding = 1;
	cx->max_protocol = 8;

	console_init();

	status = parse_options(cx, argc, argv);
	if (status >= 0)
		return status;

	cx->encoding_count = cx->allowed_encodings;
	cx->encoding = cx->allow_encoding;

reconnect:
	if (bandwidth_init(cx)) {
		fprintf(stderr, "out of memory\n");
		status = 5;
		goto err;
	}

	cx->name = NULL;
	status = 1;
	cx->sfd = -1;
	cx->fdselect = NULL;

	cx->read_ready = vnc_read_ready;
	cx->write_ready = vnc_write_ready;
	cx->safe_write = vnc_safe_write;

	if (cx->listen) {
		if (vnc_listen(cx))
			goto err;

		if (open_visual(cx))
			goto err;

		if (set_title(cx))
			goto err;
	}
	else {
		if (open_visual(cx))
			goto err;

		for (;;) {
			if (set_title(cx))
				goto err;

			if (!vnc_connect(cx))
				break;

reconnect2:
			status = get_connection(cx);
			switch (status) {
			case 0:
				status = 1;
				break;
			case -1:
				return 0;
			case -2:
				return 4;
			default:
				return status;
			}
		}
	}

	{
#if defined(F_GETFL)
		long flags = fcntl(cx->sfd, F_GETFL);
		fcntl(cx->sfd, F_SETFL, flags | O_NONBLOCK);
#elif defined(FIONBIO)
		u_long flags = 1;
		ioctlsocket(cx->sfd, FIONBIO, &flags);
#endif
	}

	cx->want_read = 1;
	cx->close_connection = 0;
	cx->input.rpos = 0;
	cx->input.wpos = 0;
	cx->output.rpos = 0;
	cx->output.wpos = 0;
	if (vnc_handshake(cx)) {
		debug(1, "vnc_handshake\n");
		if (cx->listen)
			goto err;
		if (cx->name)
			free(cx->name);
		cx->name = NULL;
		if (cx->sfd != -1)
			close(cx->sfd);
		cx->sfd = -1;
		if (!cx->file_passwd && cx->passwd) {
			free(cx->passwd);
			cx->passwd = NULL;
		}
		if (cx->vencrypt)
			vnc_security_vencrypt_end(cx, 1);
		cx->read_ready = vnc_read_ready;
		cx->write_ready = vnc_write_ready;
		cx->safe_write = vnc_safe_write;
		cx->close_connection = 0;
		switch (show_reconnect(cx, 0)) {
		case 1:
			debug(1, "reconnect\n");
			goto reconnect2;
		case 0:
			status = 0;
		default:
			goto err;
		}
	}

	select_mode(cx);

	ggiCheckMode(cx->stem, &cx->mode);

	if (ggiSetMode(cx->stem, &cx->mode))
		goto err;

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	if (set_title(cx)) {
		fprintf(stderr, "set_title\n");
		goto err;
	}

	cx->area.x = cx->width;
	cx->area.y = cx->height;
	scrollbar_area(cx);

	if (cx->mode.visible.x - cx->scrolly * SCROLL_SIZE > cx->width)
		cx->offset.x = (cx->mode.visible.x -
			cx->scrolly * SCROLL_SIZE - cx->width) / 2;
	else
		cx->offset.x = 0;
	if (cx->mode.visible.y - cx->scrollx * SCROLL_SIZE > cx->height)
		cx->offset.y = (cx->mode.visible.y -
			cx->scrollx * SCROLL_SIZE - cx->height) / 2;
	else
		cx->offset.y = 0;

#ifndef HAVE_WIDGETS
	if (cx->mode.visible.x < cx->width)
		cx->scrollx = 1;
	if (cx->mode.visible.y < cx->height)
		cx->scrolly = 1;
	cx->area = cx->mode.visible;
#endif

	ggiSetFlags(cx->stem, GGIFLAG_ASYNC);
	ggiSetColorfulPalette(cx->stem);

	generate_pixfmt(cx->local_pixfmt, sizeof(cx->local_pixfmt),
		ggiGetPixelFormat(cx->stem));
#ifdef GGI_BIG_ENDIAN
	cx->local_endian = 1;
#else
	cx->local_endian = 0;
#endif
	if (GT_SUBSCHEME(cx->mode.graphtype) & GT_SUB_REVERSE_ENDIAN)
		cx->local_endian = !cx->local_endian;

	if (!strcmp(cx->wire_pixfmt, "local")) {
		strcpy(cx->wire_pixfmt, cx->local_pixfmt);
		if (!strcmp(cx->wire_pixfmt, "weird"))
			strcpy(cx->wire_pixfmt, "r5g6b5");
	}

	if (!strcmp(cx->wire_pixfmt, "server")) {
		strcpy(cx->wire_pixfmt, cx->server_pixfmt);
		if (!strcmp(cx->wire_pixfmt, "weird"))
			strcpy(cx->wire_pixfmt, "r5g6b5");
	}

	switch (cx->wire_endian) {
	case -1:
	case -2:
		cx->wire_endian = cx->local_endian;
		break;
	case -3:
		cx->wire_endian = cx->server_endian;
		break;
	}

	memset(&cx->work, 0, sizeof(cx->work));
	if (buffer_reserve(&cx->work, 65536))
		goto err;

	cx->encoding_def[corre_encoding].action = vnc_corre;
	cx->encoding_def[hextile_encoding].action = vnc_hextile;
	cx->encoding_def[gii_encoding].action = gii_receive;
	cx->encoding_def[rre_encoding].action = vnc_rre;
	cx->encoding_def[tight_encoding].action = vnc_tight;
	cx->encoding_def[zlib_encoding].action = vnc_zlib;
	cx->encoding_def[zlibhex_encoding].action = vnc_zlibhex;
	cx->encoding_def[trle_encoding].action = vnc_trle;
	cx->encoding_def[zrle_encoding].action = vnc_zrle;
	cx->encoding_def[tight_file].action = vnc_unexpected;
	cx->encoding_def[xvp_encoding].action = xvp_receive;

	debug(1, "wire pixfmt: %s, %s endian\n",
		cx->wire_pixfmt,
		cx->wire_endian ? "big" : "little");

	if (vnc_set_pixel_format(cx))
		goto err;

	if (vnc_set_encodings(cx))
		goto err;

	if (vnc_update_request(cx, 0))
		goto err;

	cx->fdselect = fdselect = ggPlugModule(libgii, cx->stem,
		"input-fdselect", "-notify=fd", NULL);
	if (!cx->fdselect) {
		fprintf(stderr, "Unable to open input-fdselect\n");
		goto err;
	}

	if (cx->want_read)
		vnc_want_read(cx);
	if (cx->want_write)
		vnc_want_write(cx);
	ggObserve(fdselect->channel, vnc_fdselect, cx);

	cx->action = vnc_wait;

	if (cx->slide.x < 0)
		cx->slide.x = cx->width - cx->area.x + cx->slide.x + 1;
	if (cx->slide.y < 0)
		cx->slide.y = cx->height - cx->area.y + cx->slide.y + 1;
	set_scrollbars(cx);

	scrollbar_create(cx);

	if (cx->mode.frames > 1 && !cx->wire_stem) {
		debug(1, "going double buffer\n");
		ggiSetWriteFrame(cx->stem, 1);
		ggiSetReadFrame(cx->stem, 1);
	}

	if (loop(cx))
		goto err_closefdselect;

	status = 0;

err_closefdselect:
    ggClosePlugin((struct vnc_gg_instance*)cx->fdselect);
err:
	if (cx->name)
		free(cx->name);
	destroy_wire_stem(cx);
	for (i = encoding_defs - 1; i >= 0; --i) {
		if (cx->encoding_def[i].end)
			cx->encoding_def[i].end(cx);
	}
	if (cx->work.data)
		free(cx->work.data);
	if (cx->vencrypt)
		vnc_security_vencrypt_end(cx, 1);
	if (cx->sfd != -1)
		close(cx->sfd);
	bandwidth_fini(cx);
	if (cx->close_connection) {
		cx->close_connection = 0;
		if (show_reconnect(cx, 1) == 1) {
			debug(1, "reconnect\n");
			goto reconnect;
		}
	}
	if (cx->vencrypt)
		vnc_security_vencrypt_end(cx, 0);
	close_visual(cx);
	if (cx->allow_security)
		free(cx->allow_security);
	if (cx->bind)
		free(cx->bind);
	if (cx->server)
		free(cx->server);
	if (cx->passwd)
		free(cx->passwd);
	if (cx->username)
		free(cx->username);
	socket_cleanup();

	return status;
}
