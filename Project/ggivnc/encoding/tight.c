/*
******************************************************************************

   VNC viewer Tight encoding.

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
#include <zlib.h>
#ifdef HAVE_JPEG
#if defined HAVE_TURBOJPEG
#include <turbojpeg.h>
#elif defined HAVE_JPEGLIB
#if defined HAVE_BASETSD_H && !defined __CYGWIN__
#include <basetsd.h>
/* jpeglib.h looks for the double include guard used by Microsoft, but misses
 * the one used by MinGW. Replicate the Microsoft guard.
 */
#if !defined _BASETSD_H_ && defined _BASETSD_H
#define _BASETSD_H_
#endif
#endif /* HAVE_BASETSD_H */
#include <jpeglib.h>
#include <setjmp.h>
#endif
#endif /* HAVE_JPEG */
#define WIN32_LEAN_AND_MEAN
#include <ggi/ggi.h>

#include "vnc.h"
#include "vnc-compat.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

#ifdef HAVE_JPEG
#if defined HAVE_TURBOJPEG
#elif defined HAVE_JPEGLIB
struct jmp_error_mgr {
	struct jpeg_error_mgr std;
	jmp_buf env;
};
#endif
#endif /* HAVE_JPEG */

struct tight {
	int length;
	z_stream ztrm[4];
	int bpp;

	ggi_visual_t stem;
	ggi_visual_t xblt_stem; /* for 888 pixfmt */
	ggi_visual_t wire_stem; /* for 888 pixfmt */

	uint8_t control;
	uint8_t filter;
	int palette_size;
	ggi_pixel palette[256];
	action_t *fill;
	action_t *jpeg;
	action_t *copy;
	action_t *pal;
	action_t *gradient;

#ifdef HAVE_JPEG
	struct buffer xblt888;

#if defined HAVE_TURBOJPEG
	tjhandle tj;
#elif defined HAVE_JPEGLIB
	struct buffer xblt;
	struct jpeg_decompress_struct cinfo;
	struct jmp_error_mgr jerr;
	const ggi_directbuffer *db;
#endif
#endif /* HAVE_JPEG */
};

static int
tight_stem_change(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	ggi_mode mode;

	if (tight->xblt_stem)
		tight->xblt_stem = cx->wire_stem ? cx->wire_stem : cx->stem;
	else
		tight->stem = cx->wire_stem ? cx->wire_stem : cx->stem;

	if (!tight->wire_stem)
		return 0;

	ggiGetMode(tight->wire_stem, &mode);
	if (mode.visible.x == cx->width && mode.visible.y == cx->height)
		return 0;

	mode.visible.x = cx->width;
	mode.visible.y = cx->height;
	mode.virt.x = cx->width;
	mode.virt.y = cx->height;
	mode.size.x = mode.size.y = GGI_AUTO;

	if (ggiSetMode(tight->wire_stem, &mode)) {
		debug(1, "ggiSetMode failed\n");
		return -1;
	}

	return 0;
}

static int tight_inflate(struct connection *cx);

#ifdef HAVE_JPEG
#if defined HAVE_TURBOJPEG
#elif defined HAVE_JPEGLIB

static void
buffer_src_init_source(j_decompress_ptr cinfo)
{
	struct connection *cx = cinfo->client_data;
	cinfo->src->next_input_byte = &cx->input.data[cx->input.rpos];
	cinfo->src->bytes_in_buffer = cx->input.wpos - cx->input.rpos;
}

static boolean
buffer_src_fill_input_buffer(j_decompress_ptr cinfo)
{
	struct jmp_error_mgr *err = (struct jmp_error_mgr *)cinfo->err;

	debug(1, "jpeg fill_input_buffer\n");

	longjmp(err->env, 1);
	return TRUE;
}

static void
buffer_src_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	struct jmp_error_mgr *err = (struct jmp_error_mgr *)cinfo->err;

	debug(1, "jpeg skip_input_data\n");

	longjmp(err->env, 1);
}

static boolean
buffer_src_resync_to_restart(j_decompress_ptr cinfo, int desired)
{
	struct jmp_error_mgr *err = (struct jmp_error_mgr *)cinfo->err;

	debug(1, "jpeg resync_to_restart\n");

	longjmp(err->env, 1);
	return TRUE;
}

static void
buffer_src_term_source(j_decompress_ptr cinfo)
{
}

static void
buffer_src(j_decompress_ptr cinfo)
{
	struct jpeg_source_mgr *src;

	if (cinfo->src == NULL) {
		cinfo->src = (struct jpeg_source_mgr *)
			cinfo->mem->alloc_small((j_common_ptr) cinfo,
				JPOOL_PERMANENT,
				sizeof(*src));
	}
	src = cinfo->src;

	src->init_source       = buffer_src_init_source;
	src->fill_input_buffer = buffer_src_fill_input_buffer;
	src->skip_input_data   = buffer_src_skip_input_data;
	src->resync_to_restart = buffer_src_resync_to_restart;
	src->term_source       = buffer_src_term_source;

	buffer_src_init_source(cinfo);
}

static void
jmp_error_exit(j_common_ptr cinfo)
{
	struct connection *cx = cinfo->client_data;
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	struct jmp_error_mgr *err = (struct jmp_error_mgr *)cinfo->err;

	if (tight->db) {
		ggiResourceRelease(tight->db->resource);
		tight->db = NULL;
	}

	cinfo->err->output_message(cinfo);

	longjmp(err->env, 1);
}

static void
jmp_output_message(j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX];

	cinfo->err->format_message(cinfo, buffer);

	debug(1, "jpeg: %s\n", buffer);
}

#endif
#endif /* HAVE_JPEG */

static int
tight_drain_inflate(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	z_stream *ztrm;
	int res;

	debug(3, "tight_drain_inflate\n");

	if (cx->input.wpos < cx->input.rpos + tight->length) {
		cx->action = tight_drain_inflate;
		return 0;
	}

	ztrm = &tight->ztrm[(tight->control >> 4) & 3];
	ztrm->avail_in = tight->length;
	ztrm->next_in = &cx->input.data[cx->input.rpos];
	ztrm->avail_out = cx->work.size - cx->work.wpos;
	ztrm->next_out = &cx->work.data[cx->work.wpos];
	res = inflate(ztrm, Z_SYNC_FLUSH);
	switch (res) {
	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
		debug(1, "inflate result %d\n", res);
		inflateEnd(ztrm);
		return close_connection(cx, -1);
	}

	cx->input.rpos += tight->length;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_bit_palette(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int x;
	int y;
	int shift;
	uint8_t *src;

	debug(3, "tight_bit_palette\n");

	if (cx->work.wpos < cx->work.rpos + (cx->w + 7) / 8 * cx->h) {
		cx->action = tight_inflate;
		return 0;
	}

	src = &cx->work.data[cx->work.rpos];

	for (y = 0; y < cx->h; ++y) {
		shift = 7;
		for (x = 0; x < cx->w; ++x) {
			ggiPutPixel(tight->stem, cx->x + x, cx->y + y,
				tight->palette[(*src >> shift) & 1]);
			if (--shift < 0) {
				shift = 7;
				++src;
			}
		}
		if (shift != 7)
			++src;
	}

	if (tight->bpp == 3)
		ggiCrossBlit(tight->stem, cx->x, cx->y, cx->w, cx->h,
			tight->xblt_stem, cx->x, cx->y);

	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_byte_palette(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int x;
	int y;
	uint8_t *src;

	debug(3, "tight_byte_palette\n");

	if (cx->work.wpos < cx->work.rpos + cx->w * cx->h) {
		cx->action = tight_inflate;
		return 0;
	}

	src = &cx->work.data[cx->work.rpos];

	for (y = 0; y < cx->h; ++y) {
		for (x = 0; x < cx->w; ++x)
			ggiPutPixel(tight->stem, cx->x + x, cx->y + y,
				tight->palette[*src++]);
	}

	if (tight->bpp == 3)
		ggiCrossBlit(tight->stem, cx->x, cx->y, cx->w, cx->h,
			tight->xblt_stem, cx->x, cx->y);

	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_inflate(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int length;
	z_stream *ztrm;
	int res;
	int flush;

	debug(3, "tight_inflate\n");

	if (cx->input.wpos < cx->input.rpos + tight->length) {
		length = cx->input.wpos - cx->input.rpos;
		flush = Z_NO_FLUSH;
	}
	else {
		length = tight->length;
		flush = Z_SYNC_FLUSH;
	}

	ztrm = &tight->ztrm[(tight->control >> 4) & 3];
	ztrm->avail_in = length;
	ztrm->next_in = &cx->input.data[cx->input.rpos];

	do {
		if (cx->work.wpos == cx->work.size) {
			if (buffer_reserve(&cx->work,
				cx->work.size + 65536))
			{
				debug(1, "tight realloc failed\n");
				return close_connection(cx, -1);
			}
		}
		ztrm->avail_out = cx->work.size - cx->work.wpos;
		ztrm->next_out = &cx->work.data[cx->work.wpos];

		res = inflate(ztrm, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			debug(1, "tight inflate error %d\n", res);
			inflateEnd(ztrm);
			return close_connection(cx, -1);
		}

		cx->work.wpos = cx->work.size - ztrm->avail_out;
	} while (!ztrm->avail_out);

	tight->length -= length - ztrm->avail_in;
	cx->input.rpos += length - ztrm->avail_in;

	/* basic */
	switch (tight->filter) {
	case 0:
		return tight->copy(cx);
	case 1:
		if (tight->palette_size <= 2)
			return tight_bit_palette(cx);
		else
			return tight_byte_palette(cx);
	case 2:
		return tight->gradient(cx);
	}

	return close_connection(cx, -1);
}

static int
tight_fill_8(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	debug(3, "tight_fill_8\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = tight->fill;
		return 0;
	}

	ggiSetGCForeground(tight->stem, cx->input.data[cx->input.rpos++]);
	ggiDrawBox(tight->stem, cx->x, cx->y, cx->w, cx->h);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_fill_16(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	uint16_t pixel;
	debug(3, "tight_fill_16\n");

	if (cx->input.wpos < cx->input.rpos + 2) {
		cx->action = tight->fill;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get16_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get16(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 2;

	ggiSetGCForeground(tight->stem, pixel);
	ggiDrawBox(tight->stem, cx->x, cx->y, cx->w, cx->h);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_fill_888(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	uint32_t pixel;
	debug(3, "tight_fill_888\n");

	if (cx->input.wpos < cx->input.rpos + 3) {
		cx->action = tight->fill;
		return 0;
	}

#ifdef GGI_BIG_ENDIAN
	/* 24-bit modes are always little endian in GGI */
	pixel = get24ll_r(&cx->input.data[cx->input.rpos]);
#else
	pixel = get24ll(&cx->input.data[cx->input.rpos]);
#endif
	cx->input.rpos += 3;

	ggiSetGCForeground(tight->stem, pixel);
	ggiDrawBox(tight->stem, cx->x, cx->y, cx->w, cx->h);

	ggiCrossBlit(tight->stem, cx->x, cx->y, cx->w, cx->h,
		tight->xblt_stem, cx->x, cx->y);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_fill_32(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	uint32_t pixel;
	debug(3, "tight_fill_32\n");

	if (cx->input.wpos < cx->input.rpos + 4) {
		cx->action = tight->fill;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		pixel = get32_r(&cx->input.data[cx->input.rpos]);
	else
		pixel = get32(&cx->input.data[cx->input.rpos]);
	cx->input.rpos += 4;

	ggiSetGCForeground(tight->stem, pixel);
	ggiDrawBox(tight->stem, cx->x, cx->y, cx->w, cx->h);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_copy_8(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int bytes = cx->w * cx->h;

	debug(3, "tight_copy_8\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		cx->action = tight_inflate;
		return 0;
	}

	ggiPutBox(tight->stem,
		cx->x, cx->y, cx->w, cx->h, cx->work.data + cx->work.rpos);
	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_copy_16(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int bytes = 2 * cx->w * cx->h;

	debug(3, "tight_copy_16\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		cx->action = tight_inflate;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		buffer_reverse_16(cx->work.data, bytes);

	ggiPutBox(tight->stem,
		cx->x, cx->y, cx->w, cx->h, cx->work.data + cx->work.rpos);
	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_copy_888(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int bytes = 3 * cx->w * cx->h;

	debug(3, "tight_copy_888\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		cx->action = tight_inflate;
		return 0;
	}

	ggiPutBox(tight->stem,
		cx->x, cx->y, cx->w, cx->h, cx->work.data + cx->work.rpos);
	cx->work.rpos = 0;
	cx->work.wpos = 0;

	ggiCrossBlit(tight->stem, cx->x, cx->y, cx->w, cx->h,
		tight->xblt_stem, cx->x, cx->y);

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_copy_32(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int bytes = 4 * cx->w * cx->h;

	debug(3, "tight_copy_32\n");

	if (cx->work.wpos < cx->work.rpos + bytes) {
		cx->action = tight_inflate;
		return 0;
	}

	if (cx->wire_endian != cx->local_endian)
		buffer_reverse_32(cx->work.data, bytes);

	ggiPutBox(tight->stem,
		cx->x, cx->y, cx->w, cx->h, cx->work.data + cx->work.rpos);
	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_length(struct connection *cx)
{
	int length;

	if (cx->input.wpos < cx->input.rpos + 1)
		return ~0;

	length = cx->input.data[cx->input.rpos];
	if (!(length & 0x80)) {
		++cx->input.rpos;
		return length;
	}

	if (cx->input.wpos < cx->input.rpos + 2)
		return ~0;
	length &= 0x7f;
	length |= cx->input.data[cx->input.rpos + 1] << 7;
	if (!(length & 0x4000)) {
		cx->input.rpos += 2;
		return length;
	}

	if (cx->input.wpos < cx->input.rpos + 3)
		return ~0;
	length &= 0x3fff;
	length |= cx->input.data[cx->input.rpos + 2] << 14;
	cx->input.rpos += 3;
	return length;
}

static int
tight_basic(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int length;

	switch (tight->filter) {
	case 0: /* copy */
	case 2: /* gradient */
		length = tight->bpp * cx->w * cx->h;
		break;
	case 1: /* palette */
		if (tight->palette_size <= 2)
			length = (cx->w + 7) / 8 * cx->h;
		else
			length = cx->w * cx->h;
		break;
	default:
		return close_connection(cx, -1);
	}

	if (length >= 12) {
		tight->length = tight_length(cx);
		if (!~tight->length) {
			cx->action = tight_basic;
			return 0;
		}
		return tight_inflate(cx);
	}

	if (cx->input.wpos < cx->input.rpos + length) {
		cx->action = tight_basic;
		return 0;
	}

	tight->length = 0;
	memcpy(cx->work.data, &cx->input.data[cx->input.rpos], length);
	cx->input.rpos += length;
	cx->work.wpos = length;
	cx->work.rpos = 0;

	switch (tight->filter) {
	case 0:
		return tight->copy(cx);
	case 1:
		if (tight->palette_size <= 2)
			return tight_bit_palette(cx);
		else
			return tight_byte_palette(cx);
	case 2:
		return tight->gradient(cx);
	}

	return close_connection(cx, -1);
}

static int
tight_palette_8(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int i;

	debug(3, "tight_palette_8\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = tight->pal;
		return 0;
	}
	tight->palette_size = cx->input.data[cx->input.rpos] + 1;

	if (cx->input.wpos < cx->input.rpos + 1 + tight->palette_size) {
		cx->action = tight->pal;
		return 0;
	}

	for (i = 0; i < tight->palette_size; ++i)
		tight->palette[i] = cx->input.data[++cx->input.rpos];
	++cx->input.rpos;

	return tight_basic(cx);
}

static int
tight_palette_16(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int i;

	debug(3, "tight_palette_16\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = tight->pal;
		return 0;
	}
	tight->palette_size = cx->input.data[cx->input.rpos] + 1;

	if (cx->input.wpos < cx->input.rpos + 1 + 2 * tight->palette_size) {
		cx->action = tight->pal;
		return 0;
	}

	++cx->input.rpos;
	if (cx->wire_endian != cx->local_endian) {
		for (i = 0; i < tight->palette_size; ++i) {
			tight->palette[i] =
				get16_r(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
		}
	}
	else {
		for (i = 0; i < tight->palette_size; ++i) {
			tight->palette[i] =
				get16(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 2;
		}
	}

	return tight_basic(cx);
}

static int
tight_palette_888(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int i;

	debug(3, "tight_palette_888\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = tight->pal;
		return 0;
	}
	tight->palette_size = cx->input.data[cx->input.rpos] + 1;

	if (cx->input.wpos < cx->input.rpos + 1 + 3 * tight->palette_size) {
		cx->action = tight->pal;
		return 0;
	}

	++cx->input.rpos;
	for (i = 0; i < tight->palette_size; ++i) {
#ifdef GGI_BIG_ENDIAN
		/* 24-bit modes are always little endian in GGI */
		tight->palette[i] =
			get24ll_r(&cx->input.data[cx->input.rpos]);
#else
		tight->palette[i] =
			get24ll(&cx->input.data[cx->input.rpos]);
#endif
		cx->input.rpos += 3;
	}

	return tight_basic(cx);
}

static int
tight_palette_32(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int i;

	debug(3, "tight_palette_32\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->action = tight->pal;
		return 0;
	}
	tight->palette_size = cx->input.data[cx->input.rpos] + 1;

	if (cx->input.wpos < cx->input.rpos + 1 + 4 * tight->palette_size) {
		cx->action = tight->pal;
		return 0;
	}

	++cx->input.rpos;
	if (cx->wire_endian != cx->local_endian) {
		for (i = 0; i < tight->palette_size; ++i) {
			tight->palette[i] =
				get32_r(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
		}
	}
	else {
		for (i = 0; i < tight->palette_size; ++i) {
			tight->palette[i] =
				get32(&cx->input.data[cx->input.rpos]);
			cx->input.rpos += 4;
		}
	}

	return tight_basic(cx);
}

static inline uint16_t
bound_16(ggi_pixel mask, ggi_pixel left, ggi_pixel up, ggi_pixel left_up)
{
	ggi_pixel predict;

	predict = (left & mask) + (up & mask) - (left_up & mask);

	if (predict & ~mask) {
		if (predict & ~(mask | (mask << 1)))
			return 0;
		return mask;
	}

	return predict;
}

static inline uint8_t
bound_888(int value)
{
	if (value < 0)
		return 0;
	if (value > 255)
		return 255;
	return value;
}

static inline uint32_t
bound_32(ggi_pixel mask, ggi_pixel left, ggi_pixel up, ggi_pixel left_up)
{
	ggi_pixel predict;
	unsigned int shift = 0U;

	if (mask & 0xc0000000) {
		shift = 2U;
		mask >>= 2U;
		left >>= 2U;
		up >>= 2U;
		left_up >>= 2U;
	}

	predict = (left & mask) + (up & mask) - (left_up & mask);

	if (predict & ~mask) {
		if (predict & ~(mask | (mask << 1)))
			return 0;
		return mask << shift;
	}

	return predict << shift;
}

static int
tight_gradient_8(struct connection *cx)
{
	debug(1, "gradient filter on 8-bit modes is not allowed\n");
	return close_connection(cx, -1);
}

static int
tight_gradient_16(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int x, y;
	uint16_t *buf = (uint16_t *)&cx->work.data[cx->work.rpos];
	uint16_t *prev;
	int length = 2 * cx->w * cx->h;
	uint16_t pixel;
	const ggi_pixelformat *pixfmt;

	debug(3, "tight_gradient_16\n");

	if (cx->work.wpos < cx->work.rpos + length) {
		cx->action = tight_inflate;
		return 0;
	}

	pixfmt = ggiGetPixelFormat(tight->stem);

	/* first row */
	/* first pixel, nothing to do */
	/* rest of row */
	for (x = 1; x < cx->w; ++x) {
		pixel = ((buf[x] & pixfmt->red_mask) + 
			(buf[x - 1] & pixfmt->red_mask))
			& pixfmt->red_mask;
		pixel |= ((buf[x] & pixfmt->green_mask) + 
			(buf[x - 1] & pixfmt->green_mask))
			& pixfmt->green_mask;
		pixel |= ((buf[x] & pixfmt->blue_mask) + 
			(buf[x - 1] & pixfmt->blue_mask))
			& pixfmt->blue_mask;
		buf[x] = pixel;
	}

	prev = buf;
	buf += cx->w;

	/* following rows */
	for (y = 1; y < cx->h; ++y) {
		/* first pixel */
		pixel = ((buf[0] & pixfmt->red_mask) + 
			(prev[0] & pixfmt->red_mask))
			& pixfmt->red_mask;
		pixel |= ((buf[0] & pixfmt->green_mask) + 
			(prev[0] & pixfmt->green_mask))
			& pixfmt->green_mask;
		pixel |= ((buf[0] & pixfmt->blue_mask) + 
			(prev[0] & pixfmt->blue_mask))
			& pixfmt->blue_mask;
		buf[0] = pixel;

		/* rest of row */
		for (x = 1; x < cx->w; ++x) {
			pixel = ((buf[x] & pixfmt->red_mask) + 
				bound_16(pixfmt->red_mask,
					buf[x - 1], prev[x], prev[x - 1]))
				& pixfmt->red_mask;
			pixel |= ((buf[x] & pixfmt->green_mask) + 
				bound_16(pixfmt->green_mask,
					buf[x - 1], prev[x], prev[x - 1]))
				& pixfmt->green_mask;
			pixel |= ((buf[x] & pixfmt->blue_mask) + 
				bound_16(pixfmt->blue_mask,
					buf[x - 1], prev[x], prev[x - 1]))
				& pixfmt->blue_mask;
			buf[x] = pixel;
		}

		prev = buf;
		buf += cx->w;
	}

	ggiPutBox(tight->stem,
		cx->x, cx->y, cx->w, cx->h, cx->work.data + cx->work.rpos);
	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_gradient_888(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int x, y;
	uint8_t *buf = &cx->work.data[cx->work.rpos];
	uint8_t *prev;
	int length = 3 * cx->w * cx->h;
	int xs;

	debug(3, "tight_gradient_888\n");

	if (cx->work.wpos < cx->work.rpos + length) {
		cx->action = tight_inflate;
		return 0;
	}

	xs = cx->w * 3;

	/* first row */
	/* first pixel, nothing to do */
	/* rest of row */
	for (x = 3; x < xs; ++x)
		buf[x] += buf[x - 3];

	prev = buf;
	buf += xs;

	/* following rows */
	for (y = 1; y < cx->h; ++y) {
		/* first pixel */
		for (x = 0; x < 3; ++x)
			buf[x] += prev[x];

		/* rest of row */
		for (; x < xs; ++x)
			buf[x] += bound_888(
				(int)buf[x - 3] + prev[x] - prev[x - 3]);

		prev = buf;
		buf += xs;
	}

	ggiPutBox(tight->stem,
		cx->x, cx->y, cx->w, cx->h, cx->work.data + cx->work.rpos);
	cx->work.rpos = 0;
	cx->work.wpos = 0;

	ggiCrossBlit(tight->stem, cx->x, cx->y, cx->w, cx->h,
		tight->xblt_stem, cx->x, cx->y);

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_gradient_32(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int x, y;
	uint32_t *buf = (uint32_t *)&cx->work.data[cx->work.rpos];
	uint32_t *prev;
	int length = 4 * cx->w * cx->h;
	uint32_t pixel;
	const ggi_pixelformat *pixfmt;

	debug(3, "tight_gradient_32\n");

	if (cx->work.wpos < cx->work.rpos + length) {
		cx->action = tight_inflate;
		return 0;
	}

	pixfmt = ggiGetPixelFormat(tight->stem);

	/* first row */
	/* first pixel, nothing to do */
	/* rest of row */
	for (x = 1; x < cx->w; ++x) {
		pixel = ((buf[x] & pixfmt->red_mask) + 
			(buf[x - 1] & pixfmt->red_mask))
			& pixfmt->red_mask;
		pixel |= ((buf[x] & pixfmt->green_mask) + 
			(buf[x - 1] & pixfmt->green_mask))
			& pixfmt->green_mask;
		pixel |= ((buf[x] & pixfmt->blue_mask) + 
			(buf[x - 1] & pixfmt->blue_mask))
			& pixfmt->blue_mask;
		buf[x] = pixel;
	}

	prev = buf;
	buf += cx->w;

	/* following rows */
	for (y = 1; y < cx->h; ++y) {
		/* first pixel */
		pixel = ((buf[0] & pixfmt->red_mask) + 
			(prev[0] & pixfmt->red_mask))
			& pixfmt->red_mask;
		pixel |= ((buf[0] & pixfmt->green_mask) + 
			(prev[0] & pixfmt->green_mask))
			& pixfmt->green_mask;
		pixel |= ((buf[0] & pixfmt->blue_mask) + 
			(prev[0] & pixfmt->blue_mask))
			& pixfmt->blue_mask;
		buf[0] = pixel;

		/* rest of row */
		for (x = 1; x < cx->w; ++x) {
			pixel = ((buf[x] & pixfmt->red_mask) + 
				bound_32(pixfmt->red_mask,
					buf[x - 1], prev[x], prev[x - 1]))
				& pixfmt->red_mask;
			pixel |= ((buf[x] & pixfmt->green_mask) + 
				bound_32(pixfmt->green_mask,
					buf[x - 1], prev[x], prev[x - 1]))
				& pixfmt->green_mask;
			pixel |= ((buf[x] & pixfmt->blue_mask) + 
				bound_32(pixfmt->blue_mask,
					buf[x - 1], prev[x], prev[x - 1]))
				& pixfmt->blue_mask;
			buf[x] = pixel;
		}

		prev = buf;
		buf += cx->w;
	}

	ggiPutBox(tight->stem,
		cx->x, cx->y, cx->w, cx->h, cx->work.data + cx->work.rpos);
	cx->work.rpos = 0;
	cx->work.wpos = 0;

	if (tight->length)
		return tight_drain_inflate(cx);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

#ifdef HAVE_JPEG

static inline void
crossblit_row_16(const ggi_pixelformat *pixfmt,
	uint16_t *dst, const uint8_t *src, int xs)
{
	uint16_t pixel;

	while (xs--) {
		pixel  = ((*src++ << 24) >> pixfmt->red_shift)
			& pixfmt->red_mask;
		pixel |= ((*src++ << 24) >> pixfmt->green_shift)
			& pixfmt->green_mask;
		pixel |= ((*src++ << 24) >> pixfmt->blue_shift)
			& pixfmt->blue_mask;
		*dst++ = pixel;
	}
}

static inline void
crossblit_row_32(const ggi_pixelformat *pixfmt,
	uint32_t *dst, const uint8_t *src, int xs)
{
	uint32_t pixel;

	while (xs--) {
		pixel  = ((*src++ << 24) >> pixfmt->red_shift)
			& pixfmt->red_mask;
		pixel |= ((*src++ << 24) >> pixfmt->green_shift)
			& pixfmt->green_mask;
		pixel |= ((*src++ << 24) >> pixfmt->blue_shift)
			& pixfmt->blue_mask;
		*dst++ = pixel;
	}
}

static int
tight_jpeg_8(struct connection *cx)
{
	debug(1, "jpeg on 8-bit modes is not allowed\n");
	return close_connection(cx, -1);
}

#if defined HAVE_TURBOJPEG

static int
tight_turbojpeg_16(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int rpos = cx->input.rpos;
	const ggi_directbuffer *db;
	uint8_t *buf888;
	uint8_t *buf16;
	int length;
	int width, height;

	debug(3, "tight_turbojpeg_16\n");

	length = tight_length(cx);
	if (!~length) {
		cx->action = tight->jpeg;
		return 0;
	}

	if (cx->input.wpos < cx->input.rpos + length) {
		cx->input.rpos = rpos; /* step back */
		cx->action = tight->jpeg;
		return 0;
	}

	if (tjDecompressHeader(tight->tj,
		&cx->input.data[cx->input.rpos], length,
		&width, &height))
	{
		debug(1, "tjpeg header error: %s\n", tjGetErrorStr());
		return close_connection(cx, -1);
	}

	if (width != cx->w || height != cx->h) {
		debug(1, "tjpeg rect mismatch\n");
		return close_connection(cx, -1);
	}

	if (buffer_reserve(&tight->xblt888, 3 * cx->w * cx->h))
		return close_connection(cx, -1);

	if (tjDecompress(tight->tj, &cx->input.data[cx->input.rpos], length,
		tight->xblt888.data, width, 3 * width, height, 3, TJ_BGR))
	{
		debug(1, "tjpeg decompress error: %s\n", tjGetErrorStr());
		return close_connection(cx, -1);
	}

	db = ggiDBGetBuffer(tight->stem, 0);
	ggiResourceAcquire(db->resource, GGI_ACTYPE_WRITE);

	buf888 = tight->xblt888.data;
	buf16 = (uint8_t *)db->write +
		cx->y * db->buffer.plb.stride + 2 * cx->x;
	while (height--) {
		crossblit_row_16(ggiGetPixelFormat(tight->stem),
			(uint16_t *)buf16, buf888, cx->w);
		buf888 += 3 * cx->w;
		buf16 += db->buffer.plb.stride;
	}

	ggiResourceRelease(db->resource);

	cx->input.rpos += length;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_turbojpeg_888(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int rpos = cx->input.rpos;
	const ggi_directbuffer *db;
	int length;
	int width, height;

	debug(3, "tight_turbojpeg_888\n");

	length = tight_length(cx);
	if (!~length) {
		cx->action = tight->jpeg;
		return 0;
	}

	if (cx->input.wpos < cx->input.rpos + length) {
		cx->input.rpos = rpos; /* step back */
		cx->action = tight->jpeg;
		return 0;
	}

	if (tjDecompressHeader(tight->tj,
		&cx->input.data[cx->input.rpos], length,
		&width, &height))
	{
		debug(1, "tjpeg header error: %s\n", tjGetErrorStr());
		return close_connection(cx, -1);
	}

	if (width != cx->w || height != cx->h) {
		debug(1, "tjpeg rect mismatch\n");
		return close_connection(cx, -1);
	}

	db = ggiDBGetBuffer(tight->stem, 0);
	ggiResourceAcquire(db->resource, GGI_ACTYPE_WRITE);

	if (tjDecompress(tight->tj, &cx->input.data[cx->input.rpos], length,
		(uint8_t *)db->write +
			cx->y * db->buffer.plb.stride + 3 * cx->x,
		width, db->buffer.plb.stride, height, 3, TJ_BGR))
	{
		debug(1, "tjpeg decompress error: %s\n", tjGetErrorStr());
		ggiResourceRelease(db->resource);
		return close_connection(cx, -1);
	}
		
	ggiResourceRelease(db->resource);

	cx->input.rpos += length;

	ggiCrossBlit(tight->stem, cx->x, cx->y, cx->w, cx->h,
		tight->xblt_stem, cx->x, cx->y);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_turbojpeg_32(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int rpos = cx->input.rpos;
	const ggi_directbuffer *db;
	uint8_t *buf888;
	uint8_t *buf32;
	int length;
	int width, height;

	debug(3, "tight_turbojpeg_32\n");

	length = tight_length(cx);
	if (!~length) {
		cx->action = tight->jpeg;
		return 0;
	}

	if (cx->input.wpos < cx->input.rpos + length) {
		cx->input.rpos = rpos; /* step back */
		cx->action = tight->jpeg;
		return 0;
	}

	if (tjDecompressHeader(tight->tj,
		&cx->input.data[cx->input.rpos], length,
		&width, &height))
	{
		debug(1, "tjpeg header error: %s\n", tjGetErrorStr());
		return close_connection(cx, -1);
	}

	if (width != cx->w || height != cx->h) {
		debug(1, "tjpeg rect mismatch\n");
		return close_connection(cx, -1);
	}

	if (buffer_reserve(&tight->xblt888, 3 * cx->w * cx->h))
		return close_connection(cx, -1);

	if (tjDecompress(tight->tj, &cx->input.data[cx->input.rpos], length,
		tight->xblt888.data, width, 3 * width, height, 3, TJ_BGR))
	{
		debug(1, "tjpeg decompress error: %s\n", tjGetErrorStr());
		return close_connection(cx, -1);
	}

	db = ggiDBGetBuffer(tight->stem, 0);
	ggiResourceAcquire(db->resource, GGI_ACTYPE_WRITE);

	buf888 = tight->xblt888.data;
	buf32 = (uint8_t *)db->write +
		cx->y * db->buffer.plb.stride + 4 * cx->x;
	while (height--) {
		crossblit_row_32(ggiGetPixelFormat(tight->stem),
			(uint32_t *)buf32, buf888, cx->w);
		buf888 += 3 * cx->w;
		buf32 += db->buffer.plb.stride;
	}

	ggiResourceRelease(db->resource);

	cx->input.rpos += length;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

#define tight_jpeg_16  tight_turbojpeg_16
#define tight_jpeg_888 tight_turbojpeg_888
#define tight_jpeg_32  tight_turbojpeg_32

#elif defined HAVE_JPEGLIB

static int
tight_jpeg_16(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int rpos = cx->input.rpos;
	uint16_t *buf16;
	int length;

	debug(3, "tight_jpeg_16\n");

	length = tight_length(cx);
	if (!~length) {
		cx->action = tight->jpeg;
		return 0;
	}

	if (cx->input.wpos < cx->input.rpos + length) {
		cx->input.rpos = rpos; /* step back */
		cx->action = tight->jpeg;
		return 0;
	}

	if (buffer_reserve(&tight->xblt, 2 * cx->w))
		return close_connection(cx, -1);

	if (buffer_reserve(&tight->xblt888, 3 * cx->w))
		return close_connection(cx, -1);

	buf16 = (uint16_t *)tight->xblt.data;

	if (setjmp(tight->jerr.env)) {
		jpeg_destroy_decompress(&tight->cinfo);
		return close_connection(cx, -1);
	}

	jpeg_read_header(&tight->cinfo, TRUE);
	tight->cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&tight->cinfo);

	if (tight->cinfo.output_width != cx->w ||
		tight->cinfo.output_height != cx->h ||
		tight->cinfo.out_color_components != 3)
	{
		debug(1, "jpeg rect mismatch\n");
		return close_connection(cx, -1);
	}

	while (tight->cinfo.output_scanline < tight->cinfo.output_height) {
		int y = tight->cinfo.output_scanline;
		jpeg_read_scanlines(&tight->cinfo, &tight->xblt888.data, 1);
		crossblit_row_16(ggiGetPixelFormat(tight->stem),
			buf16, tight->xblt888.data, cx->w);
		ggiPutHLine(tight->stem, cx->x, cx->y + y, cx->w, buf16);
	}

	jpeg_finish_decompress(&tight->cinfo);

	cx->input.rpos += length;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_jpeg_888(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int rpos = cx->input.rpos;
	uint8_t *scanlines;
	int length;

	debug(3, "tight_jpeg_888\n");

	length = tight_length(cx);
	if (!~length) {
		cx->action = tight->jpeg;
		return 0;
	}

	if (cx->input.wpos < cx->input.rpos + length) {
		cx->input.rpos = rpos; /* step back */
		cx->action = tight->jpeg;
		return 0;
	}

	if (setjmp(tight->jerr.env)) {
		jpeg_destroy_decompress(&tight->cinfo);
		return close_connection(cx, -1);
	}

	jpeg_read_header(&tight->cinfo, TRUE);
	tight->cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&tight->cinfo);

	if (tight->cinfo.output_width != cx->w ||
		tight->cinfo.output_height != cx->h ||
		tight->cinfo.out_color_components != 3)
	{
		debug(1, "jpeg rect mismatch\n");
		return close_connection(cx, -1);
	}

	tight->db = ggiDBGetBuffer(tight->stem, 0);
	ggiResourceAcquire(tight->db->resource, GGI_ACTYPE_WRITE);

	while (tight->cinfo.output_scanline < tight->cinfo.output_height) {
		scanlines = (uint8_t *)tight->db->write +
			(cx->y + tight->cinfo.output_scanline) *
			 tight->db->buffer.plb.stride + 3 * cx->x;
		jpeg_read_scanlines(&tight->cinfo, &scanlines, 1);
	}

	jpeg_finish_decompress(&tight->cinfo);

	ggiResourceRelease(tight->db->resource);
	tight->db = NULL;

	cx->input.rpos += length;

	ggiCrossBlit(tight->stem, cx->x, cx->y, cx->w, cx->h,
		tight->xblt_stem, cx->x, cx->y);

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

static int
tight_jpeg_32(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	int rpos = cx->input.rpos;
	uint32_t *buf32;
	int length;

	debug(3, "tight_jpeg_32\n");

	length = tight_length(cx);
	if (!~length) {
		cx->action = tight->jpeg;
		return 0;
	}

	if (cx->input.wpos < cx->input.rpos + length) {
		cx->input.rpos = rpos; /* step back */
		cx->action = tight->jpeg;
		return 0;
	}

	if (buffer_reserve(&tight->xblt, 4 * cx->w))
		return close_connection(cx, -1);

	if (buffer_reserve(&tight->xblt888, 3 * cx->w))
		return close_connection(cx, -1);

	buf32 = (uint32_t *)tight->xblt.data;

	if (setjmp(tight->jerr.env)) {
		jpeg_destroy_decompress(&tight->cinfo);
		return close_connection(cx, -1);
	}

	jpeg_read_header(&tight->cinfo, TRUE);
	tight->cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&tight->cinfo);

	if (tight->cinfo.output_width != cx->w ||
		tight->cinfo.output_height != cx->h ||
		tight->cinfo.out_color_components != 3)
	{
		debug(1, "jpeg rect mismatch\n");
		return close_connection(cx, -1);
	}

	while (tight->cinfo.output_scanline < tight->cinfo.output_height) {
		int y = tight->cinfo.output_scanline;
		jpeg_read_scanlines(&tight->cinfo, &tight->xblt888.data, 1);
		crossblit_row_32(ggiGetPixelFormat(tight->stem),
			buf32, tight->xblt888.data, cx->w);
		ggiPutHLine(tight->stem, cx->x, cx->y + y, cx->w, buf32);
	}

	jpeg_finish_decompress(&tight->cinfo);

	cx->input.rpos += length;

	--cx->rects;

	remove_dead_data(&cx->input);
	cx->action = vnc_update_rect;
	return 1;
}

#endif
#else

static int
tight_jpeg(struct connection *cx)
{
	debug(1, "unexpected jpeg request\n");
	return close_connection(cx, -1);
}

#define tight_jpeg_8   tight_jpeg
#define tight_jpeg_16  tight_jpeg
#define tight_jpeg_888 tight_jpeg
#define tight_jpeg_32  tight_jpeg

#endif /* HAVE_JPEG */

static int
tight_init_ztrm(z_stream *ztrm)
{
	ztrm->zalloc = Z_NULL;
	ztrm->zfree = Z_NULL;
	ztrm->opaque = Z_NULL;
	ztrm->avail_in = 0;
	ztrm->next_in = Z_NULL;
	ztrm->avail_out = 0;

	if (inflateInit(ztrm) != Z_OK)
		return -1;

	return 0;
}

static int
tight_reset_ztrm(z_stream *ztrm)
{
	inflateEnd(ztrm);

	return tight_init_ztrm(ztrm);
}

static ggi_visual_t
tight_stem_888(struct connection *cx)
{
	ggi_visual_t stem;
	ggi_mode mode;

	memset(&mode, 0, sizeof(mode));
	mode.frames = 1;
	mode.visible.x = cx->width;
	mode.visible.y = cx->height;
	mode.virt.x    = cx->width;
	mode.virt.y    = cx->height;
	mode.size.x = mode.size.y = GGI_AUTO;
	GT_SETDEPTH(mode.graphtype, 24);
	GT_SETSIZE(mode.graphtype, 24);
	GT_SETSCHEME(mode.graphtype, GT_TRUECOLOR);
	mode.dpp.x = mode.dpp.y = 1;

#ifdef HAVE_GGNEWSTEM
	stem = ggNewStem(libggi, NULL);
	if (!stem) {
		debug(1, "ggNewStem failed\n");
		return NULL;
	}
	/* 24-bit mode are always little endian in GGI */
	if (ggiOpen(stem, "display-memory:-pixfmt=b8g8r8", NULL) != GGI_OK) {
		debug(1, "ggiOpen failed\n");
		ggDelStem(stem);
		return NULL;
	}
#else /* HAVE_GGNEWSTEM */
	stem = ggiOpen("display-memory:-pixfmt=b8g8r8", NULL);
	if (!stem) {
		debug(1, "ggiOpen failed\n");
		return NULL;
	}
#endif /* HAVE_GGNEWSTEM */

	if (ggiSetMode(stem, &mode)) {
		debug(1, "ggiSetMode failed\n");
		ggiClose(stem);
#ifdef HAVE_GGNEWSTEM
		ggDelStem(stem);
#endif
		return NULL;
	}

	return stem;
}

static int
tight_rect(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;
	const ggi_pixelformat *pf;

	debug(2, "tight\n");

	if (cx->input.wpos < cx->input.rpos + 1)
		return 0;

	tight->control = cx->input.data[cx->input.rpos];

	if ((tight->control & 0xc0) == 0x40) {
		/* basic with filter */
		if (cx->input.wpos < cx->input.rpos + 2)
			return 0;
		tight->filter = cx->input.data[++cx->input.rpos];
	}
	else
		tight->filter = 0;

	++cx->input.rpos;

	if ((tight->control & 0x01) && tight_reset_ztrm(&tight->ztrm[0]))
		return close_connection(cx, -1);
	if ((tight->control & 0x02) && tight_reset_ztrm(&tight->ztrm[1]))
		return close_connection(cx, -1);
	if ((tight->control & 0x04) && tight_reset_ztrm(&tight->ztrm[2]))
		return close_connection(cx, -1);
	if ((tight->control & 0x08) && tight_reset_ztrm(&tight->ztrm[3]))
		return close_connection(cx, -1);
	tight->control &= 0xf0;

	tight->stem = cx->wire_stem ? cx->wire_stem : cx->stem;
	cx->stem_change = tight_stem_change;

	switch (GT_SIZE(cx->wire_mode.graphtype)) {
	case  8:
		tight->bpp           = 1;
		tight->fill          = tight_fill_8;
		tight->jpeg          = tight_jpeg_8;
		tight->copy          = tight_copy_8;
		tight->pal           = tight_palette_8;
		tight->gradient      = tight_gradient_8;
		break;
	case 16:
		tight->bpp           = 2;
		tight->fill          = tight_fill_16;
		tight->jpeg          = tight_jpeg_16;
		tight->copy          = tight_copy_16;
		tight->pal           = tight_palette_16;
		tight->gradient      = tight_gradient_16;
		break;
	case 32:
		pf = ggiGetPixelFormat(tight->stem);
		if (((pf->red_mask   << pf->red_shift)   == 0xff000000) &&
		    ((pf->green_mask << pf->green_shift) == 0xff000000) &&
		    ((pf->blue_mask  << pf->blue_shift)  == 0xff000000))
		{
			if (!tight->wire_stem) {
				tight->wire_stem = tight_stem_888(cx);
				if (!tight->wire_stem)
					return close_connection(cx, -1);
			}
			tight->xblt_stem = tight->stem;
			tight->stem = tight->wire_stem;

			tight->bpp           = 3;
			tight->fill          = tight_fill_888;
			tight->jpeg          = tight_jpeg_888;
			tight->copy          = tight_copy_888;
			tight->pal           = tight_palette_888;
			tight->gradient      = tight_gradient_888;
			break;
		}
		tight->bpp           = 4;
		tight->fill          = tight_fill_32;
		tight->jpeg          = tight_jpeg_32;
		tight->copy          = tight_copy_32;
		tight->pal           = tight_palette_32;
		tight->gradient      = tight_gradient_32;
		break;
	}

	if (tight->control == 0x80)
		return tight->fill(cx);
	if (tight->control == 0x90)
		return tight->jpeg(cx);
	if (tight->control & 0x80)
		return close_connection(cx, -1);

	if (tight->filter == 1)
		return tight->pal(cx);

	return tight_basic(cx);
}

static void
tight_end(struct connection *cx)
{
	struct tight *tight = cx->encoding_def[tight_encoding].priv;

	if (!tight)
		return;

	debug(1, "tight_end\n");

	if (tight->wire_stem) {
		ggiClose(tight->wire_stem);
#ifdef HAVE_GGNEWSTEM
		ggDelStem(tight->wire_stem);
#endif
	}

#ifdef HAVE_JPEG
#if defined HAVE_TURBOJPEG
	if (tight->tj)
		tjDestroy(tight->tj);
#elif defined HAVE_JPEGLIB
	jpeg_destroy_decompress(&tight->cinfo);

	if (tight->xblt.data)
		free(tight->xblt.data);
#endif

	if (tight->xblt888.data)
		free(tight->xblt888.data);
#endif /* HAVE_JPEG */

	inflateEnd(&tight->ztrm[3]);
	inflateEnd(&tight->ztrm[2]);
	inflateEnd(&tight->ztrm[1]);
	inflateEnd(&tight->ztrm[0]);

	free(cx->encoding_def[tight_encoding].priv);
	cx->encoding_def[tight_encoding].priv = NULL;
	cx->encoding_def[tight_encoding].action = vnc_tight;
}

int
vnc_tight(struct connection *cx)
{
	struct tight *tight;

	tight = malloc(sizeof(*tight));
	if (!tight)
		return close_connection(cx, -1);
	memset(tight, 0, sizeof(*tight));

	cx->encoding_def[tight_encoding].priv = tight;
	cx->encoding_def[tight_encoding].end = tight_end;

	if (tight_init_ztrm(&tight->ztrm[0]))
		return close_connection(cx, -1);
	if (tight_init_ztrm(&tight->ztrm[1]))
		return close_connection(cx, -1);
	if (tight_init_ztrm(&tight->ztrm[2]))
		return close_connection(cx, -1);
	if (tight_init_ztrm(&tight->ztrm[3]))
		return close_connection(cx, -1);

#ifdef HAVE_JPEG
	if (buffer_reserve(&tight->xblt888, 3 * 2048))
		return close_connection(cx, -1);

#if defined HAVE_TURBOJPEG
	tight->tj = tjInitDecompress();
	if (!tight->tj)
		return close_connection(cx, -1);
#elif defined HAVE_JPEGLIB
	if (buffer_reserve(&tight->xblt, 4 * 2048))
		return close_connection(cx, -1);

	tight->cinfo.client_data = cx;
	tight->cinfo.err = jpeg_std_error(&tight->jerr.std);
	tight->jerr.std.trace_level =
		get_debug_level() > 3 ? get_debug_level() - 3 : 0;
	tight->jerr.std.error_exit     = jmp_error_exit;
	tight->jerr.std.output_message = jmp_output_message;
	if (setjmp(tight->jerr.env))
		return close_connection(cx, -1);
	jpeg_create_decompress(&tight->cinfo);
	buffer_src(&tight->cinfo);
#endif
#endif /* HAVE_JPEG */

	cx->action = cx->encoding_def[tight_encoding].action = tight_rect;
	return cx->action(cx);
}
