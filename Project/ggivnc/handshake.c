/*
******************************************************************************

   VNC viewer RFB protocol handshake.

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
#endif

#include "vnc.h"
#include "handshake.h"
#include "vnc-compat.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

#ifdef HAVE_OPENSSL
#define ALLOW_VENCRYPT 1
#else
#define ALLOW_VENCRYPT 0
#endif

const struct security security_types[] = {
	{ 1,    2, "none" },
	{ 2,    1, "vnc-auth" },
	{ 5,    0, "ra2" },
	{ 6,    0, "ra2ne" },
	{ 7,    0, "sspi" },
	{ 8,    0, "sspine" },
	{ 16,   4, "tight" },
	{ 17,   0, "ultra" },
	{ 18,   0, "tls" },
	{ 19,   3 * ALLOW_VENCRYPT, "vencrypt" },
	{ 20,   0, "sasl" },
	{ 21,   0, "md5" },
	{ 256, -1 * ALLOW_VENCRYPT, "plain" },
	{ 257,  6 * ALLOW_VENCRYPT, "tls-none" },
	{ 258,  5 * ALLOW_VENCRYPT, "tls-vnc" },
	{ 259, -1 * ALLOW_VENCRYPT, "tls-plain" },
	{ 260,  9 * ALLOW_VENCRYPT, "x509-none" },
	{ 261,  8 * ALLOW_VENCRYPT, "x509-vnc" },
	{ 262,  7 * ALLOW_VENCRYPT, "x509-plain" },
	{ 0,    0, NULL }
};

const char *
security_name(uint32_t type)
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

int
vnc_finish_handshake(struct connection *cx)
{
	debug(2, "vnc_finish_handshake\n");

	cx->action = NULL;
	return 0;
}

static int
vnc_drain_server_name(struct connection *cx)
{
	const int max_chunk = 0x100000;
	uint32_t rest;
	uint32_t limit;
	int chunk;

	debug(2, "vnc_drain_server_name\n");

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

	if (cx->security_tight) {
		cx->action = vnc_security_tight_init;
		return 1;
	}

	return vnc_finish_handshake(cx);
}

static int
vnc_server_name(struct connection *cx)
{
	const uint32_t max_length = 2000;
	uint32_t name_length;
	int length;

	debug(2, "vnc_server_name\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	name_length = get32_hilo(&cx->input.data[cx->input.rpos]);
	length = name_length < max_length ? name_length : max_length;

	if (cx->input.wpos < cx->input.rpos + 4 + length)
		return 0;

	cx->name = malloc(length + 1);
	if (!cx->name)
		return close_connection(cx, -1);

	memcpy(cx->name, &cx->input.data[cx->input.rpos + 4], length);
	cx->name[length] = '\0';

	debug(1, "Desktop name: \"%s\"\n", cx->name);

	if (name_length > max_length) {
		/* read out the tail of the name */
		cx->input.rpos += length;
		remove_dead_data(&cx->input);
		name_length -= max_length;
		insert32_hilo(cx->input.data, name_length);

		cx->action = vnc_drain_server_name;
		return 1;
	}

	cx->input.rpos += sizeof(name_length) + length;
	remove_dead_data(&cx->input);

	if (cx->security_tight) {
		cx->action = vnc_security_tight_init;
		return 1;
	}

	return vnc_finish_handshake(cx);
}

static int
vnc_server_init(struct connection *cx)
{
	uint16_t red_max;
	uint16_t green_max;
	uint16_t blue_max;
	uint32_t name_length;
	ggi_pixelformat server_ggi_pf;

	debug(2, "vnc_server_init\n");

	if (cx->input.wpos < cx->input.rpos + 24)
		return 0;

	cx->width = get16_hilo(&cx->input.data[cx->input.rpos]);
	cx->height = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
	red_max = get16_hilo(&cx->input.data[cx->input.rpos + 8]);
	green_max = get16_hilo(&cx->input.data[cx->input.rpos + 10]);
	blue_max = get16_hilo(&cx->input.data[cx->input.rpos + 12]);
	name_length = get32_hilo(&cx->input.data[cx->input.rpos + 20]);

	memset(&server_ggi_pf, 0, sizeof(server_ggi_pf));
	server_ggi_pf.size = cx->input.data[cx->input.rpos + 4];
	if (cx->input.data[cx->input.rpos + 7]) {
		server_ggi_pf.depth = color_bits(red_max) +
			color_bits(green_max) +
			color_bits(blue_max);
		if (server_ggi_pf.size < server_ggi_pf.depth)
			server_ggi_pf.depth =
				cx->input.data[cx->input.rpos + 5];
		server_ggi_pf.red_mask =
			red_max << cx->input.data[cx->input.rpos + 14];
		server_ggi_pf.green_mask =
			green_max << cx->input.data[cx->input.rpos + 15];
		server_ggi_pf.blue_mask =
			blue_max << cx->input.data[cx->input.rpos + 16];
	}
	else {
		server_ggi_pf.depth = cx->input.data[cx->input.rpos + 5];
		server_ggi_pf.clut_mask = (1 << server_ggi_pf.depth) - 1;
	}
	cx->server_endian = !!cx->input.data[cx->input.rpos + 6];

	generate_pixfmt(cx->server_pixfmt, sizeof(cx->server_pixfmt),
		&server_ggi_pf);

	if (name_length) {
		cx->input.rpos += 20;
		remove_dead_data(&cx->input);

		cx->action = vnc_server_name;
		return 1;
	}

	if (cx->name)
		free(cx->name);
	cx->name = NULL;

	cx->input.rpos += 24;
	remove_dead_data(&cx->input);

	if (cx->security_tight) {
		cx->action = vnc_security_tight_init;
		return 1;
	}

	return vnc_finish_handshake(cx);
}

int
vnc_client_init(struct connection *cx)
{
	debug(2, "vnc_client_init\n");

	if (safe_write(cx, &cx->shared, sizeof(cx->shared)))
		return close_connection(cx, -1);

	cx->action = vnc_server_init;
	return 1;
}

int
vnc_security_result(struct connection *cx)
{
	uint32_t security_result;

	debug(2, "vnc_security_result\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	security_result = get32_hilo(&cx->input.data[cx->input.rpos]);

	if (security_result) {
		debug(1, "security result %u\n", security_result);
		return close_connection(cx, -1);
	}

	cx->input.rpos += sizeof(security_result);
	remove_dead_data(&cx->input);

	cx->action = vnc_client_init;
	return 1;
}

int
vnc_handle_security(struct connection *cx)
{
	debug(2, "vnc_handle_security\n");

	switch (cx->security) {
	case 1:
		cx->action = vnc_security_none;
		return 1;

	case 2:
		cx->action = vnc_auth;
		return 1;

	case 16:
		cx->action = vnc_security_tight;
		return 1;

	case 19:
		cx->action = vnc_security_vencrypt;
		return 1;
	}

	return close_connection(cx, -1);
}

static int
vnc_security(struct connection *cx)
{
	uint8_t count;
	uint8_t types[255];
	int i, j;
	int current_weight;
	int weight = -1;
	int security = -1;
	uint8_t buf[1];

	debug(2, "vnc_security\n");

	cx->security_tight = 0;

	if (cx->protocol < 7) {
		uint32_t type;

		if (cx->input.wpos < cx->input.rpos + 4)
			return 0;

		type = get32_hilo(&cx->input.data[cx->input.rpos]);
		cx->input.rpos += sizeof(type);

		for (j = 0; cx->allow_security[j]; ++j)
			if (cx->allow_security[j] == type)
				break;
		if (!cx->allow_security[j])
			return close_connection(cx, -1);

		debug(1, "Security type %d - %s\n",
			type, security_name(type));

		if (type == 16)
			return close_connection(cx, -1);

		cx->security = type;
		remove_dead_data(&cx->input);
		cx->action = vnc_handle_security;
		return 1;
	}

	if (cx->input.wpos < cx->input.rpos + 1)
		return 0;
	count = cx->input.data[cx->input.rpos];

	if (!count) {
		debug(1, "No security types?\n");
		return close_connection(cx, -1);
	}

	if (cx->input.wpos < cx->input.rpos + 1 + count)
		return 0;
	memcpy(types, &cx->input.data[cx->input.rpos + 1], count);

	cx->input.rpos += sizeof(count) + count;

	for (i = 0; i < count; ++i) {

		debug(1, "Security type: %d - %s\n",
			types[i], security_name(types[i]));

		if (!types[i])
			return close_connection(cx, -1);

		for (j = 0; cx->allow_security[j]; ++j)
			if (cx->allow_security[j] == types[i])
				break;
		if (!cx->allow_security[j])
			continue;
		current_weight = 256 - j;

		if (current_weight && current_weight > weight) {
			weight = current_weight;
			security = types[i];
		}
	}

	if (weight == -1 && cx->force_security) {
		/* No allowed security type found, be a little dirty and
		 * request the first allowed security type. The server
		 * just might allow it...
		 */
		for (j = 0; cx->allow_security[j] >= 256; ++j);
		security = cx->allow_security[j];
	}

	if (security < 0)
		return close_connection(cx, -1);

	cx->security = security;

	buf[0] = cx->security;
	if (safe_write(cx, buf, sizeof(buf)))
		return close_connection(cx, -1);

	remove_dead_data(&cx->input);
	cx->action = vnc_handle_security;
	return 1;
}

static int
vnc_version(struct connection *cx)
{
	unsigned int major, minor;
	char str[13];
	const int ver_len = sizeof(str) - 1;

	debug(2, "vnc_version\n");

	if (cx->input.wpos < cx->input.rpos + ver_len)
		return 0;

	memcpy(str, &cx->input.data[cx->input.rpos], ver_len);
	str[ver_len] = '\0';
	major = atoi(str + 4);
	minor = atoi(str + 8);
	if (major > 999 || minor > 999) {
		debug(1, "Invalid protocol version "
			"(%s -> %d.%d) requested\n",
			str, major, minor);
		return close_connection(cx, -1);
	}
	sprintf(str, "RFB %03u.%03u\n", major, minor);
	if (memcmp(str, &cx->input.data[cx->input.rpos], ver_len)) {
		debug(1, "Invalid protocol version requested\n");
		return close_connection(cx, -1);
	}

	debug(1, "Server has protocol version RFB %03u.%03u\n", major, minor);
	if ((major == 3 && minor < 3) || major < 3) {
		/* 3.3 is the lowest protocol version supported.
		 * Bail.
		 */
		debug(1, "Protocol version not supported\n");
		return close_connection(cx, -1);
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

	if (minor > cx->max_protocol)
		minor = cx->max_protocol;

	sprintf(str, "RFB %03u.%03u\n", major, minor);
	debug(1, "Using protocol version RFB %03u.%03u\n", major, minor);

	if (safe_write(cx, str, ver_len))
		return close_connection(cx, -1);

	cx->protocol = minor;

	cx->input.rpos += ver_len;

	remove_dead_data(&cx->input);
	cx->action = vnc_security;
	return 1;
}

int
vnc_handshake(struct connection *cx)
{
	fd_set rfds;
	fd_set wfds;
	int res;

	cx->action = vnc_version;

	while (cx->action && !cx->close_connection) {
		FD_ZERO(&rfds);
		if (cx->want_read)
			FD_SET(cx->sfd, &rfds);
		FD_ZERO(&wfds);
		if (cx->want_write)
			FD_SET(cx->sfd, &wfds);
		res = select(cx->sfd + 1, &rfds, &wfds, NULL, NULL);
		if (res < 0) {
			if (errno == EINTR)
				continue;
			debug(1, "select error %d \"%s\"\n",
				errno, strerror(errno));
			return -1;
		}
		if (FD_ISSET(cx->sfd, &rfds))
			cx->read_ready(cx);
		if (FD_ISSET(cx->sfd, &wfds))
			cx->write_ready(cx);
	}

	return cx->close_connection;
}
