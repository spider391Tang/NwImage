/*
******************************************************************************

   VNC viewer option parsing.

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
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include "vnc.h"
#include "vnc-compat.h"
#include "vnc-debug.h"


struct encoding {
	int32_t number;
	const char *name;
};

static const struct encoding encoding_table[] = {
	{      0, "raw" },
	{      1, "copyrect" },
	{      2, "rre" },
	{      4, "corre" },
	{      5, "hextile" },
#ifdef HAVE_ZLIB
	{      6, "zlib" },
	{      7, "tight" },
	{      8, "zlibhex" },
	{     15, "trle" },
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
#ifdef HAVE_WIDGETS
	{   -309, "xvp" },
#endif
	{ 0x574d5669, "wmvi" },
	{      0,  NULL }
};

static const int32_t default_encodings[] = {
	1,	/* copyrect */
#ifdef HAVE_ZLIB
	16,	/* zrle */
#endif
	15,	/* trle */
	5,	/* hextile */
#ifdef HAVE_ZLIB
	7,	/* tight */
	8,	/* zlibhex */
	6,	/* zlib */
#endif /* HAVE_ZLIB */
	2,	/* rre */
	4,	/* corre */
	0,	/* raw */
#ifdef HAVE_ZLIB
#ifdef HAVE_JPEG
	-27,	/* tight quality 5 */
#endif
	-247,	/* tight compression 9 */
#endif /* HAVE_ZLIB */
	-223,	/* desksize */
	-224,	/* lastrect */
#ifdef HAVE_GGNEWSTEM
	-305,	/* gii */
#endif
#ifdef HAVE_WMH
	-307,	/* deskname */
#endif
#ifdef HAVE_WIDGETS
	-309,	/* xvp */
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
parse_encodings(struct connection *cx, char *encstr)
{
	char *enc;
	char *end;
	uint16_t i, j;

	cx->allowed_encodings = 0;
	for (enc = encstr - 1; enc; enc = strchr(enc + 1, ',')) {
		if (!++cx->allowed_encodings)
			return -1;
	}

	if (cx->allow_encoding)
		free(cx->allow_encoding);
	cx->allow_encoding = malloc(cx->allowed_encodings * sizeof(int32_t));
	if (!cx->allow_encoding)
		return -1;

	enc = encstr;
	for (i = 0; i < cx->allowed_encodings; ++i) {
		end = strchr(enc, ',');
		if (end)
			*end = '\0';
		for (j = 0; encoding_table[j].name; ++j) {
			if (strcmp(enc, encoding_table[j].name))
				continue;
			cx->allow_encoding[i] = encoding_table[j].number;
			break;
		}
		if (!encoding_table[j].name)
			return -1;
		enc = end + 1;
	}

	return 0;
}

static int
parse_security(struct connection *cx, char *secstr)
{
	char *sec;
	char *end;
	uint16_t i, j;
	uint32_t *security;
	int count;

	count = 0;
	for (sec = secstr - 1; sec; sec = strchr(sec + 1, ',')) {
		if (!++count)
			return -1;
	}

	security = malloc((count + 1) * sizeof(*security));
	if (!security)
		return -1;
	cx->allow_security = security;
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

static int
read_password(struct connection *cx, const char *file)
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

	cx->passwd = strdup(passwd);
	if (!cx->passwd) {
		debug(0, "error allocating passwd memory\n");
		return -1;
	}
	cx->file_passwd = 1;
	return 0;
}

int
parse_port(struct connection *cx)
{
	char *port;
	char *end;

	if (cx->server)
		free(cx->server);
	cx->server = strdup(cx->server_port);
	if (!cx->server)
		return -2;
	port = cx->server;

	if (cx->server[0] == '[') {
		end = strrchr(cx->server, ']');
		if (end) {
			memmove(cx->server, cx->server + 1,
				end - cx->server - 1);
			memmove(end - 1, end + 1, strlen(end));
			port = end - 1;
		}
	}

	port = strchr(port, ':');
	if (!port) {
		cx->port = 5900;
		return 0;
	}

	*port++ = '\0';
	if (*port == ':') {
		++port;
		cx->port = 0;
	}
	else
		cx->port = 5900;
	cx->port += strtoul(port, &end, 10);
	if ((port == end) || *end) {
		free(cx->server);
		cx->server = NULL;
		return -1;
	}
	return 0;
}

static int
parse_protocol(struct connection *cx, const char *protocol)
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
	cx->max_protocol = minor;
	return 0;

error:
	debug(0, "Bad RFB protocol version \"%s\" specified\n", protocol);
	return -1;
}

static int
parse_listen(struct connection *cx, const char *display)
{
	int port;
	unsigned short base = 5500;

	if (!display) {
		cx->listen = base;
		return 0;
	}
	if (*display == ':') {
		++display;
		base = 0;
	}
	if (sscanf(display, "%d", &port) != 1)
		goto error;
	cx->listen = base + port;
	return 0;

error:
	debug(0, "Bad 'listen' port \"%s\" specified\n", display);
	return -1;
}

static int
parse_bind(struct connection *cx, const char *iface)
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

	if (cx->bind)
		free(cx->bind);
	cx->bind = strdup(iface);
	if (!cx->bind) {
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

static int
parse_view(struct connection *cx, const char *coord)
{
	const char *orig = coord;
	char *end;
	int negative;
	long x, y;

	if (!*coord)
		goto error;

	while (*coord == ' ' || *coord == '\t')
		++coord;
	negative = *coord == '-';
	if (negative)
		++coord;
	x = strtol(coord, &end, 10);
	if (x < 0 || (*end != 'x' && *end != 'X'))
		goto error;
	if (negative)
		x = -x - 1;

	coord = end + 1;
	while (*coord == ' ' || *coord == '\t')
		++coord;
	negative = *coord == '-';
	if (negative)
		++coord;
	if (!*coord)
		goto error;
	y = strtol(coord, &end, 10);
	if (y < 0 || *end)
		goto error;
	if (negative)
		y = -y - 1;

	cx->slide.x = x;
	cx->slide.y = y;
	return 0;

error:
	debug(0, "bad view coordinate \"%s\" specified\n", orig);
	return -1;
}

static const char * const help_strings[] = {
"",
"If 'server' contains a colon or starts with a literal '[', quote it with",
"literal square brackets. E.g. [::ffff:192.168.1.1] (for IPv6 users).",
#ifndef HAVE_WIDGETS
"",
"If you do not supply a 'server', you have to supply the --listen option.",
#endif
"",
"Options:",
"  -a, --alone",
"      alone - don't request a shared session",
"  --shared",
"      the default, request a shared session",
"  --auto-reconnect",
"      automatically try to reconnect on connection failure",
"  -A, --auto-encoding",
"      adjust preferred encoding automatically, the default w/o -e",
"  -b, --bind <interface>",
"      the interface to listen to.",
#ifdef HAVE_OPENSSL
"  --cert <pem-file>",
"      file with certificate chain to use",
"  --ciphers <cipher-spec>",
"      allowed cipher suites. See \"openssl ciphers\" documentation.",
#endif
"  -d, --debug",
"      increase debug output level",
"  -e, --encodings <encodings>",
"      comma separated list of (in order of preference, but see -A):",
"...ENCODINGS...",
"  -E, --endian <endian>",
"      specify little or big endian",
"  -f, --pixfmt <pixfmt>",
"      pixfmt is either r<bits>g<bits>b<bits> (in any order, insert p<bits>",
"      as desired for padding), c<bits>, server or local",
"  --gii <input>",
"      extra gii input target to load",
"  -h, --help",
"      prints this help text and exits",
"  -i, --no-input",
"      no input - peek only, don't poke",
"  -l, --listen[=<display>|=:<port>]",
"      operate in reverse, i.e. listen for connections",
"  -p, --password <password>",
"      file containing password, beware, max 8 (7-bit) characters are used",
"      in the password",
#ifdef HAVE_OPENSSL
"  --priv-key <pem-file>",
"      private key file for certificate",
#endif
"  --rfb <version>",
"      the maximum rfb protocol version to use (3.3, 3.7 or 3.8)",
"  -s, --security-types <security-types>",
"      comma separated list of (in order of preference):",
"...SECURITY TYPES...",
"  --security-type-force",
"      If the server does not support any of the allowed security types the",
"      first allowed security type is requested anyway.",
#ifdef HAVE_OPENSSL
"  --ssl-method <method>",
"      one of TLSv1, SSLv2, SSLv3 and SSLv23",
"  --verify-file <pem-file>",
"      file with trusted certificates",
"  --verify-dir <pem-dir>",
"      directory with trusted certificates",
#endif
"  -v, --version",
"      prints the version of this software and exits",
"  --view <coordinate>",
"      selects initial upper left coordinate of the remote framebuffer",
"  -4, --ipv4",
"      only attempt IPv4 connections",
#ifdef PF_INET6
"  -6, --ipv6",
"      only attempt IPv6 connections",
#endif /* PF_INET6 */
NULL
};

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

static void
print_help(const char *program)
{
	int line;

	fprintf(stderr, "Usage: %s [options] [server[:<display>|::<port>]]\n",
		remove_path(program));

	for (line = 0; help_strings[line]; ++line) {
		if (!strcmp(help_strings[line], "...ENCODINGS...")) {
			int i;
			int len = 0;
			fprintf(stderr, "      ");
			for (i = 0; encoding_table[i].name; ++i) {
				int enc_len = strlen(encoding_table[i].name);
				if (len + enc_len > 70) {
					fprintf(stderr, ",\n      ");
					len = 0;
				}
				fprintf(stderr, "%s%s", len ? ", " : "",
					encoding_table[i].name);
				len += (len ? 2 : 0) + enc_len;
			}
			fprintf(stderr, "\n");
			continue;
		}
		if (!strcmp(help_strings[line], "...SECURITY TYPES...")) {
			int i;
			int len = 0;
			fprintf(stderr, "      ");
			for (i = 0; security_types[i].name; ++i) {
				int sec_len = strlen(security_types[i].name);
				if (!security_types[i].weight)
					continue;
				if (len + sec_len > 70) {
					fprintf(stderr, ",\n      ");
					len = 0;
				}
				fprintf(stderr, "%s%s", len ? ", " : "",
					security_types[i].name);
				len += (len ? 2 : 0) + sec_len;
			}
			fprintf(stderr, "\n");
			continue;
		}
		fprintf(stderr, "%s\n", help_strings[line]);
	}
}


#ifdef PF_INET6
#define PF_OPTS "46"
#else
#define PF_OPTS "4"
#endif /* PF_INET6 */

int
parse_options(struct connection *cx, int argc, char * const argv[])
{
	int c;
	int status = -1;

	opterr = 1;

	do {
		int longidx;
		struct option longopts[] = {
			{ "alone",         0, NULL, 'a' },
			{ "shared",        0, NULL, '$' },
			{ "auto-reconnect",0, NULL, '/' },
			{ "auto-encoding", 0, NULL, 'A' },
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
			{ "security-types",1, NULL, 's' },
			{ "security-type-force",
			                   0, NULL, '&' },
			{ "version",       0, NULL, 'v' },
			{ "view",          1, NULL, 'V' },
			{ "ipv4",          0, NULL, '4' },
#ifdef HAVE_OPENSSL
			{ "ssl-method",    1, NULL, 'M' },
			{ "cert",          1, NULL, 'c' },
			{ "ciphers",       1, NULL, 'C' },
			{ "priv-key",      1, NULL, 'K' },
			{ "verify-file",   1, NULL, 'F' },
			{ "verify-dir",    1, NULL, 'D' },
#endif
#ifdef PF_INET6
			{ "ipv6",          0, NULL, '6' },
#endif
			{ 0, 0, 0, 0 }
		};

		c = getopt_long_only(argc, argv,
			"ab:de:E:f:hil::p:s:v" PF_OPTS,
			longopts, &longidx);
		switch (c) {
		case 'a':
			cx->shared = 0;
			break;
		case '$':
			cx->shared = 1;
			break;
		case '/':
			cx->auto_reconnect = 1;
			break;
		case 'A':
			cx->auto_encoding = 2;
			break;
		case 'b':
			if (parse_bind(cx, optarg))
				status = 2;
			break;
		case 'd':
			set_debug_level(get_debug_level() + 1);
			break;
		case 'e':
			if (parse_encodings(cx, optarg)) {
				fprintf(stderr, "bad encoding\n");
				status = 2;
			}
			if (cx->auto_encoding != 2)
				cx->auto_encoding = 0;
			break;
		case 'E':
			if (!strcmp(optarg, "little"))
				cx->wire_endian = 0;
			else if (!strcmp(optarg, "big"))
				cx->wire_endian = 1;
			else {
				fprintf(stderr, "bad endian\n");
				status = 2;
			}
			break;
		case 'f':
			if (ggstrlcpy(cx->wire_pixfmt, optarg,
					sizeof(cx->wire_pixfmt))
				>= sizeof(cx->wire_pixfmt))
			{
				fprintf(stderr, "pixfmt too long\n");
				status = 2;
			}
			else if (!strcmp(cx->wire_pixfmt, "local")) {
				if (cx->wire_endian < 0)
					cx->wire_endian = -2;
			}
			else if (!strcmp(cx->wire_pixfmt, "server")) {
				if (cx->wire_endian < 0)
					cx->wire_endian = -3;
			}
			else if (canonicalize_pixfmt(
				cx->wire_pixfmt, sizeof(cx->wire_pixfmt)))
			{
				fprintf(stderr, "bad pixfmt\n");
				status = 2;
			}
			break;
		case '%':
			cx->gii_input = optarg;
			break;
		case 'h':
			print_help(argv[0]);
			return 0;
		case 'i':
			cx->no_input = 1;
			break;
		case 'l':
			if (parse_listen(cx, optarg))
				status = 2;
			break;
		case 'p':
			if (read_password(cx, optarg))
				status = 2;
			break;
		case '#':
			if (parse_protocol(cx, optarg))
				status = 2;
			break;
		case 's':
			if (parse_security(cx, optarg)) {
				fprintf(stderr, "bad security type\n");
				status = 2;
			}
			break;
		case '&':
			cx->force_security = 1;
			break;
		case 'v':
			fprintf(stderr, "%s version %s\n",
				remove_path(argv[0]), PACKAGE_VERSION);
			return 0;
		case 'V':
			if (parse_view(cx, optarg))
				status = 2;
			break;
		case '4':
			cx->net_family = 4;
			break;
		case '6':
			cx->net_family = 6;
			break;
#ifdef HAVE_OPENSSL
		case 'M':
			if (vencrypt_set_method(cx, optarg))
				status = 2;
			break;
		case 'c':
			if (vencrypt_set_cert(cx, optarg))
				status = 2;
			break;
		case 'C':
			if (vencrypt_set_ciphers(cx, optarg))
				status = 2;
			break;
		case 'K':
			if (vencrypt_set_priv_key(cx, optarg))
				status = 2;
			break;
		case 'F':
			if (vencrypt_set_verify_file(cx, optarg))
				status = 2;
			break;
		case 'D':
			if (vencrypt_set_verify_dir(cx, optarg))
				status = 2;
			break;
#endif
		case ':':
		case '?':
			status = 2;
			break;
		}
	} while (c != -1);

	cx->auto_encoding = !!cx->auto_encoding;

	if (!cx->allow_encoding) {
		const int32_t *def_enc;

		cx->allowed_encodings = get_default_encodings(&def_enc);
		cx->allow_encoding = malloc(cx->allowed_encodings *
			sizeof(*cx->allow_encoding));
		if (!cx->allow_encoding) {
			fprintf(stderr, "out of memory\n");
			return 5;
		}
		memcpy(cx->allow_encoding, def_enc,
			cx->allowed_encodings * sizeof(*cx->allow_encoding));
	}

	if (!cx->allow_security) {
		int32_t curr = -1;
		int count = 1;
		int i;

		for (i = 0; security_types[i].number; ++i) {
			if (security_types[i].weight <= 0)
				continue;
			++count;
			if (curr < security_types[i].weight)
				curr = security_types[i].weight;
		}
		cx->allow_security = malloc(
			count * sizeof(*cx->allow_security));
		if (!cx->allow_security) {
			fprintf(stderr, "out of memory\n");
			return 5;
		}
		for (count = 0; curr > 0; --curr) {
			for (i = 0; security_types[i].number; ++i) {
				if (security_types[i].weight != curr)
					continue;
				cx->allow_security[count++] =
					security_types[i].number;
			}
		}
		cx->allow_security[count] = 0;
	}

	if (socket_init()) {
		fprintf(stderr, "Winsock init failure\n");
		return 3;
	}

	if (cx->listen) {
		if (optind < argc) {
			fprintf(stderr,
				"Bad, both listening and connecting...\n");
			status = 2;
		}
	}
	else if (optind + 1 == argc) {
		cx->server_port = argv[optind];
		switch (parse_port(cx)) {
		case -2:
			return 5;
		case -1:
			fprintf(stderr,
				"Bad display or port specified\n");
			status = 2;
		}
	}
	else if (status == -1) {
		status = get_connection(cx);
		switch (status) {
		case 0:
			status = -1;
			break;
		case -1:
			return 0;
		case -2:
			return 4;
		}
	}

	if (status >= 0) {
		print_help(argv[0]);
		return status;
	}

	if (!cx->wire_pixfmt[0])
		strcpy(cx->wire_pixfmt, "local");

	return -1;
}
