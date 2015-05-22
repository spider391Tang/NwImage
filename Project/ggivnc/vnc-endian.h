/*
******************************************************************************

   VNC viewer endian handling.

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

#ifndef VNC_ENDIAN_H
#define VNC_ENDIAN_H

#include <ggi/ggi.h>

static inline uint16_t
get16(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return (*buf << 8) | *(buf + 1);
#else
	return (*(buf + 1) << 8) | *buf;
#endif
}

static inline uint16_t
get16_r(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return (*(buf + 1) << 8) | *buf;
#else
	return (*buf << 8) | *(buf + 1);
#endif
}

static inline uint16_t
get16_hilo(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return get16(buf);
#else
	return get16_r(buf);
#endif
}

/* _b_ig endian on wire, _l_ow three bytes */
static inline uint32_t
get24bl(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return
		(* buf      << 16) |
		(*(buf + 1) <<  8) |
		 *(buf + 2);
#else
	return
		(*(buf + 2) << 24) |
		(*(buf + 1) << 16) |
		(* buf      <<  8);
#endif
}

static inline uint32_t
get24bl_r(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return
		(*(buf + 2) << 24) |
		(*(buf + 1) << 16) |
		(* buf      <<  8);
#else
	return
		(* buf      << 16) |
		(*(buf + 1) <<  8) |
		 *(buf + 2);
#endif
}

/* _b_ig endian on wire, _h_igh three bytes */
static inline uint32_t
get24bh(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return
		(* buf      << 24) |
		(*(buf + 1) << 16) |
		(*(buf + 2) <<  8);
#else
	return
		(*(buf + 2) << 16) |
		(*(buf + 1) <<  8) |
		 * buf;
#endif
}

static inline uint32_t
get24bh_r(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return
		(*(buf + 2) << 16) |
		(*(buf + 1) <<  8) |
		 * buf;
#else
	return
		(* buf      << 24) |
		(*(buf + 1) << 16) |
		(*(buf + 2) <<  8);
#endif
}

/* _l_ittle endian on wire, _h_igh three bytes */
#define get24lh   get24bl
#define get24lh_r get24bl_r

/* _l_ittle endian on wire, _l_ow three bytes */
#define get24ll   get24bh
#define get24ll_r get24bh_r

static inline uint32_t
get32(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return
		(* buf      << 24) |
		(*(buf + 1) << 16) |
		(*(buf + 2) <<  8) |
		 *(buf + 3);
#else
	return
		(*(buf + 3) << 24) |
		(*(buf + 2) << 16) |
		(*(buf + 1) <<  8) |
		 * buf;
#endif
}

static inline uint32_t
get32_r(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return
		(*(buf + 3) << 24) |
		(*(buf + 2) << 16) |
		(*(buf + 1) <<  8) |
		 * buf;
#else
	return
		(* buf      << 24) |
		(*(buf + 1) << 16) |
		(*(buf + 2) <<  8) |
		 *(buf + 3);
#endif
}

static inline uint32_t
get32_hilo(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return get32(buf);
#else
	return get32_r(buf);
#endif
}

static inline uint32_t
get32_lohi(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return get32_r(buf);
#else
	return get32(buf);
#endif
}

#ifdef GG_HAVE_INT64
static inline uint64_t
get64(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return ((uint64_t)get32(buf)) << 32 | (get32(buf + 4));
#else
	return ((uint64_t)get32(buf + 4)) << 32 | (get32(buf));
#endif
}

static inline uint64_t
get64_r(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return ((uint64_t)get32_r(buf + 4)) << 32 | (get32_r(buf));
#else
	return ((uint64_t)get32_r(buf)) << 32 | (get32_r(buf + 4));
#endif
}

static inline uint64_t
get64_hilo(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return get64(buf);
#else
	return get64_r(buf);
#endif
}

static inline uint64_t
get64_lohi(const uint8_t *buf)
{
#ifdef GGI_BIG_ENDIAN
	return get64_r(buf);
#else
	return get64(buf);
#endif
}
#endif /* GG_HAVE_INT64 */

static inline void
buffer_reverse_16(uint8_t *buf8, uint32_t count)
{
	uint16_t *buf = (uint16_t *)buf8;
	count >>= 1;

	for (; count--; ++buf)
		*buf = GGI_BYTEREV16(*buf);
}

static inline void
buffer_reverse_32(uint8_t *buf8, uint32_t count)
{
	uint32_t *buf = (uint32_t *)buf8;
	count >>= 2;

	for (; count--; ++buf)
		*buf = GGI_BYTEREV32(*buf);
}

static inline uint8_t *
insert16_hilo(uint8_t *dst, uint16_t value)
{
	*dst++ = value >> 8;
	*dst++ = value;
	return dst;
}

static inline uint8_t *
insert32_hilo(uint8_t *dst, uint32_t value)
{
	*dst++ = value >> 24;
	*dst++ = value >> 16;
	*dst++ = value >> 8;
	*dst++ = value;
	return dst;
}

#ifdef GG_HAVE_INT64
static inline uint8_t *
insert64_hilo(uint8_t *dst, uint64_t value)
{
	insert32_hilo(dst, value >> 32);
	return insert32_hilo(dst + 4, value);
}
#endif

static inline uint8_t *
insert16_lohi(uint8_t *dst, uint16_t value)
{
	*dst++ = value;
	*dst++ = value >> 8;
	return dst;
}

static inline uint8_t *
insert32_lohi(uint8_t *dst, uint32_t value)
{
	*dst++ = value;
	*dst++ = value >> 8;
	*dst++ = value >> 16;
	*dst++ = value >> 24;
	return dst;
}

#ifdef GG_HAVE_INT64
static inline uint8_t *
insert64_lohi(uint8_t *dst, uint64_t value)
{
	insert32_lohi(dst, value);
	return insert32_lohi(dst + 4, value >> 32);
}
#endif

#endif /* VNC_ENDIAN_H */
