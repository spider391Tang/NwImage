/*
******************************************************************************

   VNC viewer Tight encoding.

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

#include <stdio.h>
#include <string.h>
#include <zlib.h>
#ifdef HAVE_JPEG
#include <jpeglib.h>
#endif
#define WIN32_LEAN_AND_MEAN
#include <ggi/ggi.h>

#include "vnc.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

struct tight_t {
	int length;
	z_stream ztrm[4];
	struct buffer xblt;
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
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
#endif
};

static struct tight_t tight;

static void
tight_stem_change(void)
{
	ggi_mode mode;

	if (tight.xblt_stem)
		tight.xblt_stem = g.wire_stem ? g.wire_stem : g.stem;
	else
		tight.stem = g.wire_stem ? g.wire_stem : g.stem;

	if (!tight.wire_stem)
		return;

	ggiGetMode(tight.wire_stem, &mode);
	if (mode.visible.x == g.width && mode.visible.y == g.height)
		return;

	mode.visible.x = g.width;
	mode.visible.y = g.height;
	mode.virt.x = g.width;
	mode.virt.y = g.height;
	mode.size.x = mode.size.y = GGI_AUTO;

	if (ggiSetMode(tight.wire_stem, &mode)) {
		debug(1, "ggiSetMode failed\n");
		exit(1);
	}
}

static int tight_inflate(void);

#ifdef HAVE_JPEG

static void
buffer_src_init_source(j_decompress_ptr cinfo)
{
	cinfo->src->next_input_byte = &g.input.data[g.input.rpos];
	cinfo->src->bytes_in_buffer = g.input.wpos - g.input.rpos;
}

static boolean
buffer_src_fill_input_buffer(j_decompress_ptr cinfo)
{
	debug(0, "jpeg fill_input_buffer\n");
	exit(1);
	return TRUE;
}

static void
buffer_src_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	debug(0, "jpeg skip_input_data\n");
	exit(1);
}

static boolean
buffer_src_resync_to_restart(j_decompress_ptr cinfo, int desired)
{
	debug(0, "jpeg resync_to_restart\n");
	exit(1);
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

#endif /* HAVE_JPEG */

static int
tight_drain_inflate(void)
{
	z_stream *ztrm;
	int res;

	debug(3, "tight_drain_inflate\n");

	if (g.input.wpos < g.input.rpos + tight.length) {
		g.action = tight_drain_inflate;
		return 0;
	}

	ztrm = &tight.ztrm[(tight.control >> 4) & 3];
	ztrm->avail_in = tight.length;
	ztrm->next_in = &g.input.data[g.input.rpos];
	ztrm->avail_out = g.work.size - g.work.wpos;
	ztrm->next_out = &g.work.data[g.work.wpos];
	res = inflate(ztrm, Z_SYNC_FLUSH);
	switch (res) {
	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
		debug(1, "inflate result %d\n", res);
		inflateEnd(ztrm);
		exit(1);
	}

	g.input.rpos += tight.length;

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_bit_palette(void)
{
	int x;
	int y;
	int shift;
	uint8_t *src;

	debug(3, "tight_bit_palette\n");

	if (g.work.wpos < g.work.rpos + (g.w + 7) / 8 * g.h) {
		g.action = tight_inflate;
		return 0;
	}

	src = &g.work.data[g.work.rpos];

	for (y = 0; y < g.h; ++y) {
		shift = 7;
		for (x = 0; x < g.w; ++x) {
			ggiPutPixel(tight.stem, g.x + x, g.y + y,
				tight.palette[(*src >> shift) & 1]);
			if (--shift < 0) {
				shift = 7;
				++src;
			}
		}
		if (shift != 7)
			++src;
	}

	if (tight.bpp == 3)
		ggiCrossBlit(tight.stem, g.x, g.y, g.w, g.h,
			tight.xblt_stem, g.x, g.y);

	g.work.rpos = 0;
	g.work.wpos = 0;

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_byte_palette(void)
{
	int x;
	int y;
	uint8_t *src;

	debug(3, "tight_byte_palette\n");

	if (g.work.wpos < g.work.rpos + g.w * g.h) {
		g.action = tight_inflate;
		return 0;
	}

	src = &g.work.data[g.work.rpos];

	for (y = 0; y < g.h; ++y) {
		for (x = 0; x < g.w; ++x)
			ggiPutPixel(tight.stem, g.x + x, g.y + y,
				tight.palette[*src++]);
	}

	if (tight.bpp == 3)
		ggiCrossBlit(tight.stem, g.x, g.y, g.w, g.h,
			tight.xblt_stem, g.x, g.y);

	g.work.rpos = 0;
	g.work.wpos = 0;

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_inflate(void)
{
	int length;
	z_stream *ztrm;
	int res;
	int flush;

	debug(3, "tight_inflate\n");

	if (g.input.wpos < g.input.rpos + tight.length) {
		length = g.input.wpos - g.input.rpos;
		flush = Z_NO_FLUSH;
	}
	else {
		length = tight.length;
		flush = Z_SYNC_FLUSH;
	}

	ztrm = &tight.ztrm[(tight.control >> 4) & 3];
	ztrm->avail_in = length;
	ztrm->next_in = &g.input.data[g.input.rpos];

	do {
		if (g.work.wpos == g.work.size) {
			void *tmp;
			g.work.size += 65536;
			tmp = realloc(g.work.data, g.work.size);
			if (!tmp) {
				free(g.work.data);
				g.work.data = NULL;
				exit(1);
			}
			g.work.data = tmp;
		}
		ztrm->avail_out = g.work.size - g.work.wpos;
		ztrm->next_out = &g.work.data[g.work.wpos];

		res = inflate(ztrm, flush);
		switch (res) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			debug(1, "inflate result %d\n", res);
			inflateEnd(ztrm);
			exit(1);
		}

		g.work.wpos = g.work.size - ztrm->avail_out;
	} while (!ztrm->avail_out);

	tight.length -= length - ztrm->avail_in;
	g.input.rpos += length - ztrm->avail_in;

	/* basic */
	switch (tight.filter) {
	case 0:
		return tight.copy();
	case 1:
		if (tight.palette_size <= 2)
			return tight_bit_palette();
		else
			return tight_byte_palette();
	case 2:
		return tight.gradient();
	default:
		exit(1);
	}

	/* silence compiler */
	return 0;
}

static int
tight_fill_8(void)
{
	debug(3, "tight_fill_8\n");

	if (g.input.wpos < g.input.rpos + 1) {
		g.action = tight.fill;
		return 0;
	}

	ggiSetGCForeground(tight.stem, g.input.data[g.input.rpos++]);
	ggiDrawBox(tight.stem, g.x, g.y, g.w, g.h);

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_fill_16(void)
{
	uint16_t pixel;
	debug(3, "tight_fill_16\n");

	if (g.input.wpos < g.input.rpos + 2) {
		g.action = tight.fill;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		pixel = get16_r(&g.input.data[g.input.rpos]);
	else
		pixel = get16(&g.input.data[g.input.rpos]);
	g.input.rpos += 2;

	ggiSetGCForeground(tight.stem, pixel);
	ggiDrawBox(tight.stem, g.x, g.y, g.w, g.h);

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_fill_888(void)
{
	uint32_t pixel;
	debug(3, "tight_fill_888\n");

	if (g.input.wpos < g.input.rpos + 3) {
		g.action = tight.fill;
		return 0;
	}

#ifdef GGI_BIG_ENDIAN
	/* 24-bit modes are always little endian in GGI */
	pixel = get24ll_r(&g.input.data[g.input.rpos]);
#else
	pixel = get24ll(&g.input.data[g.input.rpos]);
#endif
	g.input.rpos += 3;

	ggiSetGCForeground(tight.stem, pixel);
	ggiDrawBox(tight.stem, g.x, g.y, g.w, g.h);

	ggiCrossBlit(tight.stem, g.x, g.y, g.w, g.h,
		tight.xblt_stem, g.x, g.y);

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_fill_32(void)
{
	uint32_t pixel;
	debug(3, "tight_fill_32\n");

	if (g.input.wpos < g.input.rpos + 4) {
		g.action = tight.fill;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		pixel = get32_r(&g.input.data[g.input.rpos]);
	else
		pixel = get32(&g.input.data[g.input.rpos]);
	g.input.rpos += 4;

	ggiSetGCForeground(tight.stem, pixel);
	ggiDrawBox(tight.stem, g.x, g.y, g.w, g.h);

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_copy_8(void)
{
	int bytes = g.w * g.h;

	debug(3, "tight_copy_8\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		g.action = tight_inflate;
		return 0;
	}

	ggiPutBox(tight.stem, g.x, g.y, g.w, g.h, g.work.data + g.work.rpos);
	g.work.rpos = 0;
	g.work.wpos = 0;

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_copy_16(void)
{
	int bytes = 2 * g.w * g.h;

	debug(3, "tight_copy_16\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		g.action = tight_inflate;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		buffer_reverse_16(g.work.data, bytes);

	ggiPutBox(tight.stem, g.x, g.y, g.w, g.h, g.work.data + g.work.rpos);
	g.work.rpos = 0;
	g.work.wpos = 0;

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_copy_888(void)
{
	int bytes = 3 * g.w * g.h;

	debug(3, "tight_copy_888\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		g.action = tight_inflate;
		return 0;
	}

	ggiPutBox(tight.stem, g.x, g.y, g.w, g.h, g.work.data + g.work.rpos);
	g.work.rpos = 0;
	g.work.wpos = 0;

	ggiCrossBlit(tight.stem, g.x, g.y, g.w, g.h,
		tight.xblt_stem, g.x, g.y);

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_copy_32(void)
{
	int bytes = 4 * g.w * g.h;

	debug(3, "tight_copy_32\n");

	if (g.work.wpos < g.work.rpos + bytes) {
		g.action = tight_inflate;
		return 0;
	}

	if (g.wire_endian != g.local_endian)
		buffer_reverse_32(g.work.data, bytes);

	ggiPutBox(tight.stem, g.x, g.y, g.w, g.h, g.work.data + g.work.rpos);
	g.work.rpos = 0;
	g.work.wpos = 0;

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_length(void)
{
	int length;

	if (g.input.wpos < g.input.rpos + 1)
		return ~0;

	length = g.input.data[g.input.rpos];
	if (!(length & 0x80)) {
		++g.input.rpos;
		return length;
	}

	if (g.input.wpos < g.input.rpos + 2)
		return ~0;
	length &= 0x7f;
	length |= g.input.data[g.input.rpos + 1] << 7;
	if (!(length & 0x4000)) {
		g.input.rpos += 2;
		return length;
	}

	if (g.input.wpos < g.input.rpos + 3)
		return ~0;
	length &= 0x3fff;
	length |= g.input.data[g.input.rpos + 2] << 14;
	g.input.rpos += 3;
	return length;
}

static int
tight_basic(void)
{
	int length;

	switch (tight.filter) {
	case 0: /* copy */
	case 2: /* gradient */
		length = tight.bpp * g.w * g.h;
		break;
	case 1: /* palette */
		if (tight.palette_size <= 2)
			length = (g.w + 7) / 8 * g.h;
		else
			length = g.w * g.h;
		break;
	default:
		exit(1);
	}

	if (length >= 12) {
		tight.length = tight_length();
		if (!~tight.length) {
			g.action = tight_basic;
			return 0;
		}
		return tight_inflate();
	}

	if (g.input.wpos < g.input.rpos + length) {
		g.action = tight_basic;
		return 0;
	}

	tight.length = 0;
	memcpy(g.work.data, &g.input.data[g.input.rpos], length);
	g.input.rpos += length;
	g.work.wpos = length;
	g.work.rpos = 0;

	switch (tight.filter) {
	case 0:
		return tight.copy();
	case 1:
		if (tight.palette_size <= 2)
			return tight_bit_palette();
		else
			return tight_byte_palette();
	case 2:
		return tight.gradient();
	default:
		exit(1);
	}

	/* silence compiler */
	return 0;
}

static int
tight_palette_8(void)
{
	int i;

	debug(3, "tight_palette_8\n");

	if (g.input.wpos < g.input.rpos + 1) {
		g.action = tight.pal;
		return 0;
	}
	tight.palette_size = g.input.data[g.input.rpos] + 1;

	if (g.input.wpos < g.input.rpos + 1 + tight.palette_size) {
		g.action = tight.pal;
		return 0;
	}

	for (i = 0; i < tight.palette_size; ++i)
		tight.palette[i] = g.input.data[++g.input.rpos];
	++g.input.rpos;

	return tight_basic();
}

static int
tight_palette_16(void)
{
	int i;

	debug(3, "tight_palette_16\n");

	if (g.input.wpos < g.input.rpos + 1) {
		g.action = tight.pal;
		return 0;
	}
	tight.palette_size = g.input.data[g.input.rpos] + 1;

	if (g.input.wpos < g.input.rpos + 1 + 2 * tight.palette_size) {
		g.action = tight.pal;
		return 0;
	}

	++g.input.rpos;
	if (g.wire_endian != g.local_endian) {
		for (i = 0; i < tight.palette_size; ++i) {
			tight.palette[i] =
				get16_r(&g.input.data[g.input.rpos]);
			g.input.rpos += 2;
		}
	}
	else {
		for (i = 0; i < tight.palette_size; ++i) {
			tight.palette[i] = get16(&g.input.data[g.input.rpos]);
			g.input.rpos += 2;
		}
	}

	return tight_basic();
}

static int
tight_palette_888(void)
{
	int i;

	debug(3, "tight_palette_888\n");

	if (g.input.wpos < g.input.rpos + 1) {
		g.action = tight.pal;
		return 0;
	}
	tight.palette_size = g.input.data[g.input.rpos] + 1;

	if (g.input.wpos < g.input.rpos + 1 + 3 * tight.palette_size) {
		g.action = tight.pal;
		return 0;
	}

	++g.input.rpos;
	for (i = 0; i < tight.palette_size; ++i) {
#ifdef GGI_BIG_ENDIAN
		/* 24-bit modes are always little endian in GGI */
		tight.palette[i] =
			get24ll_r(&g.input.data[g.input.rpos]);
#else
		tight.palette[i] =
			get24ll(&g.input.data[g.input.rpos]);
#endif
		g.input.rpos += 3;
	}

	return tight_basic();
}

static int
tight_palette_32(void)
{
	int i;

	debug(3, "tight_palette_32\n");

	if (g.input.wpos < g.input.rpos + 1) {
		g.action = tight.pal;
		return 0;
	}
	tight.palette_size = g.input.data[g.input.rpos] + 1;

	if (g.input.wpos < g.input.rpos + 1 + 4 * tight.palette_size) {
		g.action = tight.pal;
		return 0;
	}

	++g.input.rpos;
	if (g.wire_endian != g.local_endian) {
		for (i = 0; i < tight.palette_size; ++i) {
			tight.palette[i] =
				get32_r(&g.input.data[g.input.rpos]);
			g.input.rpos += 4;
		}
	}
	else {
		for (i = 0; i < tight.palette_size; ++i) {
			tight.palette[i] = get32(&g.input.data[g.input.rpos]);
			g.input.rpos += 4;
		}
	}

	return tight_basic();
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
tight_gradient_8(void)
{
	debug(1, "gradient filter on 8-bit modes is not allowed\n");
	exit(1);
	return 0;
}

static int
tight_gradient_16(void)
{
	int x, y;
	uint16_t *buf = (uint16_t *)&g.work.data[g.work.rpos];
	uint16_t *prev;
	int length = 2 * g.w * g.h;
	uint16_t pixel;
	const ggi_pixelformat *pixfmt;

	debug(3, "tight_gradient_16\n");

	if (g.work.wpos < g.work.rpos + length) {
		g.action = tight_inflate;
		return 0;
	}

	pixfmt = ggiGetPixelFormat(tight.stem);

	/* first row */
	/* first pixel, nothing to do */
	/* rest of row */
	for (x = 1; x < g.w; ++x) {
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
	buf += g.w;

	/* following rows */
	for (y = 1; y < g.h; ++y) {
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
		for (x = 1; x < g.w; ++x) {
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
		buf += g.w;
	}

	ggiPutBox(tight.stem, g.x, g.y, g.w, g.h, g.work.data + g.work.rpos);
	g.work.rpos = 0;
	g.work.wpos = 0;

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_gradient_888(void)
{
	int x, y;
	uint8_t *buf = &g.work.data[g.work.rpos];
	uint8_t *prev;
	int length = 3 * g.w * g.h;
	int xs;

	debug(3, "tight_gradient_888\n");

	if (g.work.wpos < g.work.rpos + length) {
		g.action = tight_inflate;
		return 0;
	}

	xs = g.w * 3;

	/* first row */
	/* first pixel, nothing to do */
	/* rest of row */
	for (x = 3; x < xs; ++x)
		buf[x] += buf[x - 3];

	prev = buf;
	buf += xs;

	/* following rows */
	for (y = 1; y < g.h; ++y) {
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

	ggiPutBox(tight.stem, g.x, g.y, g.w, g.h, g.work.data + g.work.rpos);
	g.work.rpos = 0;
	g.work.wpos = 0;

	ggiCrossBlit(tight.stem, g.x, g.y, g.w, g.h,
		tight.xblt_stem, g.x, g.y);

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_gradient_32(void)
{
	int x, y;
	uint32_t *buf = (uint32_t *)&g.work.data[g.work.rpos];
	uint32_t *prev;
	int length = 4 * g.w * g.h;
	uint32_t pixel;
	const ggi_pixelformat *pixfmt;

	debug(3, "tight_gradient_32\n");

	if (g.work.wpos < g.work.rpos + length) {
		g.action = tight_inflate;
		return 0;
	}

	pixfmt = ggiGetPixelFormat(tight.stem);

	/* first row */
	/* first pixel, nothing to do */
	/* rest of row */
	for (x = 1; x < g.w; ++x) {
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
	buf += g.w;

	/* following rows */
	for (y = 1; y < g.h; ++y) {
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
		for (x = 1; x < g.w; ++x) {
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
		buf += g.w;
	}

	ggiPutBox(tight.stem, g.x, g.y, g.w, g.h, g.work.data + g.work.rpos);
	g.work.rpos = 0;
	g.work.wpos = 0;

	if (tight.length)
		return tight_drain_inflate();

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
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
tight_jpeg_8(void)
{
	debug(1, "jpeg on 8-bit modes is not allowed\n");
	exit(1);
	return 0;
}

static int
tight_jpeg_16(void)
{
	int rpos = g.input.rpos;
	uint16_t *buf16;
	uint8_t *buf888;
	int length;

	debug(3, "tight_jpeg_16\n");

	length = tight_length();
	if (!~length) {
		g.action = tight.jpeg;
		return 0;
	}

	if (g.input.wpos < g.input.rpos + length) {
		g.input.rpos = rpos; /* step back */
		g.action = tight.jpeg;
		return 0;
	}

	if (tight.xblt.size < (2 + 3) * g.w) {
		uint8_t *tmp = realloc(tight.xblt.data, (2 + 3) * g.w);
		if (!tmp)
			exit(1);
		tight.xblt.data = tmp;
		tight.xblt.size = (2 + 3) * g.w;
	}

	buf16 = (uint16_t *)tight.xblt.data;
	buf888 = tight.xblt.data + 2 * g.w;

	jpeg_read_header(&tight.cinfo, TRUE);
	tight.cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&tight.cinfo);

	if (tight.cinfo.output_width != g.w ||
		tight.cinfo.output_height != g.h ||
		tight.cinfo.out_color_components != 3)
	{
		debug(1, "jpeg rect mismatch\n");
		exit(1);
	}

	while (tight.cinfo.output_scanline < tight.cinfo.output_height) {
		int y = tight.cinfo.output_scanline;
		jpeg_read_scanlines(&tight.cinfo, &buf888, 1);
		crossblit_row_16(ggiGetPixelFormat(tight.stem),
			buf16, buf888, g.w);
		ggiPutHLine(tight.stem, g.x, g.y + y, g.w, buf16);
	}

	jpeg_finish_decompress(&tight.cinfo);

	g.input.rpos += length;

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_jpeg_888(void)
{
	int rpos = g.input.rpos;
	const ggi_directbuffer *db;
	uint8_t *scanlines;
	int length;

	debug(3, "tight_jpeg_888\n");

	length = tight_length();
	if (!~length) {
		g.action = tight.jpeg;
		return 0;
	}

	if (g.input.wpos < g.input.rpos + length) {
		g.input.rpos = rpos; /* step back */
		g.action = tight.jpeg;
		return 0;
	}

	db = ggiDBGetBuffer(tight.stem, 0);
	ggiResourceAcquire(db->resource, GGI_ACTYPE_WRITE);

	jpeg_read_header(&tight.cinfo, TRUE);
	tight.cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&tight.cinfo);

	if (tight.cinfo.output_width != g.w ||
		tight.cinfo.output_height != g.h ||
		tight.cinfo.out_color_components != 3)
	{
		debug(1, "jpeg rect mismatch\n");
		exit(1);
	}

	while (tight.cinfo.output_scanline < tight.cinfo.output_height) {
		scanlines = (uint8_t *)db->write +
			(g.y + tight.cinfo.output_scanline) *
			 db->buffer.plb.stride + 3 * g.x;
		jpeg_read_scanlines(&tight.cinfo, &scanlines, 1);
	}

	jpeg_finish_decompress(&tight.cinfo);

	ggiResourceRelease(db->resource);

	g.input.rpos += length;

	ggiCrossBlit(tight.stem, g.x, g.y, g.w, g.h,
		tight.xblt_stem, g.x, g.y);

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

static int
tight_jpeg_32(void)
{
	int rpos = g.input.rpos;
	uint32_t *buf32;
	uint8_t *buf888;
	int length;

	debug(3, "tight_jpeg_32\n");

	length = tight_length();
	if (!~length) {
		g.action = tight.jpeg;
		return 0;
	}

	if (g.input.wpos < g.input.rpos + length) {
		g.input.rpos = rpos; /* step back */
		g.action = tight.jpeg;
		return 0;
	}

	if (tight.xblt.size < (4 + 3) * g.w) {
		uint8_t *tmp = realloc(tight.xblt.data, (4 + 3) * g.w);
		if (!tmp)
			exit(1);
		tight.xblt.data = tmp;
		tight.xblt.size = (4 + 3) * g.w;
	}

	buf32 = (uint32_t *)tight.xblt.data;
	buf888 = tight.xblt.data + 4 * g.w;

	jpeg_read_header(&tight.cinfo, TRUE);
	tight.cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&tight.cinfo);

	if (tight.cinfo.output_width != g.w ||
		tight.cinfo.output_height != g.h ||
		tight.cinfo.out_color_components != 3)
	{
		debug(1, "jpeg rect mismatch\n");
		exit(1);
	}

	while (tight.cinfo.output_scanline < tight.cinfo.output_height) {
		int y = tight.cinfo.output_scanline;
		jpeg_read_scanlines(&tight.cinfo, &buf888, 1);
		crossblit_row_32(ggiGetPixelFormat(tight.stem),
			buf32, buf888, g.w);
		ggiPutHLine(tight.stem, g.x, g.y + y, g.w, buf32);
	}

	jpeg_finish_decompress(&tight.cinfo);

	g.input.rpos += length;

	--g.rects;

	remove_dead_data();
	g.action = vnc_update_rect;
	return 1;
}

#else

static int
tight_jpeg(void)
{
	debug(1, "unexpected jpeg request\n");
	exit(1);
	return 0;
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
tight_stem_888(void)
{
	ggi_visual_t stem;
	ggi_mode mode;

	memset(&mode, 0, sizeof(mode));
	mode.frames = 1;
	mode.visible.x = g.width;
	mode.visible.y = g.height;
	mode.virt.x    = g.width;
	mode.virt.y    = g.height;
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

int
vnc_tight(void)
{
	const ggi_pixelformat *pf;

	debug(2, "tight\n");

	if (g.input.wpos < g.input.rpos + 1)
		return 0;

	tight.control = g.input.data[g.input.rpos];

	if ((tight.control & 0xc0) == 0x40) {
		/* basic with filter */
		if (g.input.wpos < g.input.rpos + 2)
			return 0;
		tight.filter = g.input.data[++g.input.rpos];
	}
	else
		tight.filter = 0;

	++g.input.rpos;

	if (tight.control & 0x01)
		tight_reset_ztrm(&tight.ztrm[0]);
	if (tight.control & 0x02)
		tight_reset_ztrm(&tight.ztrm[1]);
	if (tight.control & 0x04)
		tight_reset_ztrm(&tight.ztrm[2]);
	if (tight.control & 0x08)
		tight_reset_ztrm(&tight.ztrm[3]);
	tight.control &= 0xf0;

	tight.stem = g.wire_stem ? g.wire_stem : g.stem;
	g.stem_change = tight_stem_change;

	switch (GT_SIZE(g.wire_mode.graphtype)) {
	case  8:
		tight.bpp           = 1;
		tight.fill          = tight_fill_8;
		tight.jpeg          = tight_jpeg_8;
		tight.copy          = tight_copy_8;
		tight.pal           = tight_palette_8;
		tight.gradient      = tight_gradient_8;
		break;
	case 16:
		tight.bpp           = 2;
		tight.fill          = tight_fill_16;
		tight.jpeg          = tight_jpeg_16;
		tight.copy          = tight_copy_16;
		tight.pal           = tight_palette_16;
		tight.gradient      = tight_gradient_16;
		break;
	case 32:
		pf = ggiGetPixelFormat(tight.stem);
		if (((pf->red_mask   << pf->red_shift)   == 0xff000000) &&
		    ((pf->green_mask << pf->green_shift) == 0xff000000) &&
		    ((pf->blue_mask  << pf->blue_shift)  == 0xff000000))
		{
			if (!tight.wire_stem) {
				tight.wire_stem = tight_stem_888();
				if (!tight.wire_stem)
					exit(1);
			}
			tight.xblt_stem = tight.stem;
			tight.stem = tight.wire_stem;

			tight.bpp           = 3;
			tight.fill          = tight_fill_888;
			tight.jpeg          = tight_jpeg_888;
			tight.copy          = tight_copy_888;
			tight.pal           = tight_palette_888;
			tight.gradient      = tight_gradient_888;
			break;
		}
		tight.bpp           = 4;
		tight.fill          = tight_fill_32;
		tight.jpeg          = tight_jpeg_32;
		tight.copy          = tight_copy_32;
		tight.pal           = tight_palette_32;
		tight.gradient      = tight_gradient_32;
		break;
	}

	if (tight.control == 0x80)
		return tight.fill();
	if (tight.control == 0x90)
		return tight.jpeg();
	if (tight.control & 0x80)
		exit(1);

	if (tight.filter == 1)
		return tight.pal();

	return tight_basic();
}

int
vnc_tight_init(void)
{
	tight.xblt.data = (uint8_t *)malloc((4 + 3) * 2048);
	if (!tight.xblt.data)
		return -1;

	tight.xblt.rpos = 0;
	tight.xblt.wpos = 0;
	tight.xblt.size = (4 + 3) * 2048;

	tight_init_ztrm(&tight.ztrm[0]);
	tight_init_ztrm(&tight.ztrm[1]);
	tight_init_ztrm(&tight.ztrm[2]);
	tight_init_ztrm(&tight.ztrm[3]);

#ifdef HAVE_JPEG
	tight.cinfo.client_data = &tight;
	tight.cinfo.err = jpeg_std_error(&tight.jerr);
	jpeg_create_decompress(&tight.cinfo);
	buffer_src(&tight.cinfo);
#endif

	return 0;
}
