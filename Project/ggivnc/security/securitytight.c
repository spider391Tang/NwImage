/*
******************************************************************************

   VNC viewer tight security type.

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


#include "vnc.h"
#include "handshake.h"
#include "vnc-compat.h"
#include "vnc-endian.h"
#include "vnc-debug.h"


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

	sprintf(dump, "%ld - %s %s", (long)(int32_t)cap->code, vendor, name);
	return dump;
}

int
vnc_security_tight_init(struct connection *cx)
{
	uint16_t server_messages;
	uint16_t client_messages;
	uint16_t encodings;
	int caps_len;
	uint16_t i;
	struct capability capability;
	int file_v1_transfer_caps = 0;
	uint32_t file_transfer_caps = 0;

	enum {
		file_v1_list_request         = 1 <<  0,
		file_v1_list_data            = 1 <<  1,
		file_v1_download_request     = 1 <<  2,
		file_v1_download_data        = 1 <<  3,
		file_v1_download_failed      = 1 <<  4, /* optional */
		file_v1_download_cancel      = 1 <<  5, /* optional */
		file_v1_upload_request       = 1 <<  6,
		file_v1_upload_data          = 1 <<  7,
		file_v1_upload_failed        = 1 <<  8,
		file_v1_upload_cancel        = 1 <<  9, /* optional */
		file_v1_last_request_failed  = 1 << 10, /* optional */
		file_v1_basics =
			file_v1_list_request |
			file_v1_list_data |
			file_v1_download_request |
			file_v1_download_data |
			file_v1_upload_request |
			file_v1_upload_data |
			file_v1_upload_failed
	};

#define file_base (0xfc000100)
#define file_compression_request    (file_base +  0) /* optional */
#define file_compression_reply      (file_base +  1) /* optional */
#define file_list_request           (file_base +  2)
#define file_list_reply             (file_base +  3)
#define file_md5_request            (file_base +  4) /* optional */
#define file_md5_reply              (file_base +  5) /* optional */
#define file_upload_start_request   (file_base +  6)
#define file_upload_start_reply     (file_base +  7)
#define file_upload_data_request    (file_base +  8)
#define file_upload_data_reply      (file_base +  9)
#define file_upload_end_request     (file_base + 10)
#define file_upload_end_reply       (file_base + 11)
#define file_download_start_request (file_base + 12)
#define file_download_start_reply   (file_base + 13)
#define file_download_data_request  (file_base + 14)
#define file_download_data_reply    (file_base + 15)
#define file_download_data_end      (file_base + 16)
#define file_mkdir_request          (file_base + 17) /* optional */
#define file_mkdir_reply            (file_base + 18) /* optional */
#define file_rm_request             (file_base + 19) /* optional */
#define file_rm_reply               (file_base + 20) /* optional */
#define file_mv_request             (file_base + 21) /* optional */
#define file_mv_reply               (file_base + 22) /* optional */
#define file_directory_size_request (file_base + 23) /* optional */
#define file_directory_size_reply   (file_base + 24) /* optional */
#define file_last_request_failed    (file_base + 25) /* optional */
#define file_basics ( \
		(1 << (file_list_request - file_base)) | \
		(1 << (file_list_reply - file_base)) | \
		(1 << (file_upload_start_request - file_base)) | \
		(1 << (file_upload_start_reply - file_base)) | \
		(1 << (file_upload_data_request - file_base)) | \
		(1 << (file_upload_data_reply - file_base)) | \
		(1 << (file_upload_end_request - file_base)) | \
		(1 << (file_upload_end_reply - file_base)) | \
		(1 << (file_download_start_request - file_base)) | \
		(1 << (file_download_start_reply - file_base)) | \
		(1 << (file_download_data_request - file_base)) | \
		(1 << (file_download_data_reply - file_base)) | \
		(1 << (file_download_data_end - file_base)))

	debug(2, "vnc_security_tight_init\n");

	if (cx->input.wpos < cx->input.rpos + 8)
		return 0;

	server_messages = get16_hilo(&cx->input.data[cx->input.rpos]);
	client_messages = get16_hilo(&cx->input.data[cx->input.rpos + 2]);
	encodings = get16_hilo(&cx->input.data[cx->input.rpos + 4]);

	caps_len = (server_messages + client_messages + encodings)
		* sizeof(capability);

	if (cx->input.wpos < cx->input.rpos + 8 + caps_len)
		return 0;

	cx->input.rpos += 8;

	for (i = 0; i < server_messages; ++i) {
		memcpy(&capability, &cx->input.data[cx->input.rpos],
			sizeof(capability));
		cx->input.rpos += sizeof(capability);

		capability.code =
			get32_hilo((uint8_t *)&capability.code);

		switch (capability.code) {
		case 130:
			file_v1_transfer_caps |= file_v1_list_data;
			break;
		case 131:
			file_v1_transfer_caps |= file_v1_download_data;
			break;
		case 132:
			file_v1_transfer_caps |= file_v1_upload_cancel;
			break;
		case 133:
			file_v1_transfer_caps |= file_v1_download_failed;
			break;
		case 135:
			file_v1_transfer_caps |= file_v1_last_request_failed;
			break;

		case file_compression_reply:
		case file_list_reply:
		case file_md5_reply:
		case file_upload_start_reply:
		case file_upload_data_reply:
		case file_upload_end_reply:
		case file_download_start_reply:
		case file_download_data_reply:
		case file_download_data_end:
		case file_mkdir_reply:
		case file_rm_reply:
		case file_mv_reply:
		case file_directory_size_reply:
		case file_last_request_failed:
			file_transfer_caps |=
				1 << (capability.code - file_base);
			break;
		}

		debug(1, "Server message: %s\n",
			capability_dump(&capability));
	}

	for (i = 0; i < client_messages; ++i) {
		memcpy(&capability, &cx->input.data[cx->input.rpos],
			sizeof(capability));
		cx->input.rpos += sizeof(capability);

		capability.code =
			get32_hilo((uint8_t *)&capability.code);

		switch (capability.code) {
		case 130:
			file_v1_transfer_caps |= file_v1_list_request;
			break;
		case 131:
			file_v1_transfer_caps |= file_v1_download_request;
			break;
		case 132:
			file_v1_transfer_caps |= file_v1_upload_request;
			break;
		case 133:
			file_v1_transfer_caps |= file_v1_upload_data;
			break;
		case 134:
			file_v1_transfer_caps |= file_v1_download_cancel;
			break;
		case 135:
			file_v1_transfer_caps |= file_v1_upload_failed;
			break;

		case file_compression_request:
		case file_list_request:
		case file_md5_request:
		case file_upload_start_request:
		case file_upload_data_request:
		case file_upload_end_request:
		case file_download_start_request:
		case file_download_data_request:
		case file_mkdir_request:
		case file_rm_request:
		case file_mv_request:
		case file_directory_size_request:
			file_transfer_caps |=
				1 << (capability.code - file_base);
			break;
		}

		debug(1, "Client message: %s\n",
			capability_dump(&capability));
	}

	for (i = 0; i < encodings; ++i) {
		memcpy(&capability, &cx->input.data[cx->input.rpos],
			sizeof(capability));
		cx->input.rpos += sizeof(capability);

		capability.code =
			get32_hilo((uint8_t *)&capability.code);

		debug(1, "Server encoding: %s\n",
			capability_dump(&capability));
	}

	remove_dead_data(&cx->input);

	if ((file_transfer_caps & file_basics) == file_basics) {
		debug(1, "enabling tight file transfer version 2\n");
		cx->file_transfer = 2;
	}
	else if ((file_v1_transfer_caps & file_v1_basics) == file_v1_basics) {
		debug(1, "enabling tight file transfer version 1\n");
		cx->file_transfer = 1;
	}

	cx->action = vnc_finish_handshake;
	return 1;
}

static int
vnc_security_tight_auth(struct connection *cx)
{
	uint32_t i, j;
	uint32_t auth_types;
	int auth_len;
	struct capability *auth_caps;
	int weight = -1;
	int current_weight;
	uint32_t auth_code = 0;
	uint8_t buf[4];

	debug(2, "vnc_security_tight_auth\n");

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	auth_types = get32_hilo(&cx->input.data[cx->input.rpos]);
	if (auth_types >=
		(uint32_t)(~0 - (cx->input.rpos + 4)) / sizeof(*auth_caps))
	{
		/* Not a protocol error per se, but servers reporting a
		 * quarter billion or so auth types are insane enough
		 * for me to ignore...
		 */
		return close_connection(cx, -1);
	}
	auth_len = 4 + auth_types * sizeof(*auth_caps);

	if (cx->input.wpos < cx->input.rpos + auth_len)
		return 0;

	auth_caps = malloc(auth_types * sizeof(*auth_caps));
	if (!auth_caps)
		return close_connection(cx, -1);

	cx->input.rpos += sizeof(auth_types);
	memcpy(auth_caps, &cx->input.data[cx->input.rpos],
		auth_types * sizeof(*auth_caps));
	cx->input.rpos += auth_types * sizeof(*auth_caps);
	for (i = 0; i < auth_types; ++i)
		auth_caps[i].code = get32_hilo((uint8_t *)&auth_caps[i].code);

	for (i = 0; i < auth_types; ++i) {

		debug(1, "Auth cap: %s\n", capability_dump(&auth_caps[i]));

		for (j = 0; cx->allow_security[j]; ++j) {
			if (cx->allow_security[j] == 16)
				continue;
			if (cx->allow_security[j] ==
				auth_caps[i].code)
			{
				break;
			}
		}
		if (!cx->allow_security[j])
			continue;
		current_weight = 256 - j;

		if (current_weight > weight) {
			weight = current_weight;
			auth_code = auth_caps[i].code;
		}
	}

	free(auth_caps);

	if (auth_types) {
		if (weight == -1 && cx->force_security) {
			/* No allowed auth code found, be a little dirty and
			 * request the first non-tight auth type. The server
			 * just might allow it...
			 */
			for (j = 0; cx->allow_security[j] == 16; ++j);
			auth_code = cx->allow_security[j];
		}

		if (!auth_code)
			return close_connection(cx, -1);

		cx->security = auth_code;

		insert32_hilo(buf, auth_code);

		if (safe_write(cx, buf, sizeof(buf)))
			return close_connection(cx, -1);
	}
	else
		cx->security = 1;

	remove_dead_data(&cx->input);

	return vnc_handle_security(cx);
}

int
vnc_security_tight(struct connection *cx)
{
	uint32_t i;
	uint32_t tunnel_types;
	int tunnel_len;
	struct capability capability;

	debug(2, "vnc_security_tight\n");

	if (cx->security_tight)
		/* prevent recursion */
		return close_connection(cx, -1);

	if (cx->input.wpos < cx->input.rpos + 4)
		return 0;

	tunnel_types = get32_hilo(&cx->input.data[cx->input.rpos]);
	if (tunnel_types >=
		(uint32_t)(~0 - (cx->input.rpos + 4)) / sizeof(capability))
	{
		/* Not a protocol error per se, but servers reporting a
		 * quarter billion or so tunneling types are insane enough
		 * for me to ignore...
		 */
		return close_connection(cx, -1);
	}
	tunnel_len = 4 + sizeof(capability) * tunnel_types;

	if (cx->input.wpos < cx->input.rpos + tunnel_len)
		return 0;

	cx->input.rpos += sizeof(tunnel_types);
	for (i = 0; i < tunnel_types; ++i) {
		memcpy(&capability, &cx->input.data[cx->input.rpos],
			sizeof(capability));
		cx->input.rpos += sizeof(capability);
		capability.code = get32_hilo((uint8_t *)&capability.code);

		debug(1, "Tunnel cap: %s\n", capability_dump(&capability));
	}

	cx->security_tight = 1;

	if (tunnel_types) {
		/* Reply blindly with "no tunneling", even if that
		 * may not be available from the server
		 */
		uint32_t notunnel = 0;
		if (safe_write(cx, &notunnel, sizeof(notunnel)))
			return close_connection(cx, -1);
	}

	remove_dead_data(&cx->input);

	cx->action = vnc_security_tight_auth;
	return 1;
}
