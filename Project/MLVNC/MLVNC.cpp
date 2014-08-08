#include "../ggivnc/config.h"
#include "MLVNC.h"
#include <QDebug>
#include <getopt.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

// ggivnc declaration

#define set_icon()
#define socket_init() 0
#define socket_cleanup()
#define console_init()

#ifdef HAVE_GETADDRINFO
#ifdef PF_INET6
#define PF_OPTS "46"
#else
#define PF_OPTS "4"
#endif /* PF_INET6 */
#else
#define PF_OPTS ""
#endif /* HAVE_GETADDRINFO */

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
parse_bind(const char *iface)
{
	struct hostent *h;

	if (socket_init())
		return -1;

	h = gethostbyname(iface);
	if (!h) {
		qDebug() << "gethostbyname error\n";
		goto error;
	}
	if (h->h_addrtype != AF_INET) {
		qDebug() << "address family does not match\n";
		goto error;
	}

	if (g.bind)
		free(g.bind);
	g.bind = strdup(iface);
	if (!g.bind) {
		qDebug() << "Out of memory\n";
		goto error;
	}
	socket_cleanup();
	return 0;

error:
    //debug(0, "bind interface \"%s\" not found\n", iface);
	qDebug() << "bind interface " << iface << "not found";
	socket_cleanup();
	return -1;
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

	qDebug( "enerate_pixfmt\n");
                  
	qDebug( " size=%d\n", ggi_pf->size);
	qDebug( " depth=%d\n", ggi_pf->depth);
	qDebug( " mask c=%08x r=%08x g=%08x b=%08x\n",
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

	qDebug( "  max c=%d r=%d g=%d b=%d\n",
		c_max, r_max, g_max, b_max);
	qDebug( "  shift c=%d r=%d g=%d b=%d\n",
		c_shift, r_shift, g_shift, b_shift);

	rgb_bits = color_bits(r_max) + color_bits(g_max) + color_bits(b_max);

	qDebug( "  rgb_bits=%d\n", rgb_bits);

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

namespace MLLibrary {

int MLVNC::parse_encodings(char *encstr)
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


int MLVNC::parse_listen(const char *display)
{
	int port;
	unsigned short base = 5500;

	if (!display) {
		mGgivnc.listen = base;
		return 0;
	}
	if (*display == ':') {
		++display;
		base = 0;
	}
	if (sscanf(display, "%d", &port) != 1)
		goto error;
	mGgivnc.listen = base + port;
	return 0;

error:
	qDebug( "Bad 'listen' port \"%s\" specified\n", display);
	return -1;
}

MLVNC::~MLVNC()
{
}

MLVNC::MLVNC()
{
	int c;
	int status = 0;
// undef in config.h
#ifdef HAVE_INPUT_FDSELECT
	struct gii_fdselect_fd fd;
#endif
	long flags;

	mGgivnc.port = 5900;
	mGgivnc.shared = 1;
	mGgivnc.listen = 0;
	mGgivnc.bind = NULL;
	mGgivnc.wire_endian = -1;
	mGgivnc.auto_encoding = 1;
	mGgivnc.encoding_count =
		sizeof(default_encodings) / sizeof(default_encodings[0]) - 1;
	mGgivnc.encoding = default_encodings;
	mGgivnc.max_protocol = 8;
// undef in config.h
#ifdef HAVE_GGNEWSTEM
	GG_LIST_INIT(&mGgivnc.devices);
#endif

        // define empty in vnc.c
        //console_init();

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

        char* argv[] = {"ggivnc","10.128.60.135"};
        int argc = sizeof( argv ) / sizeof( argv[0] );
        c = getopt_long_only(argc, argv,
			"ab:de:E:f:hil::p:s:v" PF_OPTS,
			longopts, &longidx);
        switch (c) {
        case 'a':
            mGgivnc.shared = 0;
            break;
        case '$':
            mGgivnc.shared = 1;
            break;
        case 'b':
            if (parse_bind(optarg))
                status = 2;
            break;
        case 'd':
            ++mGgivnc.debug;
            break;
        case 'e':
            if (parse_encodings(optarg)) {
                fprintf(stderr, "bad encoding\n");
                status = 2;
            }
            mGgivnc.auto_encoding = 0;
            break;
        case 'E':
            if (!strcmp(optarg, "little"))
                mGgivnc.wire_endian = 0;
            else if (!strcmp(optarg, "big"))
                mGgivnc.wire_endian = 1;
            else {
                fprintf(stderr, "bad endian\n");
                status = 2;
            }
            break;
        case 'f':
            if (ggstrlcpy(mGgivnc.wire_pixfmt, optarg,
                    sizeof(mGgivnc.wire_pixfmt))
                >= sizeof(mGgivnc.wire_pixfmt))
            {
                fprintf(stderr, "pixfmt too long\n");
                status = 2;
            }
            else if (!strcmp(mGgivnc.wire_pixfmt, "local")) {
                if (mGgivnc.wire_endian < 0)
                    mGgivnc.wire_endian = -2;
            }
            else if (!strcmp(mGgivnc.wire_pixfmt, "server")) {
                if (mGgivnc.wire_endian < 0)
                    mGgivnc.wire_endian = -3;
            }
            else if (canonicalize_pixfmt(
                mGgivnc.wire_pixfmt, sizeof(mGgivnc.wire_pixfmt)))
            {
                fprintf(stderr, "bad pixfmt\n");
                status = 2;
            }
            break;
        case '%':
            mGgivnc.gii_input = optarg;
            break;
        // we don't need 'h'
        //case 'h':
        //    fprintf(stderr, help, remove_path(argv[0]));
        //    return 0;
        case 'i':
            mGgivnc.no_input = 1;
            break;
        case 'l':
            if (parse_listen(optarg))
                status = 2;
            break;
        case 'p':
            if (read_password(optarg))
                status = 2;
            break;
        //case '#':
        //    if (parse_protocol(optarg))
        //        status = 2;
        //    break;
        //case 's':
        //    if (parse_security(optarg)) {
        //        fprintf(stderr, "bad security type\n");
        //        status = 2;
        //    }
        //    break;
        //case 'v':
        //    fprintf(stderr, "%s version %s\n",
        //        remove_path(argv[0]), PACKAGE_VERSION);
        //    return 0;
        //case '4':
        //    mGgivnc.net_family = 4;
        //    break;
        //case '6':
        //    mGgivnc.net_family = 6;
        //    break;
        case ':':
        case '?':
            status = 2;
            break;
        }
    } while (c != -1);

	opterr = 1;
}

void MLVNC::pwrp()
// don't delete the following line as it's needed to preserve source code of this autogenerated element
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B84 begin
{
}
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B84 end
// don't delete the previous line as it's needed to preserve source code of this autogenerated element

void MLVNC::init()
// don't delete the following line as it's needed to preserve source code of this autogenerated element
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B86 begin
{
}
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B86 end
// don't delete the previous line as it's needed to preserve source code of this autogenerated element

void MLVNC::pwrdn()
// don't delete the following line as it's needed to preserve source code of this autogenerated element
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B88 begin
{
}
// section 127-0-1-1-584d88fa:144b573379b:-8000:0000000000000B88 end
// don't delete the previous line as it's needed to preserve source code of this autogenerated element

} /* End of namespace MLLibrary */
