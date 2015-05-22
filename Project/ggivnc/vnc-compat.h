/*
******************************************************************************

   VNC viewer compatibility stuff.

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

#ifndef VNC_COMPAT_H
#define VNC_COMPAT_H

#if !defined(HAVE_WIDGETS) && !defined(HAVE_GETPASS)
char *getpass(const char *prompt);
#endif

#if !defined(HAVE_STRERROR)
const char *strerror(int errnum);
#endif /* HAVE_STRERROR */

#ifndef HAVE_WIDGETS
#define show_about(cx) 0
#define show_menu(cx) 0
#define show_xvp_menu(cx) 0
#define show_file_transfer(cx) 0
#define file_upload_fragment(cx) 0
#define show_reconnect(cx, popup) ((cx)->auto_reconnect)
#endif

#ifndef HAVE_ZLIB
#define vnc_tight          vnc_unexpected
#define vnc_zlib           vnc_unexpected
#define vnc_zlibhex        vnc_unexpected
#define vnc_zrle           vnc_unexpected
#endif
#ifndef HAVE_WMH
#define vnc_desktop_name   vnc_unexpected
#endif
#ifndef HAVE_GGNEWSTEM
#define gii_inject(cx, event) 0
#define gii_receive        vnc_unexpected
#endif /* HAVE_GGNEWSTEM */
#ifndef HAVE_WIDGETS
#define xvp_shutdown(cx) 0
#define xvp_reboot(cx) 0
#define xvp_reset(cx) 0
#define xvp_receive        vnc_unexpected
#endif

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
#define getopt_long      vnc_getopt_long
#define getopt_long_only vnc_getopt_long_only
#define optarg           vnc_optarg
#define optind           vnc_optind
#define opterr           vnc_opterr
#define optopt           vnc_optopt
#include "lib/getopt.h"
#endif

#ifndef HAVE_SNPRINTF
#ifdef HAVE__SNPRINTF
#define snprintf _snprintf
#else
#define snprintf vnc_snprintf
int snprintf(char *str, size_t count, const char *fmt, ...);
#endif
#endif /* HAVE_SNPRINTF */

#ifndef HAVE_VSNPRINTF
#ifdef HAVE__VSNPRINTF
#define vsnprintf _vsnprintf
#else
#include <stdarg.h>	/* for va_list */
#define vsnprintf vnc_vsnprintf
int vsnprintf(char *str, size_t count, const char *fmt, va_list args);
#endif
#endif /* HAVE_VSNPRINTF */

#ifndef HAVE_GGNEWSTEM
/* GGI 2.x doesn't clip source visual */
#define ggiCrossBlit vnc_cross_blit
int
ggiCrossBlit(ggi_visual_t src, int sx, int sy, int w, int h,
	ggi_visual_t dst, int dx, int dy);
/* GII 1.x doesn't have input-fdselect, emulate it */
#define giiEventPoll vnc_event_poll
int
giiEventPoll(ggi_visual_t vis, gii_event_mask mask, struct timeval *tv);
struct gii_fdselect_fd {
	int fd;
	int mode;
};
#define GII_FDSELECT_READY 1
#define GII_FDSELECT_READ  1
#define GII_FDSELECT_WRITE 2
#define GII_FDSELECT_ADD   2
#define GII_FDSELECT_DEL   3
#define giiEventsQueued ggiEventsQueued
#define giiEventRead    ggiEventRead
#define gg_instance     vnc_gg_instance
typedef int (observe_cb)(void *arg, uint32_t msg, void *data);
struct gg_instance {
	void *channel;
	observe_cb *cb;
	void *arg;
};
#define ggPlugModule    vnc_plug_module
struct gg_instance *
ggPlugModule(void *api, ggi_visual_t stem, const char *name,
	const char *argstr, void *argptr);
#define libgii NULL
#define ggControl       vnc_control
void
ggControl(void *channel, uint32_t code, void *arg);
#define ggObserve       vnc_observe
void
ggObserve(void *channel, observe_cb *cb, void *arg);
#define ggClosePlugin   vnc_close_plugin
void
ggClosePlugin(struct gg_instance *instance);
#endif /* HAVE_GGNEWSTEM */

#ifndef GWT_AF_BACKGROUND
#define GWT_AF_BACKGROUND (0)
#endif

#if defined _MSC_VER && _MSC_VER <= 1200
#define intptr_t int
#endif

#ifdef _WIN32

#ifdef read
#undef read
#endif
#define read(fd, buf, size) recv((fd), (char *)(buf), (size), 0)
#ifdef write
#undef write
#endif
#define write(fd, buf, size) send((fd), (const char *)(buf), (size), 0)

static inline void
vnc_FD_SET(SOCKET fd, fd_set *set)
{
	FD_SET(fd, set);
}
#undef FD_SET
#define FD_SET(fd, set) vnc_FD_SET((fd), (set))

#else

#define set_icon()
#define socket_init() 0
#define socket_cleanup()
#define console_init()

#endif /* _WIN32 */

#ifndef HAVE_GETADDRINFO

#ifndef _WIN32
#define EAI_BADFLAGS   -1
#define EAI_NONAME     -2
#define EAI_AGAIN      -3
#define EAI_FAIL       -4
#define EAI_NODATA     -5
#define EAI_FAMILY     -6
#define EAI_SOCKTYPE   -7
#define EAI_SERVICE    -8
#define EAI_ADDRFAMILY -9
#define EAI_MEMORY     -10
#define EAI_SYSTEM     -11

#ifdef addrinfo
#undef addrinfo
#endif
#define addrinfo vnc_addrinfo
struct addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	size_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

/* getaddrinfo flags */
#define AI_PASSIVE	1
#define AI_CANONNAME	2
#define AI_NUMERICHOST	4

#else /* _WIN32 */

struct addrinfo;

#endif /* _WIN32 */

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV	16
#endif
/* valid flags for getaddrinfo */
#ifndef AI_MASK
#define AI_MASK (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV)
#endif

#ifdef getaddrinfo
#undef getaddrinfo
#endif
#define getaddrinfo vnc_getaddrinfo
int getaddrinfo(const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **res);
#ifdef freeaddrinfo
#undef freeaddrinfo
#endif
#define freeaddrinfo vnc_freeaddrinfo
void freeaddrinfo(struct addrinfo *res);

#endif /* HAVE_GETADDRINFO */

#ifndef HAVE_OPENSSL
#include "lib/d3des.h"
#define DES_key_schedule int
#define DES_cblock uint8_t
#define DES_set_key_unchecked(key, ks) deskey((key), *(ks) = 0)
#define DES_ecb_encrypt(in, out, ks, flag) des((in), (out))
#define vnc_security_vencrypt            vnc_unexpected
#define vnc_security_vencrypt_end(cx, keep_opt) do {} while(0)
#endif /* HAVE_OPENSSL */

#if !defined _WIN32 || !defined HAVE_SHLOBJ_H
#define get_appdata_path() NULL
#endif /* !_WIN32 || !HAVE_SHLOBJ_H */

#ifdef HAVE_MKDIR
#ifndef HAVE_TWO_ARG_MKDIR
#define mkdir(path, mode) mkdir(path)
#endif /* HAVE_TWO_ARG_MKDIR */
#else /* HAVE_MKDIR */
#ifdef HAVE__MKDIR
#define mkdir(path, mode) _mkdir(path)
#endif /* HAVE__MKDIR */
#endif /* HAVE_MKDIR */

#ifdef _WIN32
#define rename vnc_rename
int rename(const char *old_path, const char *new_path);
#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING (1)
#endif
#endif /* _WIN32 */

#ifndef HAVE_MKSTEMP
#define mkstemp vnc_mkstemp
int mkstemp(char *template);
#endif /* HAVE_MKSTEMP */

#ifndef S_IREAD
#define S_IREAD 0
#endif
#ifndef S_IWRITE
#define S_IWRITE 0
#endif

#ifndef HAVE_ICONV
#define iconv_t     vnc_iconv_t
#define iconv_open  vnc_iconv_open
#define iconv       vnc_iconv
#define iconv_close vnc_iconv_close
typedef struct iconv_t *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t iconv(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft);
int iconv_close(iconv_t cd);
#endif /* HAVE_ICONV */

#endif /* VNC_COMPAT_H */
