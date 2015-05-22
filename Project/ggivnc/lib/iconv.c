/*
******************************************************************************

   iconv replacement for ggivnc.

   The MIT License

   Copyright (C) 2009-2010 Peter Rosin  [peda@lysator.liu.se]

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#ifndef ICONV_TEST
#include "vnc.h"
#include "vnc-compat.h"
#else
#include <sys/types.h>
#include <stdint.h>
typedef struct iconv_t *iconv_t;
#endif /* ICONV_TEST */

#ifndef ICONV_CHUNK
#define ICONV_CHUNK 512
#endif

typedef size_t (iconv_func_t)(iconv_t cd,
	const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft);

#ifdef _WIN32

#include <windows.h>

#ifndef WC_ERR_INVALID_CHARS
#define WC_ERR_INVALID_CHARS 0
#endif

struct iconv_t {
	iconv_func_t *convert;
	UINT in_cp;
	UINT out_cp;
	LPWSTR tmp;
};

static size_t
iconv_wctomb(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	size_t chunk;
	size_t minchunk;
	size_t maxchunk;
	int len;

	if (!inbuf || !*inbuf)
		return 0;

	while (*inbytesleft > 1) {
		maxchunk = ICONV_CHUNK;
		if (maxchunk > *inbytesleft / sizeof(wchar_t))
			maxchunk = *inbytesleft / sizeof(wchar_t);
		chunk = minchunk = maxchunk;

retry_chunk:
		len = WideCharToMultiByte(cd->out_cp, WC_ERR_INVALID_CHARS,
			(LPCWSTR)*inbuf, chunk,
			*outbuf, *outbytesleft, NULL, NULL);
		if (!len) {
			switch (GetLastError()) {
			case ERROR_INSUFFICIENT_BUFFER:
				if (minchunk == 1) {
					errno = E2BIG;
					break;
				}
half_chunk:
				maxchunk = minchunk / 2 + 6;
				if (maxchunk >= minchunk)
					maxchunk = minchunk - 1;
				minchunk /= 2;
				chunk = minchunk;
				goto retry_chunk;

			case ERROR_NO_UNICODE_TRANSLATION:
				if (chunk == maxchunk) {
					if (minchunk == 1) {
						errno = EILSEQ;
						break;
					}
					goto half_chunk;
				}
				++chunk;
				goto retry_chunk;

			default:
				errno = EINVAL;
				break;
			}
			return (size_t)-1;
		}

		*inbuf += chunk * sizeof(wchar_t);
		*inbytesleft -= chunk * sizeof(wchar_t);
		*outbuf += len;
		*outbytesleft -= len;
	}

	if (*inbytesleft) {
		errno = EINVAL;
		return (size_t)-1;
	}
	return 0;
}

static size_t
iconv_mbtowc(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	size_t chunk;
	size_t minchunk;
	size_t maxchunk;
	int len;

	if (!inbuf || !*inbuf)
		return 0;

	while (*inbytesleft) {
		maxchunk = ICONV_CHUNK;
		if (maxchunk > *inbytesleft)
			maxchunk = *inbytesleft;
		chunk = minchunk = maxchunk;

retry_chunk:
		len = MultiByteToWideChar(cd->in_cp, MB_ERR_INVALID_CHARS,
			*inbuf, chunk, (LPWSTR)*outbuf, *outbytesleft / 2);
		if (!len) {
			switch (GetLastError()) {
			case ERROR_INSUFFICIENT_BUFFER:
				if (minchunk == 1) {
					errno = E2BIG;
					break;
				}
half_chunk:
				maxchunk = minchunk / 2 + 6;
				if (maxchunk >= minchunk)
					maxchunk = minchunk - 1;
				minchunk /= 2;
				chunk = minchunk;
				goto retry_chunk;

			case ERROR_NO_UNICODE_TRANSLATION:
				if (chunk == maxchunk) {
					if (minchunk == 1) {
						errno = EILSEQ;
						break;
					}
					goto half_chunk;
				}
				++chunk;
				goto retry_chunk;

			default:
				errno = EINVAL;
				break;
			}
			return (size_t)-1;
		}

		*inbuf += chunk;
		*inbytesleft -= chunk;
		*outbuf += len * sizeof(wchar_t);
		*outbytesleft -= len * sizeof(wchar_t);
	}

	if (*inbytesleft) {
		errno = EINVAL;
		return (size_t)-1;
	}
	return 0;
}

static size_t
iconv_mbtomb(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	size_t chunk;
	size_t minchunk;
	size_t maxchunk;
	int len;

	if (!inbuf || !*inbuf)
		return 0;

	while (*inbytesleft) {
		maxchunk = ICONV_CHUNK;
		if (maxchunk > *inbytesleft)
			maxchunk = *inbytesleft;
		chunk = minchunk = maxchunk;

retry_chunk:
		/*printf("chunk %d\n", chunk);*/
		len = MultiByteToWideChar(cd->in_cp, MB_ERR_INVALID_CHARS,
			*inbuf, chunk, cd->tmp, ICONV_CHUNK);
		if (!len) {
handle_error:
			switch (GetLastError()) {
			case ERROR_INSUFFICIENT_BUFFER:
				if (minchunk == 1) {
					errno = E2BIG;
					break;
				}
half_chunk:
				maxchunk = minchunk / 2 + 6;
				if (maxchunk >= minchunk)
					maxchunk = minchunk - 1;
				minchunk /= 2;
				chunk = minchunk;
				goto retry_chunk;

			case ERROR_NO_UNICODE_TRANSLATION:
				if (chunk == maxchunk) {
					if (minchunk == 1) {
						errno = EILSEQ;
						break;
					}
					goto half_chunk;
				}
				++chunk;
				goto retry_chunk;

			default:
				errno = EINVAL;
				break;
			}
			return (size_t)-1;
		}

		len = WideCharToMultiByte(cd->out_cp, WC_ERR_INVALID_CHARS,
			cd->tmp, len, *outbuf, *outbytesleft, NULL, NULL);
		if (!len)
			goto handle_error;

		*inbuf += chunk;
		*inbytesleft -= chunk;
		*outbuf += len;
		*outbytesleft -= len;
	}

	return 0;
}

static UINT
iconv_code(const char *code)
{
	if (!strcmp(code, "UTF-16"))
		return 1200;

	if (!strcmp(code, "UTF-8"))
		return CP_UTF8;
	if (!strcmp(code, "US-ASCII") || !strcmp(code, "ASCII"))
		return 20127;
	if (!strcmp(code, "ISO-8859-1"))
		return 28591;

	return 0;
}

iconv_t
iconv_open(const char *tocode, const char *fromcode)
{
	iconv_t cd = malloc(sizeof(*cd));
	if (!cd) {
		errno = ENOMEM;
		return (iconv_t)-1;
	}

	cd->convert = NULL;
	cd->in_cp = iconv_code(fromcode);
	cd->out_cp = iconv_code(tocode);

	if (!cd->in_cp || !cd->out_cp)
		;
	else if (cd->in_cp == cd->out_cp)
		;
	else if (cd->in_cp == 1200)
		cd->convert = iconv_wctomb;
	else if (cd->out_cp == 1200)
		cd->convert = iconv_mbtowc;
	else {
		cd->tmp = malloc(ICONV_CHUNK * sizeof(wchar_t));
		if (!cd->tmp) {
			free(cd);
			errno = ENOMEM;
			return (iconv_t)-1;
		}
		cd->convert = iconv_mbtomb;
	}


	if (cd->convert)
		return cd;

	free(cd);
	errno = EINVAL;
	return (iconv_t)-1;
}

#else

struct iconv_t {
	iconv_func_t *convert;
	int in_cp;
	int out_cp;
};

/* Handles ASCII input and UTF-8 or ISO 8859-1 output */
static size_t
iconv_from_ascii(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	size_t count = 0;

	if (!inbuf || !*inbuf)
		return count;

	while (*inbytesleft && *outbytesleft) {
		uint8_t ch = **inbuf;
		if (ch & 0x80) {
			errno = EILSEQ;
			return count;
		}
		**outbuf = ch;
		++*outbuf;
		--*outbytesleft;
		++*inbuf;
		--*inbytesleft;
		++count;
	}
	if (*inbytesleft)
		errno = E2BIG;
	return count;
}

/* Handles UTF-8 input and ASCII or ISO 8859-1 output */
static size_t
iconv_from_utf8(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	size_t count = 0;
	uint32_t unicode;

	if (!inbuf || !*inbuf)
		return count;

	while (*inbytesleft && *outbytesleft) {
		uint8_t ch1 = **inbuf;
		if (ch1 < 0x80)
			unicode = ch1;
		else if (ch1 < 0xc2 || ch1 > 0xf4) {
			errno = EILSEQ;
			return count;
		}
		else if (ch1 < 0xe0) {
			uint8_t ch2;
			if (*inbytesleft < 2) {
				errno = EINVAL;
				return count;
			}
			ch2 = *(*inbuf + 1);
			if ((ch2 & 0xc0) != 0x80) {
				errno = EILSEQ;
				return count;
			}
			unicode  = (uint32_t)(ch1 & 0x1f) << 6;
			unicode |= (uint32_t)(ch2 & 0x3f);
			if (unicode < 0x80) {
				/* overly long form */
				errno = EILSEQ;
				return count;
			}
			++*inbuf;
			--*inbytesleft;
		}
		else if (ch1 < 0xf0) {
			uint8_t ch2, ch3;
			if (*inbytesleft < 3) {
				errno = EINVAL;
				return count;
			}
			ch2 = *(*inbuf + 1);
			ch3 = *(*inbuf + 2);
			if ((ch2 & 0xc0) != 0x80) {
				errno = EILSEQ;
				return count;
			}
			if ((ch3 & 0xc0) != 0x80) {
				errno = EILSEQ;
				return count;
			}
			unicode  = (uint32_t)(ch1 & 0x0f) << 12;
			unicode |= (uint32_t)(ch2 & 0x3f) << 6;
			unicode |= (uint32_t)(ch3 & 0x3f);
			if (unicode < 0x800) {
				/* overly long form */
				errno = EILSEQ;
				return count;
			}
			if (unicode >= 0xd800 && unicode <= 0xdfff) {
				/* surrogate pair */
				errno = EILSEQ;
				return count;
			}
			*inbuf += 2;
			*inbytesleft -= 2;
		}
		else {
			uint8_t ch2, ch3, ch4;
			if (*inbytesleft < 4) {
				errno = EINVAL;
				return count;
			}
			ch2 = *(*inbuf + 1);
			ch3 = *(*inbuf + 2);
			ch4 = *(*inbuf + 3);
			if ((ch2 & 0xc0) != 0x80) {
				errno = EILSEQ;
				return count;
			}
			if ((ch3 & 0xc0) != 0x80) {
				errno = EILSEQ;
				return count;
			}
			if ((ch4 & 0xc0) != 0x80) {
				errno = EILSEQ;
				return count;
			}
			unicode  = (uint32_t)(ch1 & 0x07) << 18;
			unicode |= (uint32_t)(ch2 & 0x3f) << 12;
			unicode |= (uint32_t)(ch3 & 0x3f) << 6;
			unicode |= (uint32_t)(ch4 & 0x3f);
			if (unicode < 0x10000 || unicode > 0x10ffff) {
				/* overly long form */
				errno = EILSEQ;
				return count;
			}
			if (unicode > 0x10ffff) {
				/* not unicode */
				errno = EILSEQ;
				return count;
			}
			*inbuf += 3;
			*inbytesleft -= 3;
		}
		if (cd->out_cp == 7 && unicode > 0x7f)
			unicode = '?';
		else if (cd->out_cp == 8 && unicode > 0xff)
			unicode = '?';
		**outbuf = unicode;
		++*outbuf;
		--*outbytesleft;
		++*inbuf;
		--*inbytesleft;
		++count;
	}
	if (*inbytesleft)
		errno = E2BIG;
	return count;
}

/* Handles ISO 8859-1 input and ASCII output */
static size_t
iconv_to_ascii(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	size_t count = 0;

	if (!inbuf || !*inbuf)
		return count;

	while (*inbytesleft && *outbytesleft) {
		uint8_t ch = **inbuf;
		if (ch & 0x80)
			ch = '?';
		**outbuf = ch;
		++*outbuf;
		--*outbytesleft;
		++*inbuf;
		--*inbytesleft;
		++count;
	}
	if (*inbytesleft)
		errno = E2BIG;
	return count;
}

/* Handles ISO 8859-1 input and UTF-8 output */
static size_t
iconv_to_utf8(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	size_t count = 0;

	if (!inbuf || !*inbuf)
		return count;

	while (*inbytesleft && *outbytesleft) {
		uint8_t ch = **inbuf;
		if (ch & 0x80) {
			if (*outbytesleft < 2)
				break;
			**outbuf = 0xc0 | (ch >> 6);
			++*outbuf;
			--*outbytesleft;
			ch &= ~0x40;
		}
		**outbuf = ch;
		++*outbuf;
		--*outbytesleft;
		++*inbuf;
		--*inbytesleft;
		++count;
	}
	if (*inbytesleft)
		errno = E2BIG;
	return count;
}

static int
iconv_code(const char *code)
{
	if (!strcmp(code, "UTF-8"))
		return 8;
	if (!strcmp(code, "US-ASCII") || !strcmp(code, "ASCII"))
		return 7;
	if (!strcmp(code, "ISO-8859-1"))
		return 1;

	return 0;
}

iconv_t
iconv_open(const char *tocode, const char *fromcode)
{
	iconv_t cd = malloc(sizeof(*cd));
	if (!cd) {
		errno = ENOMEM;
		return (iconv_t)-1;
	}

	cd->convert = NULL;
	cd->in_cp = iconv_code(fromcode);
	cd->out_cp = iconv_code(tocode);

	if (!cd->in_cp || !cd->out_cp)
		;
	else if (cd->in_cp == cd->out_cp)
		;
	else if (cd->in_cp == 7)
		cd->convert = iconv_from_ascii;
	else if (cd->in_cp == 8)
		cd->convert = iconv_from_utf8;
	else if (cd->out_cp == 7)
		cd->convert = iconv_to_ascii;
	else if (cd->out_cp == 8)
		cd->convert = iconv_to_utf8;

	if (cd->convert)
		return cd;

	free(cd);
	errno = EINVAL;
	return (iconv_t)-1;
}

#endif /* _WIN32 */

size_t
iconv(iconv_t cd, const char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	return cd->convert(cd, inbuf, inbytesleft, outbuf, outbytesleft);
}

int
iconv_close(iconv_t cd)
{
#ifdef _WIN32
	if (cd->tmp)
		free(cd->tmp);
#endif
	free(cd);
	return 0;
}

#ifdef ICONV_TEST

#include <stdio.h>

int
main(int argc, char *argv[])
{
	const char *swedish_8859_1 =
		"Jag var d\xe4r och k\xf6pte en k\xe5rv.";
	const char *swedish_utf_8 =
		"Jag var d\xc3\xa4r och k\xc3\xb6pte en k\xc3\xa5rv.";
#ifdef _WIN32
	const char *swedish_ascii = "Jag var dar och kopte en karv.";
#else
	const char *swedish_ascii = "Jag var d?r och k?pte en k?rv.";
#endif
	const char *english_ascii = "I was there bying a sausage.";
	const char *surrogate_utf8 =
		"Surrogate pair:\xed\xa1\x8c\xed\xbe\xb4";
	const char *surrogate_ascii = "Surrogate pair:";
#ifdef _WIN32
	const wchar_t surrogate_utf16[] = {
		'S','u','r','r','o','g','a','t','e',' ','p','a','i','r',':',
		0xd84c,0xdfb4,0
	};
	const char *nosurrogate_utf8 = "Surrogate pair:\xf0\xa3\x8e\xb4";
#endif
	char tmp[4096];
	iconv_t cd;
	const char *in;
	char *out;
	size_t inlen;
	size_t outlen;
	int res;
	int test = 0;
	int fail = 0;
	int ok;
	int verbose = 0;

	if (argc > 1) {
		if (!strcmp(argv[1], "--verbose"))
			verbose = 1;
		else
			return 1;
	}

	ok = 1;
	printf("%d. From ISO 8859-1 to UTF-8: ", ++test);
	cd = iconv_open("UTF-8", "ISO-8859-1");
	if (!cd)
		return 1;
	in = swedish_8859_1;
	inlen = strlen(in) + 1;
	out = tmp;
	outlen = sizeof(tmp);
	res = iconv(cd, &in, &inlen, &out, &outlen);
	if (inlen) {
		tmp[sizeof(tmp) - outlen] = '\0';
		ok = 0;
	}
	if (memcmp(tmp, swedish_utf_8, sizeof(swedish_utf_8)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose)
		printf("\t%s\n", tmp);
	fail += !ok;

	ok = 1;
	printf("%d. From ISO 8859-1 to UTF-8 in steps: ", ++test);
	in = swedish_8859_1;
	inlen = strlen(in) + 1;
	out = tmp;
	outlen = 10;
	res = iconv(cd, &in, &inlen, &out, &outlen);
	outlen = 100;
	res = iconv(cd, &in, &inlen, &out, &outlen);
	if (inlen) {
		tmp[sizeof(tmp) - outlen] = '\0';
		ok = 0;
	}
	if (memcmp(tmp, swedish_utf_8, sizeof(swedish_utf_8)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose)
		printf("\t%s\n", tmp);
	iconv_close(cd);
	fail += !ok;

	ok = 1;
	printf("%d. From UTF-8 to ISO 8859-1: ", ++test);
	cd = iconv_open("ISO-8859-1", "UTF-8");
	if (!cd)
		return 1;
	in = swedish_utf_8;
	inlen = strlen(in) + 1;
	out = tmp;
	outlen = sizeof(tmp);
	res = iconv(cd, &in, &inlen, &out, &outlen);
	if (inlen) {
		tmp[sizeof(tmp) - outlen] = '\0';
		ok = 0;
	}
	if (memcmp(tmp, swedish_8859_1, sizeof(swedish_8859_1)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose)
		printf("\t%s\n", tmp);
	iconv_close(cd);
	fail += !ok;

	ok = 1;
	printf("%d. From ISO 8859-1 to ASCII: ", ++test);
	cd = iconv_open("ASCII", "ISO-8859-1");
	if (!cd)
		return 1;
	in = swedish_8859_1;
	inlen = strlen(in) + 1;
	out = tmp;
	outlen = sizeof(tmp);
	res = iconv(cd, &in, &inlen, &out, &outlen);
	if (inlen) {
		tmp[sizeof(tmp) - outlen] = '\0';
		ok = 0;
	}
	if (memcmp(tmp, swedish_ascii, sizeof(swedish_ascii)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose)
		printf("\t%s\n", tmp);
	iconv_close(cd);
	fail += !ok;

	ok = 1;
	printf("%d. From UTF-8 to ASCII with (illegal) surrogate pair: ", ++test);
	cd = iconv_open("ASCII", "UTF-8");
	if (!cd)
		return 1;
	in = surrogate_utf8;
	inlen = strlen(in) + 1;
	out = tmp;
	outlen = sizeof(tmp);
	res = iconv(cd, &in, &inlen, &out, &outlen);
	if (inlen)
		tmp[sizeof(tmp) - outlen] = '\0';
	else
		ok = 0;
	if (memcmp(tmp, surrogate_ascii, sizeof(surrogate_ascii)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose)
		printf("\t%s\n", tmp);
	iconv_close(cd);
	fail += !ok;

	ok = 1;
	printf("%d. From ASCII to UTF-8: ", ++test);
	cd = iconv_open("UTF-8", "ASCII");
	if (!cd)
		return 1;
	in = english_ascii;
	inlen = strlen(in) + 1;
	out = tmp;
	outlen = sizeof(tmp);
	res = iconv(cd, &in, &inlen, &out, &outlen);
	if (inlen) {
		tmp[sizeof(tmp) - outlen] = '\0';
		ok = 0;
	}
	if (memcmp(tmp, english_ascii, sizeof(english_ascii)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose)
		printf("\t%s\n", tmp);
	iconv_close(cd);
	fail += !ok;

#ifdef _WIN32
	ok = 1;
	printf("%d. From UTF-16 to UTF-8 with surrogate pair: ", ++test);
	cd = iconv_open("UTF-8", "UTF-16");
	if (!cd)
		return 1;
	in = (char *)surrogate_utf16;
	inlen = sizeof(surrogate_utf16);
	out = tmp;
	outlen = sizeof(tmp);
	res = iconv(cd, &in, &inlen, &out, &outlen);
	if (inlen) {
		tmp[sizeof(tmp) - outlen] = '\0';
		ok = 0;
	}
	if (memcmp(tmp, nosurrogate_utf8, sizeof(nosurrogate_utf8)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose)
		printf("\t%s\n", tmp);
	iconv_close(cd);
	fail += !ok;

	ok = 1;
	printf("%d. From UTF-8 to UTF-16 with surrogate pair: ", ++test);
	cd = iconv_open("UTF-16", "UTF-8");
	if (!cd)
		return 1;
	in = nosurrogate_utf8;
	inlen = strlen(nosurrogate_utf8) + 1;
	out = tmp;
	outlen = sizeof(tmp);
	res = iconv(cd, &in, &inlen, &out, &outlen);
	/*
	printf("in %p (%p) inlen %d out %p (%p) outlen %d\n",
		in, nosurrogate_utf8, inlen,
		out, tmp, outlen);
	*/
	if (inlen) {
		tmp[sizeof(tmp) - outlen] = '\0';
		ok = 0;
	}
	if (memcmp(tmp, surrogate_utf16, sizeof(surrogate_utf16)))
		ok = 0;
	printf(ok ? "passed\n" : "failed\n");
	if (verbose) {
		printf("\t");
		for (res = 0; tmp[res] || tmp[res + 1]; res += 2) {
			uint16_t utf16;
			utf16 = (tmp[res + 1] << 8) | (uint8_t)tmp[res];
			if (utf16 < 0x80)
				printf("%c", utf16);
			else
				printf("\\x%04X", utf16);
		}
		printf("\n");
	}
	iconv_close(cd);
	fail += !ok;
#endif

	if (!fail)
		printf("All %d tests passed.\n", test);
	else
		printf("%d of %d tests failed.\n", fail, test);

	return fail;
}

#endif /* ICONV_TEST */
