/*
******************************************************************************

   VNC viewer common stuff.

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

#ifndef VNC_H
#define VNC_H

#include <ggi/gg.h>
#ifdef HAVE_GGI_GII_EVENTS_H
#include <ggi/gii-events.h>
#endif
#include <ggi/ggi.h>

#ifdef HAVE_GGNEWSTEM
typedef struct device_list {
	GG_LIST_ENTRY(device_list) others;
	uint32_t local_origin;
	uint32_t remote_origin;
} gii_device;
#endif /* HAVE_GGNEWSTEM */

struct bw_sample {
	double count;
	double interval;
};

struct bw_history {
	struct bw_sample sample[16];
	struct bw_sample total;
	int index;
};

struct bandwidth {
	int counting;
	int count;
	struct timeval start;
	int estimate;
};

struct buffer {
	uint8_t *data;
	int size;
	int wpos;
	int rpos;
};

typedef int (action_t)(void);

struct globals {
	ggi_visual_t stem;
	const char *gii_input;
	void (*flush_hook)(void *data);
	void (*post_flush_hook)(void *data);
	void *flush_hook_data;
	char *server;
	const char *passwd;
	int port;
	uint8_t shared;
#ifdef _WIN32
	SOCKET sfd;
#else
	int sfd;
#endif
	int net_family;
	int listen;
	char *bind;
	int protocol;
	unsigned int max_protocol;
	uint8_t security;
	uint8_t *allow_security;
	int security_tight;
	char *name;
	char server_pixfmt[30];
	char local_pixfmt[30];
	char wire_pixfmt[30];
	int server_endian;
	int local_endian;
	int wire_endian;
	ggi_mode mode;
	uint16_t width;
	uint16_t height;
	ggi_coord offset;
	ggi_coord area;
	ggi_coord slide;
	struct buffer input;
	struct buffer output;
	struct buffer work;
	action_t *action;
	uint16_t rects;
	uint16_t x, y, w, h;
	int desktop_size;
	ggi_visual_t wire_stem;
	ggi_mode wire_mode;
	int wire_stem_flags;
	void (*stem_change)(void);
	int no_input;
	int auto_encoding;
	uint16_t encoding_count;
	const int32_t *encoding;
	int scrollx;
	int scrolly;
	void *scroll;
	double sx;
	double sy;
	int compression;
	double compression_level;
	int quality;
	double quality_level;
	int debug;
	int F8_allow_release;
	int gii;
#ifdef HAVE_GGNEWSTEM
	GG_LIST_HEAD(devices, device_list) devices;
#endif /* HAVE_GGNEWSTEM */
#ifdef HAVE_INPUT_FDSELECT
	struct gg_instance *fdselect;
#endif /* HAVE_INPUT_FDSELECT */

	struct bandwidth bw;
	struct bw_history history;

	void *visualanchor;
};

extern struct globals g;

#define device_FOREACH(device) \
	GG_LIST_FOREACH(device, &g.devices, others)
#define device_FIRST() \
	GG_LIST_FIRST(&g.devices)
#define device_NEXT(device) \
	GG_LIST_NEXT(device, others)
#define device_INSERT(device) \
	GG_LIST_INSERT_HEAD(&g.devices, device, others)
#define device_REMOVE(device) \
	GG_LIST_REMOVE(device, others)

#if !defined(HAVE_WIDGETS) && !defined(HAVE_GETPASS)
char *getpass(const char *prompt);
#endif

#if !defined(HAVE_GETADDRINFO) && !defined(HAVE_HSTRERROR)
#ifndef h_errno
#define h_errno 0
#endif
const char *hstrerror(int errnum);
#endif /* HAVE_HSTRERROR */

#if !defined(HAVE_STRERROR)
const char *strerror(int errnum);
#endif /* HAVE_STRERROR */

#ifdef HAVE_WIDGETS
int show_about(void);
int show_menu(void);
#else
#define show_about() 0
#define show_menu() 0
#endif

int safe_write(int fd, const void *buf, int count);
void select_mode(void);
int parse_port(void);
int open_visual(void);
void close_visual(void);
int set_title(void);
int get_password(void);
int get_connection(void);
int canonicalize_pixfmt(char *pixfmt, int count);
const char *lookup_encoding(int32_t number);
int get_default_encodings(const int32_t **encodings);
int color_bits(uint16_t max);
int generate_pixfmt(char *pixfmt, int count, const ggi_pixelformat *ggi_pf);
int wire_mode_switch(const char *pixfmt, int endian, ggi_coord wire_size);

void remove_dead_data(void);
int vnc_update_request(int incremental);
int vnc_set_encodings(void);
int vnc_update_rect(void);
int vnc_wait(void);

int vnc_raw(void);
int vnc_copyrect(void);
int vnc_rre(void);
int vnc_corre(void);
int vnc_hextile(void);
int vnc_hextile_size(int bpp);
#ifdef HAVE_ZLIB
int vnc_tight_init(void);
int vnc_tight(void);
int vnc_zlib_init(void);
int vnc_zlib(void);
int vnc_zlibhex_init(void);
int vnc_zlibhex(void);
int vnc_zrle_init(void);
int vnc_zrle(void);
#else
#define VNC_NOT_IMPL
#define vnc_tight_init()   0
#define vnc_tight          vnc_not_implemented
#define vnc_zlib_init()    0
#define vnc_zlib           vnc_not_implemented
#define vnc_zlibhex_init() 0
#define vnc_zlibhex        vnc_not_implemented
#define vnc_zrle_init()    0
#define vnc_zrle           vnc_not_implemented
#endif
int vnc_lastrect(void);
int vnc_wmvi(void);
int vnc_desktop_size(void);
#ifdef HAVE_WMH
int vnc_desktop_name(void);
#else
#ifndef VNC_NOT_IMPL
#define VNC_NOT_IMPL
#endif
#define vnc_desktop_name   vnc_not_implemented
#endif /* HAVE_WMH */
#ifdef HAVE_GGNEWSTEM
int gii_inject(gii_event *ev);
int gii_create_device(uint32_t origin, struct gii_cmddata_devinfo *dev);
int gii_delete_device(gii_event *ev);
int gii_receive(void);
#else
#define gii_inject(event) 0
#endif

void bandwidth_start(ssize_t len);
void bandwidth_update(ssize_t len);
void bandwidth_end(void);

#ifndef HAVE_GAI_STRERROR
#ifdef gai_strerror
#undef gai_strerror
#endif
#define gai_strerror vnc_gai_strerror
const char *gai_strerror(int error);
#endif

#if defined(HAVE_GETOPT_H) && defined(HAVE_GETOPT_LONG_ONLY)
#include <getopt.h>
#elif !defined(HAVE_GETOPT_LONG_ONLY)
#define getopt           vnc_getopt
#define getopt_long      vnc_genopt_long
#define getopt_long_only vnc_getopt_long_only
#define optarg           vnc_optarg
#define optind           vnc_optind
#define opterr           vnc_opterr
#define optopt           vnc_optopt
#include "lib/getopt.h"
#endif

#ifndef HAVE_GGNEWSTEM
/* GGI 2.x doesn't clip source visual */
//#define ggiCrossBlit vnc_cross_blit
//int
//ggiCrossBlit(ggi_visual_t src, int sx, int sy, int w, int h,
//	ggi_visual_t dst, int dx, int dy);
#endif /* HAVE_GGNEWSTEM */

#if _MSC_VER <= 1200
//#define intptr_t int
#endif


#endif /* VNC_H */

