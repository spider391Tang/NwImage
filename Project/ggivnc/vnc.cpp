/*
******************************************************************************

   VNC viewer using the RFB protocol.

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
#include <ctype.h>
#include <errno.h>

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
#include <ggi/ggi-unix.h>
/* compatibility cruft */
struct gii_fdselect_fd {
	int fd;
	int mode;
};
#define GII_FDSELECT_READY 1
#define GII_FDSELECT_READ  1
#define GII_FDSELECT_WRITE 2
#endif


extern "C" {
#include "d3des.h"
#include "vnc.h"
}

#include "vnc-endian.h"
#include "vnc-debug.h"
#include "scrollbar.h"

#include <QDebug>
#include <QImage>
#include "../MLVNC/flyggi.h"
#include "../MLVNC/MLVNC.h"

#include <boost/signals2/signal.hpp>
#include <boost/signals2/connection.hpp>
#include <boost/function.hpp>

typedef boost::signals2::signal <void( const std::string& )> TestSignalType;
TestSignalType mTestEvent;

struct globals g;

#ifdef _WIN32
#include "resource.h"

static BOOL CALLBACK
locate_wnd_callback(HWND wnd, LPARAM param)
{
	DWORD pid;
	HICON icon;

	GetWindowThreadProcessId(wnd, &pid);
	if(pid != GetCurrentProcessId())
		return TRUE;

#ifndef HAVE_WMH
	SetWindowText(wnd, "ggivnc");
#endif /* HAVE_WMH */

	icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_GGIVNC));
	if (icon)
		SendMessage(wnd, WM_SETICON, ICON_BIG, (LPARAM)icon);

	icon = (HICON)LoadImage(GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDI_GGIVNC), IMAGE_ICON, 16, 16, 0);
	if (icon)
		SendMessage(wnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

	return TRUE;
}

static void
set_icon(void)
{
	/* Try to locate the window, if any... */
	EnumWindows(locate_wnd_callback, 0);
}

static int
socket_init(void)
{
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;

	wVersionRequested = MAKEWORD(2, 0);

	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0)
		return -1;

	if (LOBYTE(wsaData.wVersion) < 1) {
		WSACleanup();
		return -1;
	}

	if (LOBYTE(wsaData.wVersion) == 1 && HIBYTE(wsaData.wVersion) < 1) {
		WSACleanup();
		return -1;
	}

	if (LOBYTE(wsaData.wVersion) > 2) {
		WSACleanup();
		return -1;
	}

	return 0;
}

static void
socket_cleanup(void)
{
	WSACleanup();
}

static void
console_init(void)
{
	/* Try to attach to the console of the parent.
	 * Only available on XP/2k3 and later.
	 */
	HMODULE kernel32;
	BOOL (WINAPI *attach_console)(DWORD);
	FILE *f;
	int fd;

	kernel32 = LoadLibrary("kernel32.dll");
	if (!kernel32)
		return;
	attach_console = GetProcAddress(kernel32, "AttachConsole");
	if (!attach_console)
		goto free_lib;

	if (!attach_console((DWORD)-1))
		goto free_lib;

	/* This is horrible, but there is no fdreopen... */
	fd = _open_osfhandle(
		(intptr_t)GetStdHandle(STD_OUTPUT_HANDLE), _O_TEXT);
	if (fd >= 0) {
		f = _fdopen(fd, "wt");
		if (f)
			*stdout = *f;
		else
			close(fd);
	}
	fd = _open_osfhandle(
		(intptr_t)GetStdHandle(STD_ERROR_HANDLE), _O_TEXT);
	if (fd >= 0) {
		f = _fdopen(fd, "wt");
		if (f)
			*stderr = *f;
		else
			close(fd);
	}

free_lib:
	FreeLibrary(kernel32);
}

#ifdef _MSC_VER
int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	return main(__argc, __argv);
}
#endif

#ifdef read
#undef read
#endif
#define read(fd, buf, size) recv((fd), (char *)(buf), (size), 0)
#ifdef write
#undef write
#endif
#define write(fd, buf, size) send((fd), (char *)(buf), (size), 0)

#else

#define set_icon()
#define socket_init() 0
#define socket_cleanup()
#define console_init()

#endif /* _WIN32 */

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

static int
color_max(ggi_pixel mask)
{
	while (mask && !(mask & 1))
		mask >>= 1;

	return mask;
}

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

/* TODO: check count for buffer overruns */
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
			idx += sprintf(&pixfmt[idx], "p%d", pad);
		idx += sprintf(&pixfmt[idx], "c%d", bits);
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
			idx += sprintf(&pixfmt[idx], "p%d", pad);
		*shift = -1;
		idx += sprintf(&pixfmt[idx], "%c%d", color, bits);
		size -= pad + bits;
	}
	if (size)
		idx += sprintf(&pixfmt[idx], "p%d", size);
	return 0;

weird_pixfmt:
	strcpy(pixfmt, "weird");
	return -1;
}

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
remove_dead_data(void)
{
	if (!g.input.rpos)
		return;

	memmove(g.input.data,
		g.input.data + g.input.rpos,
		g.input.wpos - g.input.rpos);

	g.input.wpos -= g.input.rpos;
	g.input.rpos = 0;
}

int
set_title(void)
{
#ifdef HAVE_WMH
	const char *text = g.name ? g.name : g.server;
	if (text && text[0]) {
		char *title = malloc(strlen(text) + 3 + 6 + 1);
		if (!title)
			return -1;
		strcpy(title, text);
		strcat(title, " - ggivnc");
		ggiWmhSetTitle(g.stem, title);
		ggiWmhSetIconTitle(g.stem, title);
		free(title);
	}
	else {
		ggiWmhSetTitle(g.stem, "ggivnc");
		ggiWmhSetIconTitle(g.stem, "ggivnc");
	}
#endif /* HAVE_WMH */
	return 0;
}

static inline int
vnc_safe_write(int fd, const void *buf, int count)
{
	int res;
	int written = 0;

again:
	res = write(fd, buf, count);

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
#ifdef HAVE_INPUT_FDSELECT
		{
		struct gii_fdselect_fd add_fd;
		add_fd.fd = g.sfd;
		add_fd.mode = GII_FDSELECT_WRITE;
		ggControl(g.fdselect->channel, GII_FDSELECT_ADD, &add_fd);
		}
#endif
		return written;

	default:
		debug(1, "write error (%d, \"%s\").\n",
			errno, strerror(errno));
		return -1;
	}
}

int
safe_write(int fd, const void *buf, int count)
{
	int res = g.output.wpos ? 0 : vnc_safe_write(fd, buf, count);

	if (res == count)
		return 0;

	if (res >= 0) {
		count -= res;
		buf = (const uint8_t *)buf + res;

		if (g.output.wpos + count > g.output.size) {
			void *tmp;
			g.output.size = g.output.wpos + count + 1024;
			tmp = realloc(g.output.data, g.output.size);
			if (!tmp) {
				free(g.output.data);
				g.output.data = NULL;
				exit(1);
			}
            g.output.data = (uint8_t *)tmp;
		}
		memcpy(g.output.data + g.output.wpos, buf, count);
		g.output.wpos += count;

		return 0;
	}

	return -1;
}

static int
vnc_version(void)
{
	char buf[12];
	ssize_t len;
	unsigned int major, minor;
	char str[13];

	debug(2, "version\n");

	len = read(g.sfd, buf, sizeof(buf));
	if (len != sizeof(buf))
		return -1;

	memcpy(str, buf, sizeof(buf));
	str[12] = '\0';
	major = atoi(str + 4);
	minor = atoi(str + 8);
	if (major > 999 || minor > 999) {
		debug(1, "Invalid protocol version "
			"(%s -> %d.%d) requested\n",
			str, major, minor);
		return -1;
	}
	sprintf(str, "RFB %03u.%03u\n", major, minor);
	if (memcmp(str, buf, sizeof(buf))) {
		debug(1, "Invalid protocol version requested\n");
		return -1;
	}

	debug(1, "Server has protocol version RFB %03u.%03u\n", major, minor);
	if ((major == 3 && minor < 3) || major < 3) {
		/* 3.3 is the lowest protocol version supported.
		 * Bail.
		 */
		debug(1, "Protocol version not supported\n");
		return -1;
	}

	if (major == 3 && minor >= 4 && minor <= 6)
		/* 3.4 and 3.6 have been hijacked by UltraVNC, 3.5 was
		 * never released. Request 3.3 in these cases...
		 */
		minor = 3;

	if (major == 3 && minor == 889)
		/* Apple Remote Desktop hijacked 3.889, request 3.3...
		 */
		minor = 3;

	if ((major == 3 && minor > 8) || major > 3) {
		/* 3.8 is the highest protocol version supported.
		 */
		major = 3;
		minor = 8;
	}

	if (minor > g.max_protocol)
		minor = g.max_protocol;

	sprintf(str, "RFB %03u.%03u\n", major, minor);
	debug(1, "Using protocol version RFB %03u.%03u\n", major, minor);

	if (safe_write(g.sfd, str, sizeof(buf)))
		return -1;

	g.protocol = minor;

	return 0;
}

static int
vnc_auth(void)
{
	uint8_t passwd[8];
	uint8_t challenge[16];
	uint8_t response[16];
	ssize_t len;
	unsigned int i;

	len = read(g.sfd, challenge, sizeof(challenge));
	if (len != sizeof(challenge))
		return -1;

	if (!g.passwd) {
		if (get_password())
			return -1;
	}

	if (g.passwd)
		strncpy((char *)passwd, g.passwd, sizeof(passwd));
	else
		memset(passwd, 0, sizeof(passwd));

	/* Should apparently bitreverse the password bytes.
	 * I just love undocumented quirks to standard algorithms...
	 */
	for (i = 0; i < sizeof(passwd); ++i)
		passwd[i] = GGI_BITREV1(passwd[i]);

	deskey(passwd, EN0);
	des(&challenge[0], &response[0]);
	des(&challenge[8], &response[8]);

	return safe_write(g.sfd, response, sizeof(response));
}

struct security_t {
	uint8_t number;
	int weight;
	const char *name;
};

static struct security_t security_types[] = {
	{ 1,  2, "none" },
	{ 2,  1, "vnc-auth" },
	{ 5,  0, "ra2" },
	{ 6,  0, "ra2ne" },
	{ 7,  0, "sspi" },
	{ 8,  0, "sspine" },
	{ 16, 3, "tight" },
	{ 17, 0, "ultra" },
	{ 18, 0, "tls" },
	{ 19, 0, "vencrypt" },
	{ 0,  0, NULL }
};

static const char *
security_name(uint8_t type)
{
	int i;

	if (!type)
		return "(invalid)";

	for (i = 0; security_types[i].number; ++i)
		if (type == security_types[i].number)
			break;

	if (!security_types[i].number)
		return "(not recognized)";

	return security_types[i].name;
}

struct capability {
	uint32_t code;
	char vendor[4];
	char name[8];
};

static inline const char *
capability_dump(struct capability *cap)
{
	static char dump[28];
	char vendor[5];
	char name[9];

	memcpy(vendor, cap->vendor, sizeof(cap->vendor));
	vendor[4] = '\0';
	memcpy(name, cap->name, sizeof(cap->name));
	name[8] = '\0';

	sprintf(dump, "%d - %s %s", cap->code, vendor, name);
	return dump;
}

static int
vnc_security_tight(void)
{
	ssize_t len;
	uint32_t i, j;
	uint32_t tunnel_types;
	uint32_t auth_types;
	struct capability capability;
	struct capability *auth_caps;
	int weight = -1;
	int current_weight;
	uint32_t auth_code = 0;
	uint8_t buf[4];

	g.security_tight = 1;

	len = read(g.sfd, &tunnel_types, sizeof(tunnel_types));
	if (len != sizeof(tunnel_types))
		return -1;

	tunnel_types = get32_hilo((uint8_t *)&tunnel_types);

	for (i = 0; i < tunnel_types; ++i) {
		len = read(g.sfd, &capability, sizeof(capability));
		if (len != sizeof(capability))
			return -1;

		capability.code = get32_hilo((uint8_t *)&capability.code);

		debug(1, "Tunnel cap: %s\n", capability_dump(&capability));
	}

	if (tunnel_types) {
		/* Reply blindly with "no tunneling", even if that
		 * may not be available from the server
		 */
		uint32_t notunnel = 0;
		if (safe_write(g.sfd, &notunnel, sizeof(notunnel)))
			return -1;
	}

	len = read(g.sfd, &auth_types, sizeof(auth_types));
	if (len != sizeof(auth_types))
		return -1;

	auth_types = get32_hilo((uint8_t *)&auth_types);

    auth_caps = (struct capability*)malloc(auth_types * sizeof(*auth_caps));
	if (!auth_caps)
		return -1;

	for (i = 0; i < auth_types; ++i) {
		len = read(g.sfd, &auth_caps[i], sizeof(auth_caps[i]));
		if (len != sizeof(auth_caps[i]))
			return -1;

		auth_caps[i].code = get32_hilo((uint8_t *)&auth_caps[i].code);
	}

	for (i = 0; i < auth_types; ++i) {

		debug(1, "Auth cap: %s\n", capability_dump(&auth_caps[i]));

		if (g.allow_security) {
			for (j = 0; g.allow_security[j]; ++j) {
				if (g.allow_security[j] == 16)
					continue;
				if (g.allow_security[j] == auth_caps[i].code)
					break;
			}
			if (!g.allow_security[j])
				continue;
			current_weight = 256 - j;
		}
		else {
			for (j = 0; security_types[j].number; ++j) {
				if (security_types[j].number == 16)
					continue;
				if (security_types[j].number
					== auth_caps[i].code)
				{
					break;
				}
			}
			if (!security_types[j].number)
				continue;

			current_weight = security_types[j].weight;
		}

		if (current_weight > weight) {
			weight = current_weight;
			auth_code = auth_caps[i].code;
		}
	}

	if (auth_types) {
		if (weight == -1 && g.allow_security) {
			/* No allowed auth code found, be a little dirty and
			 * request the first non-tight auth type. The server
			 * just might allow it...
			 */
			for (j = 0; g.allow_security[j] == 16; ++j);
			auth_code = g.allow_security[j];
		}

		if (!auth_code)
			return -1;

		g.security = auth_code;

		buf[0] = auth_code >> 24;
		buf[1] = auth_code >> 16;
		buf[2] = auth_code >> 8;
		buf[3] = auth_code;

		if (safe_write(g.sfd, buf, sizeof(buf)))
			return -1;
	}
	else
		g.security = 1;

	switch (g.security) {
	case 1:
		if (g.protocol <= 7)
			/* No security result */
			return 0;
		break;

	case 2:
		if (vnc_auth())
			return -1;
		break;

	default:
		return -1;
	}

	len = read(g.sfd, buf, 4);
	if (len != sizeof(buf))
		return -1;

	if (memcmp(buf, "\0\0\0\0", 4))
		return -1;

	return 0;
}

static int
vnc_security(void)
{
	uint8_t count;
	uint8_t types[255];
	ssize_t len;
	int i, j;
	int current_weight;
	int weight = -1;
	int security = -1;

	debug(2, "security\n");

	g.security_tight = 0;

	if (g.protocol < 7) {
		uint32_t type;

		len = read(g.sfd, &type, sizeof(type));
		if (len != sizeof(type))
			return -1;

		type = get32_hilo((uint8_t *)&type);

		if (g.allow_security) {
			for (j = 0; g.allow_security[j]; ++j)
				if (g.allow_security[j] == type)
					break;
			if (!g.allow_security[j])
				return -1;
		}

		debug(1, "Security type %d - %s\n",
			type, security_name(type));

		switch (type) {
		case 1:
			/* No security result */
			return 0;
		case 2:
			g.security = type;
			goto handle_security;
		}

		return -1;
	}

	len = read(g.sfd, &count, sizeof(count));
	if (len != sizeof(count))
		return -1;

	if (!count) {
		debug(1, "No security types?\n");
		return -1;
	}

	len = read(g.sfd, types, count);
	if (len != count)
		return -1;

	for (i = 0; i < count; ++i) {

		debug(1, "Security type: %d - %s\n",
			types[i], security_name(types[i]));

		if (!types[i])
			return -1;

		if (g.allow_security) {
			for (j = 0; g.allow_security[j]; ++j)
				if (g.allow_security[j] == types[i])
					break;
			if (!g.allow_security[j])
				continue;
			current_weight = 256 - j;
		}
		else {
			for (j = 0; security_types[j].number; ++j)
				if (security_types[j].number == types[i])
					break;
			if (!security_types[j].number)
				continue;

			current_weight = security_types[j].weight;
		}

		if (current_weight > weight) {
			weight = current_weight;
			security = types[i];
		}
	}

	if (weight == -1 && g.allow_security)
		/* No allowed security type found, be a little dirty and
		 * request the first allowed security type. The server
		 * just might allow it...
		 */
		security = g.allow_security[0];

	if (security < 0)
		return security;

	g.security = security;

	if (safe_write(g.sfd, &g.security, sizeof(g.security)))
		return -1;

handle_security:
	switch (g.security) {
	case 1:
		if (g.protocol <= 7)
			/* No security result */
			return 0;
		break;

	case 2:
		if (vnc_auth())
			return -1;
		break;

	case 16:
		return vnc_security_tight();

	default:
		return -1;
	}

	len = read(g.sfd, types, 4);
	if (len != 4)
		return -1;

	if (memcmp(types, "\0\0\0\0", 4))
		return -1;

	return 0;
}

static int
vnc_init(void)
{
	ssize_t len;
	uint8_t sinit[24];
	uint16_t red_max;
	uint16_t green_max;
	uint16_t blue_max;
	uint32_t name_length;
	ggi_pixelformat server_ggi_pf;

	debug(2, "init\n");

	if (safe_write(g.sfd, &g.shared, sizeof(g.shared)))
		return -1;

	len = read(g.sfd, sinit, sizeof(sinit));
	if (len != sizeof(sinit))
		return -1;

	g.width = get16_hilo(&sinit[0]);
	g.height = get16_hilo(&sinit[2]);
	red_max = get16_hilo(&sinit[8]);
	green_max = get16_hilo(&sinit[10]);
	blue_max = get16_hilo(&sinit[12]);
	name_length = get32_hilo(&sinit[20]);

	memset(&server_ggi_pf, 0, sizeof(server_ggi_pf));
	server_ggi_pf.size = sinit[4];
	if (sinit[7]) {
		server_ggi_pf.depth = color_bits(red_max) +
			color_bits(green_max) +
			color_bits(blue_max);
		if (server_ggi_pf.size < server_ggi_pf.depth)
			server_ggi_pf.depth = sinit[5];
		server_ggi_pf.red_mask = red_max << sinit[14];
		server_ggi_pf.green_mask = green_max << sinit[15];
		server_ggi_pf.blue_mask = blue_max << sinit[16];
	}
	else {
		server_ggi_pf.depth = sinit[5];
		server_ggi_pf.clut_mask = (1 << server_ggi_pf.depth) - 1;
	}
	g.server_endian = !!sinit[6];

	generate_pixfmt(g.server_pixfmt, sizeof(g.server_pixfmt),
		&server_ggi_pf);

	if (!name_length) {
		if (g.name)
			free(g.name);
		g.name = NULL;
	}
	else {
		int length = name_length < 2000 ? name_length : 2000;

        g.name = (char*)malloc(length + 1);
		if (!g.name)
			return -1;

		len = read(g.sfd, g.name, length);
		if (len != length) {
			free(g.name);
			g.name = NULL;
			return -1;
		}

		g.name[length] = '\0';

		if (name_length > 2000) {
			/* read out the tail of the name */
			uint32_t rest = name_length - 2000;
			while (rest > 0) {
				char tmp[128];
				int chunk = rest < sizeof(tmp)
					? rest : sizeof(tmp);
				len = read(g.sfd, tmp, chunk);
				if (len != chunk)
					return -1;
				rest -= chunk;
			}
		}
	}

	if (g.security_tight) {
		uint8_t tight_init[8];
		uint16_t server_messages;
		uint16_t client_messages;
		uint16_t encodings;
		uint16_t i;
		struct capability capability;

		len = read(g.sfd, tight_init, sizeof(tight_init));
		if (len != sizeof(tight_init)) {
			debug(1, "Failed to read tight init\n");
			return -1;
		}

		server_messages = get16_hilo(&tight_init[0]);
		client_messages = get16_hilo(&tight_init[2]);
		encodings = get16_hilo(&tight_init[4]);

		for (i = 0; i < server_messages; ++i) {
			len = read(g.sfd, &capability, sizeof(capability));
			if (len != sizeof(capability))
				return -1;

			capability.code =
				get32_hilo((uint8_t *)&capability.code);

			debug(1, "Server message: %s\n",
				capability_dump(&capability));
		}

		for (i = 0; i < client_messages; ++i) {
			len = read(g.sfd, &capability, sizeof(capability));
			if (len != sizeof(capability))
				return -1;

			capability.code =
				get32_hilo((uint8_t *)&capability.code);

			debug(1, "Client message: %s\n",
				capability_dump(&capability));
		}

		for (i = 0; i < encodings; ++i) {
			len = read(g.sfd, &capability, sizeof(capability));
			if (len != sizeof(capability))
				return -1;

			capability.code =
				get32_hilo((uint8_t *)&capability.code);

			debug(1, "Server encoding: %s\n",
				capability_dump(&capability));
		}
	}

	return 0;
}

static void
destroy_wire_stem(void)
{
	if (!g.wire_stem)
		return;

	ggiClose(g.wire_stem);
#ifdef HAVE_GGNEWSTEM
	ggDelStem(g.wire_stem);
#endif
	g.wire_stem = NULL;
	memcpy(&g.wire_mode, &g.mode, sizeof(ggi_mode));
}

static int
create_wire_stem(void)
{
	int c_max;
	int r_max, g_max, b_max;
	int r_shift, g_shift, b_shift;
	int size, depth;
	char *target;

	if (parse_pixfmt(g.wire_pixfmt, &c_max,
		&r_max, &g_max, &b_max,
		&r_shift, &g_shift, &b_shift,
		&size, &depth))
	{
		return -1;
	}

#ifdef HAVE_GGNEWSTEM
	g.wire_stem = ggNewStem(libggi, NULL);
	if (!g.wire_stem)
		return -1;
#endif
	if (c_max)
		target = strdup("display-memory");
	else {
		const char display_memory[] =
			"display-memory:-pixfmt=";

        target = (char*)malloc(sizeof(display_memory)
			+ strlen(g.wire_pixfmt) + 1);
		if (target) {
			strcpy(target, display_memory);
			strcat(target, g.wire_pixfmt);
		}
	}
	if (!target) {
#ifdef HAVE_GGNEWSTEM
		ggDelStem(g.wire_stem);
#endif
		g.wire_stem = NULL;
		return -1;
	}
#ifdef HAVE_GGNEWSTEM
	if (ggiOpen(g.wire_stem, target, NULL) < 0) {
		ggDelStem(g.wire_stem);
		g.wire_stem = NULL;
		free(target);
		return -1;
	}
#else
	g.wire_stem = ggiOpen(target, NULL);
	if (!g.wire_stem) {
		free(target);
		return -1;
	}
#endif
	free(target);

	memset(&g.wire_mode, 0, sizeof(g.wire_mode));
	g.wire_mode.frames = 1;
	g.wire_mode.visible.x = g.width;
	g.wire_mode.visible.y = g.height;
	g.wire_mode.virt.x = g.width;
	g.wire_mode.virt.y = g.height;
	g.wire_mode.size.x = g.wire_mode.size.y = GGI_AUTO;
	GT_SETDEPTH(g.wire_mode.graphtype, depth);
	GT_SETSIZE(g.wire_mode.graphtype, size);
	if (c_max)
		GT_SETSCHEME(g.wire_mode.graphtype, GT_PALETTE);
	else
		GT_SETSCHEME(g.wire_mode.graphtype, GT_TRUECOLOR);
/*
	if (g.wire_endian != g.local_endian)
		GT_SETSUBSCHEME(g.wire_mode.graphtype, GT_SUB_REVERSE_ENDIAN);
*/
	g.wire_mode.dpp.x = g.wire_mode.dpp.y = 1;

	if (ggiSetMode(g.wire_stem, &g.wire_mode) < 0) {
		destroy_wire_stem();
		return -1;
	}

	return 0;
}

static void
delete_wire_stem(void)
{
	debug(1, "delete_wire_stem\n");

	ggiSetWriteFrame(g.stem, 1 - ggiGetDisplayFrame(g.stem));
	ggiSetReadFrame(g.stem, 1 - ggiGetDisplayFrame(g.stem));

	ggiCrossBlit(g.wire_stem,
		0, 0,
		g.width, g.height,
		g.stem,
		g.offset.x, g.offset.y);

	ggiSetWriteFrame(g.stem, ggiGetDisplayFrame(g.stem));

	ggiCopyBox(g.stem,
		0, 0,
		g.width, g.height,
		0, 0);
	ggiFlush(g.stem);

	ggiSetWriteFrame(g.stem, 1 - ggiGetDisplayFrame(g.stem));

	destroy_wire_stem();

	if (g.stem_change)
		g.stem_change();
}

static int
add_wire_stem(void)
{
	debug(1, "add_wire_stem\n");

	if (create_wire_stem())
		return -1;

	ggiCrossBlit(g.stem,
		g.offset.x, g.offset.y,
		g.width, g.height,
		g.wire_stem,
		0, 0);

	ggiSetWriteFrame(g.stem, ggiGetDisplayFrame(g.stem));
	ggiSetReadFrame(g.stem, ggiGetDisplayFrame(g.stem));

	if (g.stem_change)
		g.stem_change();

	return 0;
}

static int
need_wire_stem(const char *pixfmt)
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

	if (g.mode.frames < 2)
		return 1;
	if (!strcmp(g.local_pixfmt, "weird"))
		return 1;
	if (strcmp(pixfmt, g.local_pixfmt))
		return 1;
	if (c_max)
		return 1;
	if (ggiGetPixelFormat(g.stem)->flags & ~GGI_PF_REVERSE_ENDIAN)
		return 1;
	if (GT_SUBSCHEME(g.mode.graphtype) & ~GT_SUB_REVERSE_ENDIAN)
		return 1;
	/*
	if (g.wire_endian != g.local_endian)
		return 1;
	*/
	return 0;
}

static int
vnc_set_pixel_format(void)
{
	int c_max;
	int r_max, g_max, b_max;
	int r_shift, g_shift, b_shift;
	int size, depth;
	uint8_t buf[20];
	int crossblit;

	debug(2, "set_pixel_format\n");

	if (parse_pixfmt(g.wire_pixfmt, &c_max,
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
	buf[ 6] = g.wire_endian;
	buf[ 7] = c_max ? 0 : 1;
	buf[ 8] = r_max >> 8;
	buf[ 9] = r_max;
	buf[10] = g_max >> 8;
	buf[11] = g_max;
	buf[12] = b_max >> 8;
	buf[13] = b_max;
	buf[14] = r_shift;
	buf[15] = g_shift;
	buf[16] = b_shift;
	buf[17] = 0;
	buf[18] = 0;
	buf[19] = 0;

	memcpy(&g.wire_mode, &g.mode, sizeof(ggi_mode));
	if (g.wire_stem) {
		ggiClose(g.wire_stem);
#ifdef HAVE_GGNEWSTEM
		ggDelStem(g.wire_stem);
#endif
		g.wire_stem = NULL;
	}

	if (need_wire_stem(g.wire_pixfmt))
		crossblit = 1;
	else if (g.scrollx || g.scrolly) {
		g.wire_stem_flags |= 1;
		crossblit = 1;
	}
	else
		crossblit = 0;

	if (crossblit) {
		if (create_wire_stem())
			return -1;
	}

	if (safe_write(g.sfd, buf, sizeof(buf))) {
		if (g.wire_stem) {
			ggiClose(g.wire_stem);
#ifdef HAVE_GGNEWSTEM
			ggDelStem(g.wire_stem);
#endif
			g.wire_stem = NULL;
			memcpy(&g.wire_mode, &g.mode, sizeof(ggi_mode));
		}
		return -1;
	}

	return 0;
}

int
vnc_set_encodings(void)
{
	int res;
	uint8_t *buf;
	uint16_t i;

	buf = (uint8_t *)malloc(4 + 4 * g.encoding_count);
	if (!buf)
		return -1;

	buf[0] = 2;
	buf[1] = 0;
	buf[2] = g.encoding_count >> 8;
	buf[3] = g.encoding_count;

	debug(1, "set_encodings\n");
	for (i = 0; i < g.encoding_count; ++i) {
		buf[4 + 4 * i + 0] = g.encoding[i] >> 24;
		buf[4 + 4 * i + 1] = g.encoding[i] >> 16;
		buf[4 + 4 * i + 2] = g.encoding[i] >> 8;
		buf[4 + 4 * i + 3] = g.encoding[i];

		debug(1, "%d: %s\n",
			g.encoding[i], lookup_encoding(g.encoding[i]));
	}

	res = safe_write(g.sfd, buf, 4 + 4 * g.encoding_count);
	free(buf);

	return res;
}

int
vnc_update_request(int incremental)
{
	uint8_t buf[10] = {
		3,
		incremental,
		0, 0,
		0, 0,
		g.width >> 8, g.width,
		g.height >> 8, g.height
	};

	debug(2, "update_request (%dx%d %s)\n",
		g.width, g.height,
		incremental ? "incr" : "full");

	return safe_write(g.sfd, buf, sizeof(buf));
}

static int
vnc_key(int down, uint32_t key)
{
	uint8_t buf[8] = {
		4,
		down,
		0, 0,
		key >> 24, key >> 16, key >> 8, key
	};

	if (g.no_input)
		return 0;

	debug(2, "key %08x %s\n", key, down ? "down" : "up");

	return safe_write(g.sfd, buf, sizeof(buf));
}

static int
vnc_pointer(int buttons, int x, int y)
{
	uint8_t buf[6] = {
		5,
		buttons,
		x >> 8, x,
		y >> 8, y
	};

	if (g.no_input)
		return 0;

	debug(2, "pointer\n");

	return safe_write(g.sfd, buf, sizeof(buf));
}

#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
static int
vnc_send_cut_text(void)
{
	uint8_t small_buf[8 + 256 + 1] = {
		6,
		0, 0, 0
	};
	size_t cut_len = sizeof(small_buf) - 9;
	uint8_t *buf = small_buf;
	const size_t max_len = 0x10000;
	int res;
	uint8_t *src;
	uint8_t *dst;

	if (g.no_input)
		return 0;

	if (ggiWmhClipboardOpen(g.stem, GGIWMH_CLIPBOARD_GET) != GGI_OK)
		return 0;
	res = ggiWmhClipboardGet(g.stem, GGIWMH_MIME_TEXT_UTF8,
		&buf[8], &cut_len);

	if (res == GGI_ENOSPACE) {
		if (cut_len > max_len)
			cut_len = max_len;
		buf = malloc(8 + cut_len + 1);
		if (!buf) {
			cut_len = sizeof(small_buf) - 9;
			buf = small_buf;
		}
		else {
			res = ggiWmhClipboardGet(g.stem,
				GGIWMH_MIME_TEXT_UTF8, &buf[8], &cut_len);
			if (cut_len > max_len)
				cut_len = max_len;
			memcpy(buf, small_buf, 8);
		}
	}
	ggiWmhClipboardClose(g.stem);
	if (res != GGI_OK && res != GGI_ENOSPACE) {
		if (buf != small_buf)
			free(buf);
		return 0;
	}

	buf[8 + cut_len] = '\0';

	/* convert from utf-8 to us-ascii and drop CR chars. */
	src = &buf[8];
	while (*src && *src != '\x0d' && !(*src & 0x80))
		++src;
	dst = src;
	while (*src) {
		while (*src == '\x0d' || (*src & 0x80))
			++src;
		while (*src && *src != '\x0d' && !(*src & 0x80))
			*dst++ = *src++;
	}

	cut_len = dst - &buf[8];
	if (!cut_len) {
		if (buf != small_buf)
			free(buf);
		return 0;
	}

	buf[4] = cut_len >> 24;
	buf[5] = cut_len >> 16;
	buf[6] = cut_len >> 8;
	buf[7] = cut_len;

	res = safe_write(g.sfd, buf, 8 + cut_len);

	if (buf != small_buf)
		free(buf);

	return res;
}
#endif /* GGIWMHFLAG_CLIPBOARD_CHANGE */

#ifdef VNC_NOT_IMPL
static int
vnc_not_implemented(void)
{
	debug(1, "Encoding not implemented\n");
	exit(1);
}
#endif

static void
render_update(void)
{
	int d_frame, w_frame;

	d_frame = ggiGetDisplayFrame(g.stem);
	w_frame = ggiGetWriteFrame(g.stem);

	if (g.wire_stem) {
		debug(2, "crossblit\n");
		ggiCrossBlit(g.wire_stem,
			g.slide.x, g.slide.y,
			g.area.x, g.area.y,
			g.stem,
			g.offset.x, g.offset.y);
	}
	if (g.flush_hook)
		g.flush_hook(g.flush_hook_data);

	ggiSetDisplayFrame(g.stem, w_frame);

	ggiFlush(g.stem);

	if (d_frame != w_frame)
        {
        ggiSetWriteFrame(g.stem, d_frame);
        ggiCopyBox(g.stem,
            g.offset.x, g.offset.y,
            g.width, g.height,
            g.offset.x, g.offset.y);
        ggiSetReadFrame(g.stem, d_frame);

        if (g.post_flush_hook)
            g.post_flush_hook(g.flush_hook_data);
        }

        static int count = 0;
        qDebug() << "update_frame" << ++count;
        mTestEvent("[Signal]update_frame");
        //int numbufs = ggiDBGetNumBuffers( g.stem );
        //qDebug() << "Number of memory buffers = " << numbufs;
        //const ggi_directbuffer *db;
        //db = ggiDBGetBuffer( g.stem, 0 );
        //if( !db->type & GGI_DB_SIMPLE_PLB )
        //{
        //    qDebug() << "We don't handle anything but simple pixel-linear buffer";
        //}
        //int frameno = db->frame;
        //int ggiStride = db->buffer.plb.stride;
        //printf("frameno,stride,pixelsize = [%d,%d,%d]\n", frameno, ggiStride, db->buffer.plb.pixelformat->size );
        //qDebug() << "frameno,stride,pixelsize = " << frameno << "," << ggiStride << "," << db->buffer.plb.pixelformat->size;
        // memcpy( ptr, db->read, ggiStride * 1080 );
        // ds.writeRawData( (const char*)db->read, ggiStride * 1080 );
        // Flyggi::instance()->getImage().loadFromData( (const uchar*)db->read, ggiStride*1080 );
        //QImage img( ( const uchar*)db->read, 1920, 1080, ggiStride, QImage::Format_RGB16 );

        //Flyggi::instance()->assignImage( img );
        //Flyggi::instance()->ggiReady("hello");

}

static int
vnc_resize(ggi_mode *mode)
{
	int del_wire_stem = 0;

	mode->virt.x = mode->visible.x;
	mode->virt.y = mode->visible.y;

	if (mode->visible.x == g.mode.visible.x &&
		mode->visible.y == g.mode.visible.y)
	{
		return 0;
	}
	if ((mode->visible.x < g.width || mode->visible.y < g.height) &&
		g.mode.visible.x >= g.width && g.mode.visible.y >= g.height)
	{
		if (!g.wire_stem) {
			add_wire_stem();
			g.wire_stem_flags |= 1;
		}
	}
	else if ((g.mode.visible.x < g.width || g.mode.visible.y < g.height)
		&& mode->visible.x >= g.width && mode->visible.y >= g.height)
	{
		if (g.wire_stem && g.wire_stem_flags) {
			g.wire_stem_flags &= ~1;
			if (!g.wire_stem_flags)
				del_wire_stem = 1;
		}
	}
	debug(2, "resize to %dx%d\n", mode->visible.x, mode->visible.y);

	scrollbar_destroy();
	ggiCheckMode(g.stem, mode);
	ggiSetMode(g.stem, mode);
#ifdef HAVE_WMH
	ggiWmhAllowResize(g.stem, 40, 40, g.width, g.height, 1, 1);
#endif
#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	g.mode = *mode;
	g.scrollx = 0;
	g.scrolly = 0;
	g.area.x = g.width;
	g.area.y = g.height;
	scrollbar_area();

	if (g.mode.visible.x - g.scrolly * SCROLL_SIZE > g.width)
		g.offset.x = (g.mode.visible.x - g.scrolly * SCROLL_SIZE -
			g.width) / 2;
	else
		g.offset.x = 0;
	if (g.mode.visible.y - g.scrollx * SCROLL_SIZE > g.height)
		g.offset.y = (g.mode.visible.y - g.scrollx * SCROLL_SIZE -
			g.height) / 2;
	else
		g.offset.y = 0;

#ifndef HAVE_WIDGETS
	if (g.mode.visible.x < g.width)
		g.scrollx = 1;
	if (g.mode.visible.y < g.height)
		g.scrolly = 1;
	g.area = g.mode.visible;
#endif

	if (g.slide.x + g.area.x > g.width)
		g.slide.x = g.width - g.area.x;
	if (g.slide.x < 0)
		g.slide.x = 0;
	g.sx = g.slide.x;
	if (g.slide.y + g.area.y > g.height)
		g.slide.y = g.height - g.area.y;
	if (g.slide.y < 0)
		g.slide.y = 0;
	g.sy = g.slide.y;

	scrollbar_create();

	if (del_wire_stem)
		delete_wire_stem();
	else if (g.wire_stem) {
		debug(3, "resize crossblit\n");
		render_update();
	}

	return 0;
}

int
wire_mode_switch(const char *pixfmt, int endian, ggi_coord wire_size)
{
	gii_event ev;
	ggi_cmddata_switchrequest swreq;
	ggi_mode current_mode;
	int current_width, current_height;
	int add_wire = 0;
	int del_wire = 0;
	int do_need_wire_stem;
	int did_need_wire_stem;

	if (g.width == wire_size.x && g.height == wire_size.y
		&& !strcmp(pixfmt, g.wire_pixfmt)
		/*&& endian == g.wire_endian*/)
	{
		debug(1, "same mode\n");
		return 0;
	}

	do_need_wire_stem = need_wire_stem(pixfmt) ? 2 : 0;
	if (g.mode.visible.x < wire_size.x || g.mode.visible.y < wire_size.y)
		do_need_wire_stem |= 1;

	did_need_wire_stem = g.wire_stem_flags;
	if (!did_need_wire_stem)
		did_need_wire_stem |= g.wire_stem ? 2 : 0;
	if (g.mode.visible.x < g.width || g.mode.visible.y < g.height)
		did_need_wire_stem |= 1;

	debug(1, "wire_mode_switch %dx%d, old %s %dx%d %d, new %s %dx%d %d\n",
		g.mode.visible.x, g.mode.visible.y,
		g.wire_pixfmt, g.width, g.height, did_need_wire_stem,
		pixfmt, wire_size.x, wire_size.y, do_need_wire_stem);

	render_update();

	if (!(did_need_wire_stem & do_need_wire_stem & 2)) {
		if (did_need_wire_stem != do_need_wire_stem) {
			del_wire = did_need_wire_stem;
			add_wire = do_need_wire_stem;
		}
	}
	else if (strcmp(pixfmt, g.wire_pixfmt))
		del_wire = add_wire = 2;

	if (del_wire) {
		g.wire_stem_flags = 0;
		delete_wire_stem();
	}
	if (add_wire)
		g.wire_stem_flags = (do_need_wire_stem & 2) ? 0 : (add_wire & 1);

	scrollbar_destroy();
	g.scrollx = 0;
	g.scrolly = 0;
	g.area = wire_size;
	g.width = g.area.x;
	g.height = g.area.y;
	scrollbar_area();

	if (g.mode.visible.x - g.scrolly * SCROLL_SIZE > g.width) {
		ggi_color black = { 0, 0, 0, 0 };
		int d = g.mode.visible.x - g.scrolly * SCROLL_SIZE - g.width;
		g.offset.x = d / 2;
		ggiSetGCForeground(g.stem, ggiMapColor(g.stem, &black));
		ggiDrawBox(g.stem, 0, 0, g.offset.x, g.mode.visible.y);
		ggiDrawBox(g.stem,
			g.mode.visible.x - (d + 1) / 2, 0,
			g.mode.visible.x, g.mode.visible.y);
		if (g.mode.frames >= 2) {
			int w_frame = ggiGetWriteFrame(g.stem);
			ggiSetWriteFrame(g.stem, 1 - w_frame);
			ggiDrawBox(g.stem,
				0, 0, g.offset.x, g.mode.visible.y);
			ggiDrawBox(g.stem,
				g.mode.visible.x - (d + 1) / 2, 0,
				g.mode.visible.x, g.mode.visible.y);
			ggiSetWriteFrame(g.stem, w_frame);
		}
	}
	else
		g.offset.x = 0;
	if (g.mode.visible.y - g.scrollx * SCROLL_SIZE > g.height) {
		ggi_color black = { 0, 0, 0, 0 };
		int d = g.mode.visible.y - g.scrollx * SCROLL_SIZE - g.height;
		g.offset.y = d / 2;
		ggiSetGCForeground(g.stem, ggiMapColor(g.stem, &black));
		ggiDrawBox(g.stem, 0, 0, g.mode.visible.x, g.offset.y);
		ggiDrawBox(g.stem,
			0, g.mode.visible.y - (d + 1) / 2,
			g.mode.visible.x, g.mode.visible.y);
		if (g.mode.frames >= 2) {
			int w_frame = ggiGetWriteFrame(g.stem);
			ggiSetWriteFrame(g.stem, 1 - w_frame);
			ggiDrawBox(g.stem,
				0, 0, g.mode.visible.x, g.offset.y);
			ggiDrawBox(g.stem,
				0, g.mode.visible.y - (d + 1) / 2,
				g.mode.visible.x, g.mode.visible.y);
			ggiSetWriteFrame(g.stem, w_frame);
		}
	}
	else
		g.offset.y = 0;

#ifndef HAVE_WIDGETS
	if (g.mode.visible.x < g.width)
		g.scrollx = 1;
	if (g.mode.visible.y < g.height)
		g.scrolly = 1;
	g.area = g.mode.visible;
#endif

	if (g.slide.x + g.area.x > g.width)
		g.slide.x = g.width - g.area.x;
	if (g.slide.x < 0)
		g.slide.x = 0;
	g.sx = g.slide.x;
	if (g.slide.y + g.area.y > g.height)
		g.slide.y = g.height - g.area.y;
	if (g.slide.y < 0)
		g.slide.y = 0;
	g.sy = g.slide.y;

	strcpy(g.wire_pixfmt, pixfmt);

	if (g.wire_stem) {
		g.wire_mode.visible.x = g.width;
		g.wire_mode.visible.y = g.height;
		g.wire_mode.virt.x = g.width;
		g.wire_mode.virt.y = g.height;
		g.wire_mode.size.x = g.wire_mode.size.y = GGI_AUTO;

		debug(1, "resize wire stem\n");
		if (ggiSetMode(g.wire_stem, &g.wire_mode) < 0) {
			debug(1, "Unable to change wire mode\n");
			exit(1);
		}

		if (g.stem_change)
			g.stem_change();
	}
	else if (add_wire)
		add_wire_stem();

	scrollbar_create();

	if (g.flush_hook)
		g.flush_hook(g.flush_hook_data);

	/* Change mode by injecting a "fake" mode switch request. */
	current_mode = g.mode;
	current_width = g.width;
	current_height = g.height;

	g.width = wire_size.x;
	g.height = wire_size.y;
	select_mode();

	swreq.request = GGI_REQSW_MODE;
	swreq.mode = g.mode;

	g.mode = current_mode;
	g.width = current_width;
	g.height = current_height;

	ggiCheckMode(g.stem, &swreq.mode);

	if (swreq.mode.frames != g.mode.frames)
		return -1;
	if (swreq.mode.graphtype != g.mode.graphtype)
		return -1;

#ifdef HAVE_INPUT_FDSELECT
	ev.any.target = GII_EV_TARGET_QUEUE;
	ev.any.size = sizeof(gii_cmd_nodata_event) +
		sizeof(ggi_cmddata_switchrequest);
	ev.any.type = evCommand;
	ev.cmd.code = GGICMD_REQUEST_SWITCH;

	memcpy(ev.cmd.data, &swreq, sizeof(swreq));

	giiEventSend(g.stem, &ev);
#else
	vnc_resize(&swreq.mode);
#endif

	return 0;
}

int
vnc_update_rect(void)
{
	uint32_t encoding;

	debug(2, "update_rect\n");

	if (!g.rects) {
		if (g.bw.count)
			bandwidth_end();
		g.bw.counting = 0;

		vnc_update_request(!g.desktop_size);
		g.desktop_size = 0;
		render_update();
		remove_dead_data();
		g.action = vnc_wait;
		return 1;
	}

	if (g.input.wpos < g.input.rpos + 12)
		return 0;

	g.x = get16_hilo(&g.input.data[g.input.rpos + 0]);
	g.y = get16_hilo(&g.input.data[g.input.rpos + 2]);
	g.w = get16_hilo(&g.input.data[g.input.rpos + 4]);
	g.h = get16_hilo(&g.input.data[g.input.rpos + 6]);

	if (!g.wire_stem) {
		g.x += g.offset.x;
		g.y += g.offset.y;
	}

	encoding = get32_hilo(&g.input.data[g.input.rpos + 8]);

	g.input.rpos += 12;

	debug(2, "encoding %d, x=%d y=%d w=%d h=%d\n",
		encoding, g.x, g.y, g.w, g.h);

	switch (encoding) {
	case 0:
		g.action = vnc_raw;
		break;

	case 1:
		g.action = vnc_copyrect;
		break;

	case 2:
		g.action = vnc_rre;
		break;

	case 4:
		g.action = vnc_corre;
		break;

	case 5:
		g.action = vnc_hextile;
		break;

	case 6:
		g.action = vnc_zlib;
		break;

	case 7:
		g.action = vnc_tight;
		break;

	case 8:
		g.action = vnc_zlibhex;
		break;

	case 16:
		g.action = vnc_zrle;
		break;

	case -223:
		g.action = vnc_desktop_size;
		break;

	case -224:
		g.action = vnc_lastrect;
		break;

	case -307:
		g.action = vnc_desktop_name;
		break;

	case 0x574d5669:
		g.action = vnc_wmvi;
		break;

	default:
		exit(1);
	}

	return 1;
}

static int
vnc_update(void)
{
	debug(2, "update\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	g.rects = get16_hilo(&g.input.data[g.input.rpos + 2]);

	debug(2, "rects=%d\n", g.rects);

	g.input.rpos += 4;

	g.bw.counting = g.auto_encoding;
	g.bw.count = 0;

	g.action = vnc_update_rect;
	return 1;
}

static int
vnc_palette(void)
{
	int i;
	int first;
	int count;
	ggi_color clut[256];

	debug(2, "palette\n");

	if (g.input.wpos < g.input.rpos + 6)
		return 0;

	first = get16_hilo(&g.input.data[g.input.rpos + 2]);
	count = get16_hilo(&g.input.data[g.input.rpos + 4]);

	if (g.input.wpos < g.input.rpos + 6 + 6 * count)
		return 0;

	g.input.rpos += 6;
	for (i = 0; i < count; ++i) {
		clut[i].r = get16_hilo(&g.input.data[g.input.rpos + 0]);
		clut[i].g = get16_hilo(&g.input.data[g.input.rpos + 2]);
		clut[i].b = get16_hilo(&g.input.data[g.input.rpos + 4]);
		clut[i].a = 0;
		g.input.rpos += 6;
	}
	ggiSetPalette(g.wire_stem, first, count, clut);

	debug(3, "palette crossblit\n");
	render_update();
	remove_dead_data();
	g.action = vnc_wait;
	return 1;
}

static int
vnc_bell(void)
{
	debug(2, "bell\n");

	++g.input.rpos;

	remove_dead_data();
	g.action = vnc_wait;
	return 1;
}


static int
drain_cut_text(void)
{
	const int max_chunk = 0x100000;
	uint32_t rest;
	uint32_t limit;
	int chunk;

	debug(2, "drain-cut-text\n");

	if (g.input.wpos < g.input.rpos + 4)
		return 0;

	rest = get32_hilo(&g.input.data[g.input.rpos]);

	limit = max_chunk;
	if (g.input.wpos - (g.input.rpos + 4) < max_chunk)
		limit = g.input.wpos - (g.input.rpos + 4);

	chunk = rest < limit ? rest : limit;

	if (rest > limit) {
		g.input.rpos += chunk;
		remove_dead_data();
		rest -= limit;
		g.input.data[0] = rest >> 24;
		g.input.data[1] = rest >> 16;
		g.input.data[2] = rest >> 8;
		g.input.data[3] = rest;
		return chunk > 0;
	}

	g.input.rpos += 4 + chunk;

	remove_dead_data();
	g.action = vnc_wait;
	return 1;
}

static int
vnc_cut_text(void)
{
	const uint32_t max_len = 0x10000;
	uint32_t length;
	int len;

	debug(2, "cut_text\n");

	if (g.input.wpos < g.input.rpos + 8)
		return 0;

	length = get32_hilo(&g.input.data[g.input.rpos + 4]);
	len = length < max_len ? length : max_len;

	if (g.input.wpos < g.input.rpos + 8 + len)
		return 0;

#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	if (ggiWmhClipboardOpen(g.stem, GGIWMH_CLIPBOARD_ADD) == GGI_OK) {
		uint8_t *orig = &g.input.data[g.input.rpos + 8];
		uint8_t *mem = malloc(2 * len + 1);
		uint8_t *nl;
		uint8_t *src;
		uint8_t *dst;
		int chunk;
		int dst_len = len;
		if (!mem)
			goto out;
		src = orig;
		dst = mem;
		for (;;) {
			nl = (uint8_t *)strchr((char *)src, '\x0a');
			if (!nl)
				break;
			chunk = nl - src;
			memcpy(dst, src, chunk);
			src += chunk + 1;
			dst += chunk;
			*dst++ = '\x0d';
			*dst++ = '\x0a';
			++dst_len;
		}
		memcpy(dst, src, len - (src - orig));
		mem[dst_len++] = '\0';
		debug(1, "client_cut_text\n");
		ggiWmhClipboardAdd(g.stem, GGIWMH_MIME_TEXT, mem, dst_len);
		free(mem);
out:
		ggiWmhClipboardClose(g.stem);
	}
	else
		debug(1, "unable to open clipboard\n");
#endif /* GGIWMHFLAG_CLIPBOARD_CHANGE */

	if (length > max_len) {
		g.input.rpos += 4 + len;
		remove_dead_data();
		length -= max_len;
		g.input.data[0] = length >> 24;
		g.input.data[1] = length >> 16;
		g.input.data[2] = length >> 8;
		g.input.data[3] = length;
		g.action = drain_cut_text;
		return 1;
	}

	g.input.rpos += 8 + len;

	remove_dead_data();
	g.action = vnc_wait;
	return 1;
}

int
vnc_wait(void)
{
	debug(2, "wait\n");

	if (g.input.wpos < g.input.rpos + 1)
		return 0;

	switch (g.input.data[g.input.rpos]) {
	case 0:
		g.action = vnc_update;
		break;

	case 1:
		g.action = vnc_palette;
		break;

	case 2:
		g.action = vnc_bell;
		break;

	case 3:
		g.action = vnc_cut_text;
		break;

#ifdef HAVE_GGNEWSTEM
	case 253:
		g.action = gii_receive;
		break;
#endif /* HAVE_GGNEWSTEM */

	default:
		debug(1, "got cmd code %d\n", g.input.data[g.input.rpos]);
		exit(1);
	}

	return 1;
}

static int
vnc_read(void *arg, uint32_t flag, struct gii_fdselect_fd *data)
{
	ssize_t len;

	debug(2, "read\n");

	if (g.input.wpos == g.input.size) {
		void *tmp;
		g.input.size += 65536;
		tmp = realloc(g.input.data, g.input.size);
		if (!tmp) {
			free(g.input.data);
			g.input.data = NULL;
			exit(1);
		}
        g.input.data = (uint8_t*)tmp;
	}

	len = read(g.sfd,
		g.input.data + g.input.wpos, g.input.size - g.input.wpos);

	if (len <= 0) {
		debug(1, "read error %d \"%s\"\n", errno, strerror(errno));
		exit(1);
	}

	debug(3, "len=%li\n", len);

	if (g.bw.counting) {
		if (!g.bw.count)
			bandwidth_start(len);
		else
			bandwidth_update(len);
	}

	g.input.wpos += len;

	while (g.action());

	return GGI_OK;
}

static int
vnc_write(void *arg, uint32_t flag, struct gii_fdselect_fd *fd)
{
	int res;

	debug(2, "write rpos %d wpos %d\n", g.output.rpos, g.output.wpos);

	res = vnc_safe_write(g.sfd, g.output.data, g.output.wpos);

	if (res == g.output.wpos) {
#ifdef HAVE_INPUT_FDSELECT
		struct gii_fdselect_fd del_fd;

		del_fd.fd = fd->fd;
		del_fd.mode = GII_FDSELECT_WRITE;
		ggControl(g.fdselect->channel, GII_FDSELECT_DEL, &del_fd);
#endif /* HAVE_INPUT_FDSELECT */

		g.output.rpos = 0;
		g.output.wpos = 0;

		return GGI_OK;
	}

	if (res > 0) {
		g.output.rpos += res;
		memmove(g.output.data, &g.output.data[g.output.rpos],
			g.output.wpos - g.output.rpos);
		g.output.wpos -= g.output.rpos;
		g.output.rpos = 0;
		return GGI_OK;
	}

	/* error */
	return GGI_OK;
}

static int
vnc_fdselect(void *arg, uint32_t flag, void *data)
{
    struct gii_fdselect_fd *fd = (struct gii_fdselect_fd*)data;

	debug(2, "fdselect\n");

	if (flag != GII_FDSELECT_READY)
		return GGI_OK;

	if (fd->fd != g.sfd)
		return GGI_OK;

	if (fd->mode & GII_FDSELECT_WRITE)
		vnc_write(arg, flag, fd);

	if (fd->mode & GII_FDSELECT_READ)
		vnc_read(arg, flag, fd);

	return GGI_OK;
}

static int
transform_gii_key(int down, uint32_t key)
{
	if (g.no_input)
		return 0;

	switch (key) {
	case GIIUC_BackSpace: key = 0xff08;                            break;
	case GIIUC_Tab:       key = 0xff09;                            break;
	case GIIUC_Return:    key = 0xff0d;                            break;
	case GIIUC_Escape:    key = 0xff1b;                            break;
	case GIIK_Insert:     key = 0xff63;                            break;
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

	return vnc_key(down, key);
}

#ifndef HAVE_WIDGETS
static void
auto_scroll(uint8_t buttons, int x, int y)
{
	int move = 0;

	if (!g.scrollx && !g.scrolly)
		return;

	if (x < g.offset.x + 3 && g.slide.x) {
		move = 1;
		if (g.slide.x > 8)
			g.slide.x -= 8;
		else
			g.slide.x = 0;
	}
	if (x >= g.offset.x + g.area.x - 3 &&
		g.slide.x != g.width - g.area.x)
	{
		move = 1;
		if (g.slide.x < g.width - g.area.x - 8)
			g.slide.x += 8;
		else
			g.slide.x = g.width - g.area.x;
	}
	if (y < g.offset.y + 3 && g.slide.y) {
		move = 1;
		if (g.slide.y > 8)
			g.slide.y -= 8;
		else
			g.slide.y = 0;
	}
	if (y >= g.offset.y + g.area.y - 3 &&
		g.slide.y != g.height - g.area.y)
	{
		move = 1;
		if (g.slide.y < g.height - g.area.y - 8)
			g.slide.y += 8;
		else
			g.slide.y = g.height - g.area.y;
	}

	if (!move)
		return;

	debug(3, "auto_scroll crossblit\n");
	render_update();
	vnc_pointer(buttons, x + g.slide.x, y + g.slide.y);
}
#endif /* HAVE_WIDGETS */

static int
loop(void)
{
	static uint8_t buttons;
	static int x, y, wheel;
	int done = 0;
	int n;
#ifdef HAVE_INPUT_FDSELECT
	gii_event event;
	gii_event req_event;
#else
	ggi_event event;
	ggi_event req_event;
	ggi_event_mask mask;
	fd_set rfds;
	fd_set wfds;
	int res;
#endif

#ifdef HAVE_WMH
	ggiWmhAllowResize(g.stem, 40, 40, g.width, g.height, 1, 1);
#endif

again:
	req_event.any.size = 0;

#ifdef HAVE_INPUT_FDSELECT
	giiEventPoll(g.stem, emAll, NULL);
	n = giiEventsQueued(g.stem, emAll);
#else
	mask = emAll;
	FD_ZERO(&rfds);
	FD_SET(g.sfd, &rfds);
	FD_ZERO(&wfds);
	if (g.output.wpos)
		FD_SET(g.sfd, &wfds);
	res = ggiEventSelect(g.stem, &mask, g.sfd + 1,
		&rfds, &wfds, NULL, NULL);
	if (FD_ISSET(g.sfd, &rfds)) {
		struct gii_fdselect_fd fd;
		fd.mode = GII_FDSELECT_READ;
		fd.fd = g.sfd;
		vnc_read(NULL, GII_FDSELECT_READY, &fd);
	}
	if (FD_ISSET(g.sfd, &wfds)) {
		struct gii_fdselect_fd fd;
		fd.mode = GII_FDSELECT_WRITE;
		fd.fd = g.sfd;
		vnc_write(NULL, GII_FDSELECT_READY, &fd);
	}
	if (!mask) {
		if (done)
			return 0;
		goto again;
	}
	n = ggiEventsQueued(g.stem, emAll);
#endif

	while (n--) {
#ifdef HAVE_INPUT_FDSELECT
		giiEventRead(g.stem, &event, emAll);
#else
		ggiEventRead(g.stem, &event, emAll);
#endif

		if (scrollbar_process(&event))
			continue;

		switch(event.any.type) {
		case evKeyPress:
		case evKeyRepeat:
			if (event.key.sym == GIIK_F8 &&
				!(event.key.modifiers & GII_KM_MASK))
			{
#ifdef HAVE_WMH
				ggiWmhAllowResize(g.stem,
					g.mode.visible.x, g.mode.visible.y,
					g.mode.visible.x, g.mode.visible.y,
					1, 1);
#endif
				g.F8_allow_release = 0;
				switch (show_menu()) {
				case 1:
					transform_gii_key(1, GIIK_F8);
					transform_gii_key(0, GIIK_F8);
					break;
				case -2:
				case 2:
					done = 1;
					break;
				case 3:
					if (show_about() == -1)
						done = 1;
					break;
				case 4:
					transform_gii_key(1, GIIK_CtrlL);
					transform_gii_key(1, GIIK_AltL);
					transform_gii_key(1, GIIUC_Delete);
					transform_gii_key(0, GIIUC_Delete);
					transform_gii_key(0, GIIK_AltL);
					transform_gii_key(0, GIIK_CtrlL);
					break;
				case 5:
#ifdef GGIWMHFLAG_GRAB_HOTKEYS
					{
					uint32_t flags =
						ggiWmhGetFlags(g.stem)
						^ GGIWMHFLAG_GRAB_HOTKEYS;
					ggiWmhSetFlags(g.stem, flags);
					if (flags != ggiWmhGetFlags(g.stem))
						debug(1, "No grab\n");
					}
#endif
					break;
				case 6:
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
					vnc_send_cut_text();
#endif
					break;
				}
#ifdef HAVE_WMH
				ggiWmhAllowResize(g.stem, 40, 40,
					g.width, g.height, 1, 1);
#endif
				break;
			}
			if (event.key.sym == GIIK_F8)
				g.F8_allow_release = 1;
			if (gii_inject(&event))
				break;
			transform_gii_key(1, event.key.sym);
			break;

		case evKeyRelease:
			if (event.key.sym == GIIK_F8 && !g.F8_allow_release)
				break;
			if (gii_inject(&event))
				break;
			transform_gii_key(0, event.key.sym);
			break;

		case evPtrButtonPress:
			if (gii_inject(&event))
				break;
			switch (event.pbutton.button) {
			case GII_PBUTTON_LEFT:   buttons |= 1; break;
			case GII_PBUTTON_MIDDLE: buttons |= 2; break;
			case GII_PBUTTON_RIGHT:  buttons |= 4; break;
			}
			vnc_pointer(buttons, x + g.slide.x, y + g.slide.y);
			break;

		case evPtrButtonRelease:
			if (gii_inject(&event))
				break;
			switch (event.pbutton.button) {
			case GII_PBUTTON_LEFT:   buttons &= ~1; break;
			case GII_PBUTTON_MIDDLE: buttons &= ~2; break;
			case GII_PBUTTON_RIGHT:  buttons &= ~4; break;
			}
			vnc_pointer(buttons, x + g.slide.x, y + g.slide.y);
			break;

		case evPtrAbsolute:
			if (gii_inject(&event))
				break;
			if (event.pmove.x < g.offset.x)
				break;
			if (event.pmove.x >= g.offset.x + g.area.x)
				break;
			if (event.pmove.y < g.offset.y)
				break;
			if (event.pmove.y >= g.offset.y + g.area.y)
				break;
			x = event.pmove.x - g.offset.x;
			y = event.pmove.y - g.offset.y;
			if (event.pmove.wheel != wheel) {
				if (event.pmove.wheel > wheel)
					vnc_pointer(buttons | 8,
						x + g.slide.x, y + g.slide.y);
				else
					vnc_pointer(buttons | 16,
						x + g.slide.x, y + g.slide.y);
				wheel = event.pmove.wheel;
			}
			vnc_pointer(buttons, x + g.slide.x, y + g.slide.y);
			break;

		case evPtrRelative:
			if (gii_inject(&event))
				break;
			x += event.pmove.x;
			y += event.pmove.y;
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			if (x >= g.area.x) x = g.area.x - 1;
			if (y >= g.area.y) y = g.area.y - 1;
			if (event.pmove.wheel) {
				if (event.pmove.wheel > 0)
					vnc_pointer(buttons | 8,
						x + g.slide.x, y + g.slide.y);
				else
					vnc_pointer(buttons | 16,
						x + g.slide.x, y + g.slide.y);
			}
			vnc_pointer(buttons, x + g.slide.x, y + g.slide.y);
			break;

		case evValAbsolute:
		case evValRelative:
			if (gii_inject(&event))
				break;
			break;

		case evCommand:
			if (event.cmd.code == GGICMD_REQUEST_SWITCH) {
				ggi_cmddata_switchrequest swreq;
				memcpy(&swreq, event.cmd.data, sizeof(swreq));
				if (swreq.request == GGI_REQSW_MODE)
					req_event = event;
			}
#ifdef HAVE_GGNEWSTEM
			if (!g.gii)
				break;
			switch (event.cmd.code) {
			case GII_CMDCODE_DEVICE_INFO:
				gii_create_device(event.cmd.origin,
					(struct gii_cmddata_devinfo *)
					event.cmd.data);
				break;
			case GII_CMDCODE_DEVICE_CLOSE:
				gii_delete_device(&event);
				break;
			case GII_CMDCODE_EVENTLOST:
			case GII_CMDCODE_DEVICE_ENABLE:
			case GII_CMDCODE_DEVICE_DISABLE:
				gii_inject(&event);
			}
#endif /* HAVE_GGNEWSTEM */
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
					vnc_send_cut_text();
					break;
#endif /* GGIWMHFLAG_CLIPBOARD_CHANGE */
				}
			}
			break;
#endif /* GGIWMHFLAG_CATCH_CLOSE */
		}
#ifndef HAVE_WIDGETS
		auto_scroll(buttons, x, y);
#endif
	}

	if (done)
		return 0;

	if (req_event.any.size) {
		ggi_cmddata_switchrequest swreq;
		memcpy(&swreq, req_event.cmd.data, sizeof(swreq));
		vnc_resize(&swreq.mode);
	}

	goto again;
}

struct encoding_t {
	int32_t number;
	const char *name;
};

static const struct encoding_t encoding_table[] = {
	{      0, "raw" },
	{      1, "copyrect" },
	{      2, "rre" },
	{      4, "corre" },
	{      5, "hextile" },
#ifdef HAVE_ZLIB
	{      6, "zlib" },
	{      7, "tight" },
	{      8, "zlibhex" },
	{     16, "zrle" },
#ifdef HAVE_JPEG
	{    -23, "quality9" },
	{    -24, "quality8" },
	{    -25, "quality7" },
	{    -26, "quality6" },
	{    -27, "quality5" },
	{    -28, "quality4" },
	{    -29, "quality3" },
	{    -30, "quality2" },
	{    -31, "quality1" },
	{    -32, "quality0" },
#endif
#endif /* HAVE_ZLIB */
	{   -223, "desksize" },
	{   -224, "lastrect" },
#ifdef HAVE_ZLIB
	{   -247, "zip9" },
	{   -248, "zip8" },
	{   -249, "zip7" },
	{   -250, "zip6" },
	{   -251, "zip5" },
	{   -252, "zip4" },
	{   -253, "zip3" },
	{   -254, "zip2" },
	{   -255, "zip1" },
	{   -256, "zip0" },
#endif /* HAVE_ZLIB */
#ifdef HAVE_GGNEWSTEM
	{   -305, "gii" },
#endif
#ifdef HAVE_WMH
	{   -307, "deskname" },
#endif
	{ 0x574d5669, "wmvi" },
	{      0,  NULL }
};

static const int32_t default_encodings[] = {
	1,	/* copyrect */
#ifdef HAVE_ZLIB
	16,	/* zrle */
#endif
	5,	/* hextile */
#ifdef HAVE_ZLIB
	7,	/* tight */
	8,	/* zlibhex */
	6,	/* zlib */
#endif /* HAVE_ZLIB */
	2,	/* rre */
	4,	/* corre */
	0,	/* raw */
#if defined(HAVE_ZLIB) && defined(HAVE_JPEG)
	-27,	/* tight quality 5 */
#endif
	-223,	/* desksize */
	-224,	/* lastrect */
#ifdef HAVE_GGNEWSTEM
	-305,	/* gii */
#endif
#ifdef HAVE_WMH
	-307,	/* deskname */
#endif
	0x574d5669, /* wmvi */
	0       /* dummy */
};

const char *
lookup_encoding(int32_t number)
{
	int i;

	for (i = 0; encoding_table[i].name; ++i) {
		if (number == encoding_table[i].number)
			break;
	}

	return encoding_table[i].name;
}

int
get_default_encodings(const int32_t **encodings)
{
	if (encodings)
		*encodings = default_encodings;
	return sizeof(default_encodings) / sizeof(default_encodings[0]) - 1;
}

static int
parse_encodings(char *encstr)
{
	char *enc;
	char *end;
	uint16_t i, j;
	int32_t *encoding;

	g.encoding_count = 0;
	for (enc = encstr - 1; enc; enc = strchr(enc + 1, ',')) {
		if (!++g.encoding_count)
			return -1;
	}

	encoding = (int32_t *)malloc(g.encoding_count * sizeof(int32_t));
	if (!encoding)
		return -1;
	g.encoding = encoding;

	enc = encstr;
	for (i = 0; i < g.encoding_count; ++i) {
		end = strchr(enc, ',');
		if (end)
			*end = '\0';
		for (j = 0; encoding_table[j].name; ++j) {
			if (strcmp(enc, encoding_table[j].name))
				continue;
			encoding[i] = encoding_table[j].number;
			break;
		}
		if (!encoding_table[j].name)
			return -1;
		enc = end + 1;
	}

	return 0;
}

static int
parse_security(char *secstr)
{
	char *sec;
	char *end;
	uint16_t i, j;
	uint8_t *security;
	int count;

	count = 0;
	for (sec = secstr - 1; sec; sec = strchr(sec + 1, ',')) {
		if (!++count)
			return -1;
	}

    security = (uint8_t*)malloc(count + 1);
	if (!security)
		return -1;
	g.allow_security = security;
	security[count] = 0;

	sec = secstr;
	for (i = 0; i < count; ++i) {
		end = strchr(sec, ',');
		if (end)
			*end = '\0';
		for (j = 0; security_types[j].name; ++j) {
			if (strcmp(sec, security_types[j].name))
				continue;
			if (!security_types[j].weight)
				return -1;
			security[i] = security_types[j].number;
			break;
		}
		if (!security_types[j].name)
			return -1;
		sec = end + 1;
	}

	return 0;
}

void
select_mode(void)
{
	const char *str = getenv("GGI_DEFMODE");

	if (!str) {
		g.mode.frames = 2;
		g.mode.visible.x = g.width;
		g.mode.visible.y = g.height;
		g.mode.virt.x = GGI_AUTO;
		g.mode.virt.y = GGI_AUTO;
		g.mode.size.x = GGI_AUTO;
		g.mode.size.y = GGI_AUTO;
		g.mode.graphtype = GT_AUTO;
		g.mode.dpp.x = GGI_AUTO;
		g.mode.dpp.y = GGI_AUTO;
		return;
	}

	ggiParseMode(str, &g.mode);

	if (g.mode.visible.x == GGI_AUTO) {
		g.mode.visible.x = g.width;
		if (g.mode.virt.x != GGI_AUTO) {
			if (g.mode.visible.x > g.mode.virt.x)
				g.mode.visible.x = g.mode.virt.x;
		}
	}
	if (g.mode.visible.y == GGI_AUTO) {
		g.mode.visible.y = g.height;
		if (g.mode.virt.y != GGI_AUTO) {
			if (g.mode.visible.y > g.mode.virt.y)
				g.mode.visible.y = g.mode.virt.y;
		}
	}

	if (g.mode.frames == GGI_AUTO)
		g.mode.frames = 2;
}

static int
read_password(const char *file)
{
	FILE *f;
	static char passwd[9];

	memset(passwd, 0, sizeof(passwd));
	f = fopen(file, "rt");
	if (!f) {
		debug(0, "error opening passwd file: %s\n", file);
		return -1;
	}
	if (!fgets(passwd, sizeof(passwd), f)) {
		if (ferror(f)) {
			fclose(f);
			debug(0, "error reading passwd file: %s\n", file);
			return -1;
		}
	}
	fclose(f);
	if (passwd[0] && passwd[strlen(passwd) - 1] == '\n')
		passwd[strlen(passwd) - 1] = '\0';

	g.passwd = passwd;
	return 0;
}

int
parse_port(void)
{
	char *port = g.server;
	char *end;
	char *fix;

	if (g.server[0] == '[') {
		end = strrchr(g.server, ']');
		if (end) {
			memmove(g.server, g.server + 1, end - g.server - 1);
			memmove(end - 1, end + 1, strlen(end));
			port = end - 1;
		}
	}

	fix = port = strchr(port, ':');
	if (!port)
		g.port = 5900;
	else {
		*port++ = '\0';
		if (*port == ':') {
			++port;
			g.port = 0;
		}
		else
			g.port = 5900;
		g.port += strtoul(port, &end, 10);
		if ((port == end) || *end) {
			*fix = ':';
			return -1;
		}
	}
	return 0;
}

static int
parse_protocol(const char *protocol)
{
	unsigned int major, minor;
	int count;
	char normalized[12];

	count = strlen(protocol);
	if (sscanf(protocol, "%u.%u", &major, &minor) != 2)
		goto error;
	if (major > 999 || minor > 999)
		goto error;
	sprintf(normalized, "%u.%u", major, minor);
	if (strcmp(protocol, normalized)) {
		sprintf(normalized, "%03u.%03u", major, minor);
		if (strcmp(protocol, normalized))
			goto error;
	}
	if ((major == 3 && minor < 3) || major < 3)
		/* 3.3 is the lowest protocol version supported. */
		goto error;
	if (major == 3 && minor >= 4 && minor <= 6)
		/* 3.4 and 3.6 have been hijacked by UltraVNC, 3.5 was
		 * never released. Max out at 3.3 in these cases...
		 */
		minor = 3;
	if ((major == 3 && minor > 8) || major > 3) {
		/* 3.8 is the highest protocol version supported. */
		major = 3;
		minor = 8;
	}
	g.max_protocol = minor;
	return 0;

error:
	debug(0, "Bad RFB protocol version \"%s\" specified\n", protocol);
	return -1;
}

static int
parse_listen(const char *display)
{
	int port;
	unsigned short base = 5500;

	if (!display) {
		g.listen = base;
		return 0;
	}
	if (*display == ':') {
		++display;
		base = 0;
	}
	if (sscanf(display, "%d", &port) != 1)
		goto error;
	g.listen = base + port;
	return 0;

error:
	debug(0, "Bad 'listen' port \"%s\" specified\n", display);
	return -1;
}

static int
parse_bind(const char *iface)
{
	struct hostent *h;

	if (socket_init())
		return -1;

	h = gethostbyname(iface);
	if (!h) {
		debug(0, "gethostbyname error\n");
		goto error;
	}
	if (h->h_addrtype != AF_INET) {
		debug(0, "address family does not match\n");
		goto error;
	}

	if (g.bind)
		free(g.bind);
	g.bind = strdup(iface);
	if (!g.bind) {
		debug(0, "Out of memory\n");
		goto error;
	}
	socket_cleanup();
	return 0;

error:
	debug(0, "bind interface \"%s\" not found\n", iface);
	socket_cleanup();
	return -1;
}

#ifdef HAVE_GETADDRINFO

static int
vnc_connect(void)
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
	switch (g.net_family) {
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

	snprintf(port, sizeof(port), "%d", g.port);

	res = getaddrinfo(g.server, port, &hints, &gai);
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
		g.sfd = res;
		res = connect(g.sfd, ai->ai_addr, ai->ai_addrlen);
		if (!res)
			break;
		close(g.sfd);
		g.sfd = -1;
		debug(1, "connect\n");
	}

	if (!ai)
		fprintf(stderr, "cannot reach %s\n", g.server);

	freeaddrinfo(gai);
	return res;
}

#define MAXSOCK	3

static int
vnc_listen(void)
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

	sprintf(str_port, "%d", g.listen);

	memset(&hints, 0, sizeof(hints));
	switch (g.net_family) {
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
	res = getaddrinfo(g.bind, str_port, &hints, &gai);
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
		g.sfd = accept(fd, (struct sockaddr *)&sa, &sa_len);
	} while (g.sfd == -1 && errno == EINTR);

	close(fd);
	return g.sfd == -1 ? -1 : 0;
}

#else /* HAVE_GETADDRINFO */

static int
vnc_connect(void)
{
	struct hostent *h;
	struct sockaddr_in sa;

	h = gethostbyname(g.server);

	if (!h) {
		fprintf(stderr, "%s: %s\n",
			hstrerror(h_errno), g.server);
		return -1;
	}
	if (h->h_addrtype != AF_INET) {
		fprintf(stderr, "host not reachable via IPv4\n");
		return -1;
	}

	g.sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (g.sfd == -1) {
		fprintf(stderr, "socket\n");
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr = *((struct in_addr *)h->h_addr);
	sa.sin_port = htons(g.port);

	if (connect(g.sfd, (struct sockaddr *)&sa, sizeof(sa))) {
		fprintf(stderr, "connect\n");
		close(g.sfd);
		return -1;
	}

	return 0;
}

static int
vnc_listen(void)
{
	int fd;
	struct hostent *h;
	struct sockaddr_in sa;
#ifdef HAVE_SOCKLEN_T
	socklen_t sa_len = sizeof(sa);
#else
	int sa_len = sizeof(sa);
#endif

	fd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (fd == -1)
		return fd;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(g.listen);

	if (g.bind) {
		h = gethostbyname(g.bind);
		if (!h) {
			debug(1, "gethostbyname error\n");
			goto error;
		}
		if (h->h_addrtype != sa.sin_family) {
			debug(1, "address family does not match\n");
			goto error;
		}
		sa.sin_addr = *((struct in_addr *)h->h_addr);
	}

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa))) {
		debug(1, "bind failed\n");
		goto error;
	}

	if (listen(fd, 1)) {
		debug(1, "listen failed\n");
		goto error;
	}

again:
	g.sfd = accept(fd, (struct sockaddr *)&sa, &sa_len);
	if (g.sfd == -1 && errno == EINTR)
		goto again;

	close(fd);
	return g.sfd == -1 ? -1 : 0;

error:
	close(fd);
	return -1;
}

#endif /* HAVE_GETADDRINFO */

#ifdef HAVE_GGNEWSTEM

#ifndef HAVE_WMH
#define libggiwmh libggi /* attaching ggi twice does no harm */
#endif

int
open_visual(void)
{
	if (g.stem)
		return 0;

	if (ggInit() < 0)
		return -1;
	g.stem = ggNewStem(libgii, libggi, libggiwmh, NULL);
	if (!g.stem)
		goto err_ggexit;
	if (ggiOpen(g.stem, NULL) < 0)
		goto err_ggdelstem;

	if (g.gii_input) {
		if (giiOpen(g.stem, g.gii_input, NULL) <= 0)
			/* zero is also bad, no match */
			goto err_ggiclose;
	}

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif
#ifdef HAVE_WIDGETS
	g.visualanchor = ggiWidgetCreateContainerVisualAnchor(g.stem);
#endif

	set_icon();

	return 0;

err_ggiclose:
	ggiClose(g.stem);
err_ggdelstem:
	ggDelStem(g.stem);
	g.stem = NULL;
err_ggexit:
	ggExit();
	return -1;
}

#else /* HAVE_GGNEWSTEM */

int
open_visual(void)
{
	if (g.stem)
		return 0;

	if (ggiInit() < 0)
		return -1;
	g.stem = ggiOpen(NULL);
	if (!g.stem)
		goto err_ggiexit;

	if (g.gii_input) {
		gii_input_t inp = giiOpen(g.gii_input, NULL);
		if (!inp)
			goto err_ggiclose;
		if (!ggiJoinInputs(g.stem, inp))
			goto err_ggiclose;
	}

#if defined(HAVE_WMH)
	if (ggiWmhInit() < 0)
		goto err_ggiclose;
	if (ggiWmhAttach(g.stem) < 0)
		goto err_wmhexit;
#endif

	return 0;

#if defined(HAVE_WMH)
err_wmhexit:
	ggiWmhExit();
#endif
err_ggiclose:
	ggiClose(g.stem);
	g.stem = NULL;
err_ggiexit:
	ggiExit();
	return -1;
}

#endif /* HAVE_GGNEWSTEM */

void
close_visual(void)
{
	if (!g.stem)
		return;

#ifdef HAVE_WIDGETS
	{
		ggi_widget_t visualanchor = g.visualanchor;
		visualanchor->destroy(visualanchor);
		g.visualanchor = NULL;
	}
#endif
	ggiClose(g.stem);
#if defined(HAVE_WMH) && !defined(HAVE_GGNEWSTEM)
	ggiWmhDetach(g.stem);
	ggiWmhExit();
#endif
#ifdef HAVE_GGNEWSTEM
	ggDelStem(g.stem);
	g.stem = NULL;
	ggExit();
#else
	ggiExit();
#endif
}

static const char help[] =
"\
Usage: %s [options] [server[:<display>|::<port>]]\n"
"\
If 'server' contains a colon or starts with a literal '[', quote it with\n\
literal square brackets. E.g. [::ffff:192.168.1.1] (for IPv6 users).\n"
#ifndef HAVE_WIDGETS
"\n\
If you do not supply a 'server', you have to supply the --listen option.\n"
#endif
"\n\
Options:\n\
  -a, --alone\n\
      alone - don't request a shared session\n\
  --shared\n\
      the default, request a shared session\n\
  -b, --bind <interface>\n\
      the interface to listen to.\n\
  -d, --debug\n\
      increase debug output level\n\
  -e, --encodings <encodings>\n"
#ifdef HAVE_ZLIB
#ifdef HAVE_JPEG
#ifdef HAVE_WMH
"\
      comma separated list of: copyrect,zrle,hextile,tight,zlibhex,zlib,\n\
      rre,corre,raw,quality0-9,gii,lastrect,desksize,deskname,wmvi,zip0-9,\n\
      in order of preference.\n"
#else /* HAVE_WMH */
"\
      comma separated list of: copyrect,zrle,hextile,tight,zlibhex,zlib,\n\
      rre,corre,raw,quality0-9,gii,lastrect,desksize,wmvi,zip0-9, in order\n\
      of preference.\n"
#endif /* HAVE_WMH */
#else /* HAVE_JPEG */
#ifdef HAVE_WMH
"\
      comma separated list of: copyrect,zrle,hextile,tight,zlibhex,zlib,\n\
      rre,corre,raw,gii,lastrect,desksize,deskname,wmvi,zip0-9, in order of\n\
      preference.\n"
#else /* HAVE_WMH */
"\
      comma separated list of: copyrect,zrle,hextile,tight,zlibhex,zlib,\n\
      rre,corre,raw,gii,lastrect,desksize,wmvi,zip0-9, in order of\n\
      preference.\n"
#endif /* HAVE_WMH */
#endif /* HAVE_JPEG */
#else /* HAVE_ZLIB */
#ifdef HAVE_WMH
"\
      comma separated list of: copyrect,hextile,rre,corre,raw,gii,\n\
      lastrect,desksize,deskname,wmvi, in order of preference.\n"
#else /* HAVE_WMH */
"\
      comma separated list of: copyrect,hextile,rre,corre,raw,gii,\n\
      lastrect,desksize,wmvi, in order of preference.\n"
#endif /* HAVE_WMH */
#endif /* HAVE_ZLIB */
"\
  -E, --endian <endian>\n\
      specify little or big endian\n\
  -f, --pixfmt <pixfmt>\n\
      pixfmt is either r<bits>g<bits>b<bits> (in any order, insert p<bits>\n\
      as desired for padding), c<bits>, server or local\n\
  --gii <input>\n\
      extra gii input target to load\n\
  -h, --help\n\
      prints this help text and exits\n\
  -i, --no-input\n\
      no input - peek only, don't poke\n\
  -l, --listen[=<display>|=:<port>]\n\
      operate in reverse, i.e. listen for connections\n\
  -p, --password <password>\n\
      file containing password, beware, max 8 (7-bit) characters are used\n\
      in the password\n\
  --rfb <version>\n\
      the maximum rfb protocol version to use (3.3, 3.7 or 3.8)\n\
  -s, --security-types <security-types>\n\
      comma separated list of: none,vnc-auth,tight, in order of preference.\n\
      If the server does not support any of these security types the first\n\
      is requested anyway.\n\
  -v, --version\n\
      prints the version of this software and exits\n"
#ifdef HAVE_GETADDRINFO
"\
  -4, --ipv4\n\
      only attempt IPv4 connections\n"
#ifdef PF_INET6
"\
  -6, --ipv6\n\
      only attempt IPv6 connections\n"
#endif /* PF_INET6 */
#endif /* HAVE_GETADDRINFO */
;

#ifdef HAVE_GETADDRINFO
#ifdef PF_INET6
#define PF_OPTS "46"
#else
#define PF_OPTS "4"
#endif /* PF_INET6 */
#else
#define PF_OPTS ""
#endif /* HAVE_GETADDRINFO */


static const char *
remove_path(const char *file)
{
	const char *tmp;

	tmp = file - 1;
	while (tmp) {
		file = tmp + 1;
		tmp = strchr(file, '/');
	}

	tmp = file - 1;
	while (tmp) {
		file = tmp + 1;
		tmp = strchr(file, '\\');
	}

	return file;
}

boost::signals2::connection connectToTestSignal
    (
    const TestSignalType::slot_type& aSlot
    )
{
    return mTestEvent.connect( aSlot );
}

boost::function<void (const std::string&)> ggivnc_func; 

void register_signal_handle_function( boost::function<void (const std::string&)> f )
{
    ggivnc_func = f;
}
//int
//ggivnc_main(int argc, char * const argv[])
int ggivnc_main(int argc, char **argv)
{
        //f = boost::bind( &MLLibrary::MLVNC::onHandleSignal,MLLibrary::MLVNC::getInstance(), _1 );
// connect signal
        //connectToTestSignal( 
        //  boost::bind( &MLLibrary::MLVNC::onHandleSignal,MLLibrary::MLVNC::getInstance(), _1 ) );
        connectToTestSignal( ggivnc_func );
	int c;
	int status = 0;
#ifdef HAVE_INPUT_FDSELECT
	struct gii_fdselect_fd fd;
#endif
	long flags;

	g.port = 5900;
	g.shared = 1;
	g.listen = 0;
	g.bind = NULL;
	g.wire_endian = -1;
	g.auto_encoding = 1;
	g.encoding_count =
		sizeof(default_encodings) / sizeof(default_encodings[0]) - 1;
	g.encoding = default_encodings;
	g.max_protocol = 8;
#ifdef HAVE_GGNEWSTEM
	GG_LIST_INIT(&g.devices);
#endif

	console_init();

	opterr = 1;

	do {
		int longidx;
		struct option longopts[] = {
			{ "alone",         0, NULL, 'a' },
			{ "shared",        0, NULL, '$' },
			{ "bind",          1, NULL, 'b' },
			{ "debug",         0, NULL, 'd' },
			{ "encodings",     1, NULL, 'e' },
			{ "endian",        1, NULL, 'E' },
			{ "pixfmt",        1, NULL, 'f' },
			{ "gii",           1, NULL, '%' },
			{ "help",          0, NULL, 'h' },
			{ "no-input",      0, NULL, 'i' },
			{ "listen",        2, NULL, 'l' },
			{ "password",      1, NULL, 'p' },
			{ "rfb",           1, NULL, '#' },
			{ "security-type", 1, NULL, 's' },
			{ "version",       0, NULL, 'v' },
#ifdef HAVE_GETADDRINFO
			{ "ipv4",          0, NULL, '4' },
#ifdef PF_INET6
			{ "ipv6",          0, NULL, '6' },
#endif
#endif /* HAVE_GETADDRINFO */
			{ 0, 0, 0, 0 }
		};

		c = getopt_long_only(argc, argv,
			"ab:de:E:f:hil::p:s:v" PF_OPTS,
			longopts, &longidx);
		switch (c) {
		case 'a':
			g.shared = 0;
			break;
		case '$':
			g.shared = 1;
			break;
		case 'b':
			if (parse_bind(optarg))
				status = 2;
			break;
		case 'd':
			++g.debug;
			break;
		case 'e':
			if (parse_encodings(optarg)) {
				fprintf(stderr, "bad encoding\n");
				status = 2;
			}
			g.auto_encoding = 0;
			break;
		case 'E':
			if (!strcmp(optarg, "little"))
				g.wire_endian = 0;
			else if (!strcmp(optarg, "big"))
				g.wire_endian = 1;
			else {
				fprintf(stderr, "bad endian\n");
				status = 2;
			}
			break;
		case 'f':
			if (ggstrlcpy(g.wire_pixfmt, optarg,
					sizeof(g.wire_pixfmt))
				>= sizeof(g.wire_pixfmt))
			{
				fprintf(stderr, "pixfmt too long\n");
				status = 2;
			}
			else if (!strcmp(g.wire_pixfmt, "local")) {
				if (g.wire_endian < 0)
					g.wire_endian = -2;
			}
			else if (!strcmp(g.wire_pixfmt, "server")) {
				if (g.wire_endian < 0)
					g.wire_endian = -3;
			}
			else if (canonicalize_pixfmt(
				g.wire_pixfmt, sizeof(g.wire_pixfmt)))
			{
				fprintf(stderr, "bad pixfmt\n");
				status = 2;
			}
			break;
		case '%':
			g.gii_input = optarg;
			break;
		case 'h':
			fprintf(stderr, help, remove_path(argv[0]));
			return 0;
		case 'i':
			g.no_input = 1;
			break;
		case 'l':
			if (parse_listen(optarg))
				status = 2;
			break;
		case 'p':
			if (read_password(optarg))
				status = 2;
			break;
		case '#':
			if (parse_protocol(optarg))
				status = 2;
			break;
		case 's':
			if (parse_security(optarg)) {
				fprintf(stderr, "bad security type\n");
				status = 2;
			}
			break;
		case 'v':
			fprintf(stderr, "%s version %s\n",
				remove_path(argv[0]), PACKAGE_VERSION);
			return 0;
		case '4':
			g.net_family = 4;
			break;
		case '6':
			g.net_family = 6;
			break;
		case ':':
		case '?':
			status = 2;
			break;
		}
	} while (c != -1);

	if (socket_init()) {
		fprintf(stderr, "Winsock init failure\n");
		return 3;
	}

	if (g.listen) {
		if (optind < argc) {
			fprintf(stderr,
				"Bad, both listening and connecting...\n");
			status = 2;
		}
	}
	else if (optind + 1 == argc) {
		g.server = argv[optind];
		if (parse_port()) {
			fprintf(stderr,
				"Bad display or port specified\n");
			status = 2;
		}
	}
	else if (!status) {
		status = get_connection();
		switch (status) {
		case -1:
			return 0;
		case -2:
			return 4;
		}
	}

	if (status) {
		fprintf(stderr, help, remove_path(argv[0]));
		return status;
	}

	if (!g.wire_pixfmt[0])
		strcpy(g.wire_pixfmt, "local");

	status = 1;

	if (g.listen) {
		if (vnc_listen())
			goto err;

		if (open_visual())
			goto err;

		if (set_title())
			goto err;
	}
	else {
		if (open_visual())
			goto err;

		if (set_title())
			goto err;

		if (vnc_connect())
			goto err;
	}

	if (vnc_version()) {
		fprintf(stderr, "vnc_version\n");
		goto err_closesfd;
	}

	if (vnc_security()) {
		fprintf(stderr, "vnc_security\n");
		goto err_closesfd;
	}

	if (vnc_init()) {
		fprintf(stderr, "vnc_init\n");
		goto err_closesfd;
	}

	select_mode();

	ggiCheckMode(g.stem, &g.mode);

	if (ggiSetMode(g.stem, &g.mode))
		goto err_closesfd;

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(g.stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	if (set_title()) {
		fprintf(stderr, "set_title\n");
		goto err_closesfd;
	}

	g.area.x = g.width;
	g.area.y = g.height;
	scrollbar_area();

	if (g.mode.visible.x - g.scrolly * SCROLL_SIZE > g.width)
		g.offset.x = (g.mode.visible.x - g.scrolly * SCROLL_SIZE -
			g.width) / 2;
	else
		g.offset.x = 0;
	if (g.mode.visible.y - g.scrollx * SCROLL_SIZE > g.height)
		g.offset.y = (g.mode.visible.y - g.scrollx * SCROLL_SIZE -
			g.height) / 2;
	else
		g.offset.y = 0;

#ifndef HAVE_WIDGETS
	if (g.mode.visible.x < g.width)
		g.scrollx = 1;
	if (g.mode.visible.y < g.height)
		g.scrolly = 1;
	g.area = g.mode.visible;
#endif

	ggiSetFlags(g.stem, GGIFLAG_ASYNC);
	ggiSetColorfulPalette(g.stem);

	generate_pixfmt(g.local_pixfmt, sizeof(g.local_pixfmt),
		ggiGetPixelFormat(g.stem));
#ifdef GGI_BIG_ENDIAN
	g.local_endian = 1;
#else
	g.local_endian = 0;
#endif
	if (GT_SUBSCHEME(g.mode.graphtype) & GT_SUB_REVERSE_ENDIAN)
		g.local_endian = !g.local_endian;

	if (!strcmp(g.wire_pixfmt, "local")) {
		strcpy(g.wire_pixfmt, g.local_pixfmt);
		if (!strcmp(g.wire_pixfmt, "weird"))
			strcpy(g.wire_pixfmt, "r5g6b5");
	}

	if (!strcmp(g.wire_pixfmt, "server")) {
		strcpy(g.wire_pixfmt, g.server_pixfmt);
		if (!strcmp(g.wire_pixfmt, "weird"))
			strcpy(g.wire_pixfmt, "r5g6b5");
	}

	switch (g.wire_endian) {
	case -1:
	case -2:
		g.wire_endian = g.local_endian;
		break;
	case -3:
		g.wire_endian = g.server_endian;
		break;
	}

	g.work.rpos = 0;
	g.work.wpos = 0;
	g.work.size = 65536;
    g.work.data = (uint8_t*)malloc(g.work.size);
	if (!g.work.data)
		goto err_closesfd;

	if (vnc_tight_init())
		goto err_closesfd;
	if (vnc_zlib_init())
		goto err_closesfd;
	if (vnc_zlibhex_init())
		goto err_closesfd;
	if (vnc_zrle_init())
		goto err_closesfd;

	debug(1, "wire pixfmt: %s, %s endian\n",
		g.wire_pixfmt,
		g.wire_endian ? "big" : "little");

	if (vnc_set_pixel_format())
		goto err_closesfd;

	if (vnc_set_encodings())
		goto err_closesfd;

	if (vnc_update_request(0))
		goto err_closesfd;

#if defined(F_GETFL)
	flags = fcntl(g.sfd, F_GETFL);
	fcntl(g.sfd, F_SETFL, flags | O_NONBLOCK);
#elif defined(FIONBIO)
	flags = 1;
	ioctlsocket(g.sfd, FIONBIO, &flags);
#endif

#ifdef HAVE_INPUT_FDSELECT
	g.fdselect = ggPlugModule(libgii, g.stem,
		"input-fdselect", "-notify=fd", NULL);
	if (!g.fdselect) {
		fprintf(stderr, "Unable to open input-fdselect\n");
		goto err_closesfd;
	}

	fd.fd = g.sfd;
	fd.mode = GII_FDSELECT_READ;
	ggControl(g.fdselect->channel, GII_FDSELECT_ADD, &fd);
	ggObserve(g.fdselect->channel, vnc_fdselect, NULL);
#endif /* HAVE_INPUT_FDSELECT */

	g.action = vnc_wait;

	scrollbar_create();

	if (g.mode.frames > 1 && !g.wire_stem) {
		debug(1, "going double buffer\n");
		ggiSetWriteFrame(g.stem, 1);
		ggiSetReadFrame(g.stem, 1);
	}

	if (loop())
		goto err_closefdselect;

	status = 0;

err_closefdselect:
#ifdef HAVE_INPUT_FDSELECT
	ggClosePlugin(g.fdselect);
#endif
err_closesfd:
	if (g.work.data)
		free(g.work.data);
	close(g.sfd);
err:
	close_visual();
	socket_cleanup();

	return status;
}
