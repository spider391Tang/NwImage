/*
******************************************************************************

   VNC viewer common stuff.

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

struct bw_table {
	int bandwidth;
	int32_t *encoding;
	uint16_t count;
};

struct bandwidth {
	int counting;
	int count;
	struct timeval start;
	int estimate;
	int idx;
};

struct buffer {
	uint8_t *data;
	int size;
	int wpos;
	int rpos;
};

struct connection;

typedef int (action_t)(struct connection *cx);

struct encoding_def {
	action_t *action;
	void (*end)(struct connection *cx);
	void *priv;
};

enum {
	corre_encoding,
	hextile_encoding,
	gii_encoding,
	rre_encoding,
	tight_encoding,
	zlib_encoding,
	zlibhex_encoding,
	trle_encoding,
	zrle_encoding,
	tight_file,
	xvp_encoding,
	encoding_defs
};

struct security {
	uint32_t number;
	int weight;
	const char *name;
};

extern const struct security security_types[];

struct connection {
	ggi_visual_t stem;
	const char *gii_input;
	void (*flush_hook)(void *data);
	void (*post_flush_hook)(void *data);
	void *flush_hook_data;
	char *server_port;
	char *server;
	int auto_reconnect;
	char *username;
	char *passwd;
	int port;
	int file_passwd;
	uint8_t shared;
	int sfd;
	int net_family;
	int listen;
	char *bind;
	int close_connection;
	int protocol;
	unsigned int max_protocol;
	uint32_t security;
	uint32_t *allow_security;
	int force_security;
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
	int (*read_ready)(struct connection *cx);
	int (*write_ready)(struct connection *cx);
	int (*safe_write)(struct connection *cx, const void *buf, int count);
	int (*write_drained)(struct connection *cx);
	int max_read;
	int want_read;
	int want_write;
	struct buffer input;
	struct buffer output;
	struct buffer work;
	action_t *action;
	struct encoding_def encoding_def[encoding_defs];
	void *vencrypt;
	uint16_t rects;
	uint16_t x, y, w, h;
	int desktop_size;
	ggi_visual_t wire_stem;
	ggi_mode wire_mode;
	int wire_stem_flags;
	int (*stem_change)(struct connection *cx);
	int no_input;
	int auto_encoding;
	uint16_t encoding_count;
	int32_t *encoding;
	uint16_t allowed_encodings;
	int32_t *allow_encoding;
	int scrollx;
	int scrolly;
	void *scroll;
	double sx;
	double sy;
	int compression;
	double compression_level;
	int quality;
	double quality_level;
	int F8_allow_release;
	void *fdselect;
	int file_transfer;
	int expert;

	struct bandwidth bw;
	struct bw_history history;
	struct bw_table *bw_table;

	void *visualanchor;
};

#define UPLOAD_FILE_FRAGMENT_CMD (GII_CMDFLAG_PRIVATE | 42)

#ifndef HAVE_WIDGETS
int show_about(struct connection *cx);
int show_menu(struct connection *cx);
int show_xvp_menu(struct connection *cx);
int show_file_transfer(struct connection *cx);
int file_upload_fragment(struct connection *cx);
int show_reconnect(struct connection *cx, int popup);
#endif

void vnc_want_read(struct connection *cx);
void vnc_want_write(struct connection *cx);
void vnc_stop_read(struct connection *cx);
void vnc_stop_write(struct connection *cx);
int safe_write(struct connection *cx, const void *buf, int count);
void select_mode(struct connection *cx);
int parse_port(struct connection *cx);
int open_visual(struct connection *cx);
void close_visual(struct connection *cx);
int set_title(struct connection *cx);
int get_password(struct connection *cx, const char *uprompt, int ulen,
	const char *pprompt, int plen);
int get_connection(struct connection *cx);
int canonicalize_pixfmt(char *pixfmt, int count);
const char *lookup_encoding(int32_t number);
int get_default_encodings(const int32_t **encodings);
int color_bits(uint16_t max);
int generate_pixfmt(char *pixfmt, int count, const ggi_pixelformat *ggi_pf);
int wire_mode_switch(struct connection *cx,
	const char *pixfmt, int endian, ggi_coord wire_size);

void remove_dead_data(struct buffer *buf);
int buffer_reserve(struct buffer *buf, int size);
int close_connection(struct connection *cx, int code);
int vnc_update_request(struct connection *cx, int incremental);
int vnc_set_encodings(struct connection *cx);
int vnc_update_rect(struct connection *cx);
int vnc_wait(struct connection *cx);

int vnc_raw(struct connection *cx);
int vnc_copyrect(struct connection *cx);
int vnc_rre(struct connection *cx);
int vnc_corre(struct connection *cx);
int vnc_hextile(struct connection *cx);
int vnc_hextile_size(struct connection *cx, int bpp);
int vnc_tight(struct connection *cx);
int vnc_trle(struct connection *cx);
int vnc_zlib(struct connection *cx);
int vnc_zlibhex(struct connection *cx);
int vnc_zrle(struct connection *cx);
int vnc_lastrect(struct connection *cx);
int vnc_wmvi(struct connection *cx);
int vnc_desktop_size(struct connection *cx);
int vnc_desktop_name(struct connection *cx);
#ifdef HAVE_GGNEWSTEM
int gii_inject(struct connection *cx, gii_event *ev);
int gii_create_device(struct connection *cx,
	uint32_t origin, struct gii_cmddata_devinfo *dev);
int gii_delete_device(struct connection *cx, gii_event *ev);
int gii_receive(struct connection *cx);
#endif /* HAVE_GGNEWSTEM */
int xvp_receive(struct connection *cx);
int xvp_shutdown(struct connection *cx);
int xvp_reboot(struct connection *cx);
int xvp_reset(struct connection *cx);
int vnc_unexpected(struct connection *cx);

int bandwidth_init(struct connection *cx);
void bandwidth_fini(struct connection *cx);
void bandwidth_start(struct connection *cx, ssize_t len);
void bandwidth_update(struct connection *cx, ssize_t len);
int bandwidth_end(struct connection *cx);

int parse_options(struct connection *cx, int argc, char * const argv[]);
int vencrypt_set_method(struct connection *cx, const char *method);
int vencrypt_set_cert(struct connection *cx, const char *cert);
int vencrypt_set_ciphers(struct connection *cx, const char *ciphers);
int vencrypt_set_priv_key(struct connection *cx, const char *priv_key);
int vencrypt_set_verify_file(struct connection *cx, const char *verify_file);
int vencrypt_set_verify_dir(struct connection *cx, const char *verify_dir);

void set_icon(void);
int socket_init(void);
void socket_cleanup(void);
void console_init(void);
char *get_appdata_path(void);

#endif /* VNC_H */
