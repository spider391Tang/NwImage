/*
******************************************************************************

   VNC viewer vnc-auth security type.

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

#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/des.h>
#endif

#include "vnc.h"
#include "handshake.h"
#include "vnc-compat.h"
#include "vnc-debug.h"


int
vnc_auth(struct connection *cx)
{
	DES_key_schedule ks;
	uint8_t passwd[8];
	uint8_t challenge[16];
	uint8_t response[16];
	unsigned int i;

	debug(2, "vnc_auth\n");

	if (cx->input.wpos < cx->input.rpos + 16)
		return 0;

	memcpy(challenge, &cx->input.data[cx->input.rpos], sizeof(challenge));

	debug(2, "challenge: %02x%02x %02x%02x %02x%02x %02x%02x "
		"%02x%02x %02x%02x %02x%02x %02x%02x\n",
		challenge[ 0], challenge[ 1], challenge[ 2], challenge[ 3],
		challenge[ 4], challenge[ 5], challenge[ 6], challenge[ 7],
		challenge[ 8], challenge[ 9], challenge[10], challenge[11],
		challenge[12], challenge[13], challenge[14], challenge[15]);

	if (!cx->passwd) {
		if (get_password(cx, NULL, 0, NULL, 8))
			return close_connection(cx, -1);
	}

	if (cx->passwd)
		strncpy((char *)passwd, cx->passwd, sizeof(passwd));
	else
		memset(passwd, 0, sizeof(passwd));

	/* Should apparently bitreverse the password bytes.
	 * I just love undocumented quirks to standard algorithms...
	 */
	for (i = 0; i < sizeof(passwd); ++i)
		passwd[i] = GGI_BITREV1(passwd[i]);

	DES_set_key_unchecked((DES_cblock *)passwd, &ks);
	DES_ecb_encrypt((DES_cblock *)&challenge[0],
		(DES_cblock *)&response[0], &ks, DES_ENCRYPT);
	DES_ecb_encrypt((DES_cblock *)&challenge[8],
		(DES_cblock *)&response[8], &ks, DES_ENCRYPT);

	if (safe_write(cx, response, sizeof(response)))
		return close_connection(cx, -1);

	cx->input.rpos += sizeof(challenge);
	remove_dead_data(&cx->input);

	cx->action = vnc_security_result;
	return 1;
}
