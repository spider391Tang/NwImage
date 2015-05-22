/*
******************************************************************************

   VNC viewer VeNCrypt security type.

   The MIT License

   Copyright (C) 2008-2010 Peter Rosin  [peda@lysator.liu.se]

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
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#ifdef HAVE_WIDGETS
#include <ggi/ggi_widget.h>
#endif
#ifdef HAVE_WMH
#include <ggi/wmh.h>
#endif
#ifdef HAVE_INPUT_FDSELECT
#include <ggi/input/fdselect.h>
#endif
#include <ggi/gg-queue.h>

#include "vnc.h"
#include "handshake.h"
#include "vnc-compat.h"
#include "vnc-endian.h"
#include "vnc-debug.h"

#ifdef HAVE_WIDGETS
#include "dialog.h"
#endif

struct options {
	int method;
	char *ciphers;
	char *cert;
	char *priv_key;
	char *verify_file;
	char *verify_dir;
};

struct vencrypt {
	uint16_t version;
	SSL_CTX *ssl_ctx;
	BIO *ssl_bio;
	int write_wants_read;
	int read_wants_write;
	action_t *action;

	uint32_t subtype;
	struct options *opt;
};

struct cert_prop {
	GG_SIMPLEQ_ENTRY(cert_prop) next_prop;
	const char *name;
	char *val;
};

struct cert_level {
	GG_SIMPLEQ_ENTRY(cert_level) next_cert;
	GG_SIMPLEQ_HEAD(props, cert_prop) props;
	int selfsign;
};

#ifdef HAVE_WIDGETS
struct ctx {
	struct connection *cx;
	int done;
};

static void
cb_ok(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct ctx *ctx = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	ctx->done = 1;
}

static void
cb_cancel(ggi_widget_t widget, ggiWidgetCallbackType cbt,
	gii_event *ev, struct ggiWidgetInputStatus *inputstatus)
{
	struct ctx *ctx = widget->callbackpriv;

	if (cbt != GWT_CB_ACTIVATE)
		return;

	ctx->done = -1;
}

static char *
common_name(struct cert_level *cert)
{
	struct cert_prop *prop;
	const char *str = NULL;
	char *name;
	size_t len;

	GG_SIMPLEQ_FOREACH(prop, &cert->props, next_prop) {
		if (strcmp(prop->name, "subject"))
			continue;
		str = prop->val;
		for (;;) {
			if (str)
				str = strstr(str, "commonName ");
			if (!str)
				return NULL;
			if (str != prop->val) {
				if (*--str != '\n') {
					str += 2;
					continue;
				}
				++str;
			}
			str += strcspn(str, "\n=");
			if (*str++ != '=')
				continue;
			str += strspn(str, " \t");
			break;
		}
		break;
	}
	if (!str)
		return NULL;
	len = strcspn(str, "\n");
	name = malloc(len + 1);
	if (!name)
		return NULL;
	ggstrlcpy(name, str, len + 1);
	return name;
}

static int
show_cert_chain(struct connection *cx, struct cert_level *level)
{
	struct cert_prop *prop;
	ggi_widget_t item;
	ggi_widget_t atom;
	ggi_widget_t prop_tree;
	ggi_widget_t cert;
	ggi_widget_t certs;
	ggi_widget_t line;
	ggi_widget_t dlg;
	char *name;
	struct ctx ctx = { NULL, -2 };

	ctx.cx = cx;
	cx->width = 571;
	cx->height = 376;

	if (open_visual(cx))
		return 2;
	select_mode(cx);

	ggiCheckMode(cx->stem, &cx->mode);

	if (ggiSetMode(cx->stem, &cx->mode)) {
		close_visual(cx);
		return 1;
	}

#ifdef GGIWMHFLAG_CATCH_CLOSE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CATCH_CLOSE);
#endif
#ifdef GGIWMHFLAG_CLIPBOARD_CHANGE
	ggiWmhAddFlags(cx->stem, GGIWMHFLAG_CLIPBOARD_CHANGE);
#endif

	if (set_title(cx)) {
		close_visual(cx);
		return 2;
	}

	ggiSetFlags(cx->stem, GGIFLAG_ASYNC);
	ggiSetColorfulPalette(cx->stem);

	dlg = ggiWidgetCreateContainerStack(1, NULL);
	if (!dlg)
		goto out;
	dlg->pad.t = dlg->pad.l = dlg->pad.b = dlg->pad.r = 10;

	atom = ggiWidgetCreateLabel("Unable to verify the server certificate");
	if (!atom)
		goto out;
	atom->pad.t = 6;
	atom->pad.b = 10;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, atom);

	if (level && (GG_SIMPLEQ_NEXT(level, next_cert) || !level->selfsign))
		atom = ggiWidgetCreateLabel("Certificate Chain");
	else
		atom = ggiWidgetCreateLabel("Self-signed Certificate");
	if (!atom)
		goto out;
	atom->gravity = GWT_GRAV_WEST;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, atom);

	certs = ggiWidgetCreateContainerStack(1, NULL);
	if (!certs)
		goto out;
	certs->gravity = GWT_GRAV_NORTHWEST;

	for (; level; level = GG_SIMPLEQ_NEXT(level, next_cert)) {
		cert = ggiWidgetCreateContainerTree(1, NULL, NULL, 0, NULL);
		if (!cert) {
			ggiWidgetDestroy(certs);
			goto out;
		}
		cert->gravity = GWT_GRAV_WEST;
		ggiWidgetLinkChild(certs, GWT_LAST_CHILD, cert);
		name = common_name(level);
		atom = ggiWidgetCreateLabel(name ? name : "Cert");
		if (name)
			free(name);
		atom->gravity = GWT_GRAV_WEST;
		ggiWidgetLinkChild(cert, GWT_LAST_CHILD, atom);
		GG_SIMPLEQ_FOREACH(prop, &level->props, next_prop) {
			prop_tree = ggiWidgetCreateContainerTree(
				1, NULL, NULL, 0, NULL);
			if (!prop_tree) {
				ggiWidgetDestroy(certs);
				goto out;
			}
			prop_tree->gravity = GWT_GRAV_WEST;
			ggiWidgetLinkChild(cert, GWT_LAST_CHILD, prop_tree);
			atom = ggiWidgetCreateLabel(prop->name);
			atom->gravity = GWT_GRAV_WEST;
			ggiWidgetLinkChild(prop_tree, GWT_LAST_CHILD, atom);
			atom = ggiWidgetCreateLabel(prop->val);
			atom->gravity = GWT_GRAV_WEST;
			ggiWidgetLinkChild(prop_tree, GWT_LAST_CHILD, atom);
		}
	}

	item = ggiWidgetCreateContainerScroller(
		560, 300, 11, 11, 11, 0.1, 0.1,
		GWT_SCROLLER_OPTION_X_AUTO |
		GWT_SCROLLER_OPTION_Y_AUTO |
		GWT_SCROLLER_OPTION_X_AUTOBARSIZE |
		GWT_SCROLLER_OPTION_Y_AUTOBARSIZE, certs);
	if (!item) {
		ggiWidgetDestroy(certs);
		goto out;
	}
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, item);

	line = ggiWidgetCreateContainerLine(1, NULL);
	if (!line)
		goto out;
	line->gravity = GWT_GRAV_EAST;
	ggiWidgetLinkChild(dlg, GWT_LAST_CHILD, line);
	atom = ggiWidgetCreateLabel("Cancel");
	if (!atom)
		goto out;
	atom->pad.t = atom->pad.b = 3;
	atom->pad.l = atom->pad.r = 12;
	item = ggiWidgetCreateButton(atom);
	if (!item) {
		ggiWidgetDestroy(atom);
		goto out;
	}
	item->callback = cb_cancel;
	item->callbackpriv = &ctx;
	item->hotkey.sym = GIIUC_Escape;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, item);
	ggiWidgetFocus(item, NULL, NULL);

	atom = ggiWidgetCreateLabel("Connect");
	if (!atom) {
		goto out;
	}
	atom->pad.t = atom->pad.b = 3;
	atom->pad.l = atom->pad.r = 8;
	item = ggiWidgetCreateButton(atom);
	if (!item) {
		ggiWidgetDestroy(atom);
		goto out;
	}
	item->callback = cb_ok;
	item->callbackpriv = &ctx;
	ggiWidgetLinkChild(line, GWT_LAST_CHILD, item);

	{
		ggi_widget_t res;

		res = attach_and_fit_visual(cx, dlg, NULL, NULL);

		if (!res) {
			ctx.done = 1;
			goto out;
		}
		dlg = res;
		if (set_title(cx)) {
			ctx.done = 2;
			goto out;
		}
	}

	ctx.done = 0;
	while (!ctx.done) {
		gii_event event;
		giiEventRead(cx->stem, &event, emAll);

#ifdef GGIWMHFLAG_CATCH_CLOSE
		switch (event.any.type) {
		case evFromAPI:
			if (event.fromapi.api_id == libggiwmh->id) {
				switch (event.fromapi.code) {
				case GII_SLI_CODE_WMH_CLOSEREQUEST:
					debug(1, "quiting\n");
					ctx.done = -1;
					break;
				}
			}
			break;
		}
#endif

		ggiWidgetProcessEvent(cx->visualanchor, &event);
		ggiWidgetRedrawWidgets(cx->visualanchor);
	}

	if (ctx.done == 1)
		ctx.done = 0;

out:
	if (dlg) {
		ggiWidgetUnlinkChild(cx->visualanchor,
			GWT_UNLINK_BY_WIDGETPTR, dlg);
		ggiWidgetDestroy(dlg);
	}
	if (ctx.done)
		close_visual(cx);
	return ctx.done;
}
#else
#define show_cert_chain(cx, level) 1
#endif /* HAVE_WIDGETS */

static void
log_error(void)
{
	int res = ERR_get_error();
	while (res) {
		debug(1, "%s\n", ERR_error_string(res, NULL));
		res = ERR_get_error();
	}
}

static int
tls_safe_write(struct connection *cx, const void *buf, int count)
{
	struct vencrypt *vencrypt = cx->vencrypt;
	SSL *ssl;
	int res;
	int written = 0;

	debug(2, "tls_safe_write\n");

	BIO_get_ssl(vencrypt->ssl_bio, &ssl);
	if (!ssl) {
		debug(1, "No SSL object?\n");
		return -1;
	}

again:
	res = SSL_write(ssl, buf, count);

	if (res == count)
		return res + written;

	if (res > 0) {
		count -= res;
		buf = (const uint8_t *)buf + res;
		written += res;
		goto again;
	}

	switch (SSL_get_error(ssl, res)) {
	case SSL_ERROR_WANT_READ:
		vencrypt->write_wants_read = 1;
		debug(2, "SSL_write wants to read\n");
		vnc_stop_write(cx);
		return written;
	case SSL_ERROR_WANT_WRITE:
		vnc_want_write(cx);
		return written;
	case SSL_ERROR_ZERO_RETURN:
		debug(1, "socket closed\n");
		log_error();
		return -1;
	case SSL_ERROR_SYSCALL:
		debug(1, "write error (%d %s)\n", errno, strerror(errno));
		log_error();
		return -1;
	default:
		debug(1, "write error\n");
		log_error();
		return -1;
	}
}

static int
tls_read(struct connection *cx)
{
	struct vencrypt *vencrypt = cx->vencrypt;
	SSL *ssl;
	int request;
	int len;

	debug(2, "tls_read\n");

	BIO_get_ssl(vencrypt->ssl_bio, &ssl);
	if (!ssl) {
		debug(1, "No SSL object?\n");
		close_connection(cx, -1);
		return 0;
	}

again:
	if (cx->input.wpos == cx->input.size) {
		if (buffer_reserve(&cx->input, cx->input.size + 65536)) {
			debug(1, "Out of memory\n");
			close_connection(cx, -1);
			return 0;
		}
	}

	if (cx->max_read && cx->input.size - cx->input.rpos > cx->max_read) {
		request = cx->max_read + cx->input.rpos - cx->input.wpos;
		if (request <= 0) {
			debug(1, "don't tls_read\n");
			vnc_stop_read(cx);
			goto run_actions;
		}
	}
	else
		request = cx->input.size - cx->input.wpos;
	len = SSL_read(ssl, cx->input.data + cx->input.wpos, request);

	switch (SSL_get_error(ssl, len)) {
	case SSL_ERROR_NONE:
		break;
	case SSL_ERROR_WANT_READ:
		goto run_actions;
	case SSL_ERROR_WANT_WRITE:
		vencrypt->read_wants_write = 1;
		debug(2, "SSL_read wants to write\n");
		vnc_want_write(cx);
		vnc_stop_read(cx);
		goto run_actions;
	case SSL_ERROR_ZERO_RETURN:
		debug(1, "socket closed\n");
		log_error();
		close_connection(cx, -1);
		return 0;
	case SSL_ERROR_SYSCALL:
		debug(1, "read error (%d %s)\n", errno, strerror(errno));
		log_error();
		close_connection(cx, -1);
		return 0;
	default:
		debug(1, "read error\n");
		log_error();
		close_connection(cx, -1);
		return 0;
	}

	debug(3, "len=%li\n", len);

	if (cx->bw.counting) {
		if (!cx->bw.count)
			bandwidth_start(cx, len);
		else
			bandwidth_update(cx, len);
	}

	cx->input.wpos += len;

	if (SSL_pending(ssl))
		goto again;

run_actions:
	while (cx->action(cx));

	return 0;
}

static int
tls_write(struct connection *cx)
{
	int res;

	debug(2, "tls_write rpos %d wpos %d\n",
		cx->output.rpos, cx->output.wpos);

	res = cx->safe_write(cx, cx->output.data, cx->output.wpos);

	if (res == cx->output.wpos) {
		vnc_stop_write(cx);

		cx->output.rpos = 0;
		cx->output.wpos = 0;

		return 0;
	}

	if (res >= 0) {
		cx->output.rpos += res;
		remove_dead_data(&cx->output);
		return 0;
	}

	/* error */
	close_connection(cx, -1);
	return 0;
}

static int
tls_read_ready(struct connection *cx)
{
	struct vencrypt *vencrypt = cx->vencrypt;

	if (vencrypt->write_wants_read) {
		vencrypt->write_wants_read = 0;
		debug(2, "SSL_write gets to read\n");
		vnc_want_write(cx);
		tls_write(cx);
		if (vencrypt->write_wants_read)
			return 0;
	}

	return tls_read(cx);
}

static int
tls_write_ready(struct connection *cx)
{
	struct vencrypt *vencrypt = cx->vencrypt;

	if (vencrypt->read_wants_write) {
		vencrypt->read_wants_write = 0;
		debug(2, "SSL_read gets to write\n");
		vnc_want_read(cx);
		if (!cx->output.wpos)
			vnc_stop_write(cx);
		tls_read(cx);
		if (vencrypt->write_wants_read || !cx->output.wpos)
			return 0;
	}

	return tls_write(cx);
}

static int
pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	struct connection *cx = userdata;
	int res;
	char *old_passwd = cx->passwd;

	if (get_password(cx, NULL, 0,
		"Enter password for the private key", size - 1))
	{
		close_connection(cx, -1);
		return 0;
	}

	res = ggstrlcpy(buf, cx->passwd, size);
	free(cx->passwd);
	cx->passwd = old_passwd;
	return res < size ? res : size - 1;
}

static void
info_callback(const SSL *ssl, int where, int ret)
{
	const char *str;
	int w = where & ~SSL_ST_MASK;

	if (w & SSL_ST_CONNECT)
		str = "SSL_connect";
	else if (w & SSL_ST_ACCEPT)
		str = "SSL_accept";
	else
		str = "undefined";

	if (where & SSL_CB_LOOP)
		debug(2, "%s: %s\n", str, SSL_state_string_long(ssl));
        else if (where & SSL_CB_ALERT)
		debug(2, "SSL3 alert %s: %s: %s\n",
			(where & SSL_CB_READ) ? "read" : "write",
			SSL_alert_type_string_long(ret),
			SSL_alert_desc_string_long(ret));
	else if (where & SSL_CB_EXIT) {
		if (ret == 0)
			debug(2, "%s: failed in %s\n",
				str, SSL_state_string_long(ssl));
		else if (ret < 0)
			debug(2, "%s: error in %s\n",
				str, SSL_state_string_long(ssl));
	}
	else if (where & SSL_CB_HANDSHAKE_START)
		debug(2, "handshake start: %s\n", SSL_state_string_long(ssl));
	else if (where & SSL_CB_HANDSHAKE_DONE)
		debug(2, "handshake done: %s\n", SSL_state_string_long(ssl));
	else
		debug(2, "%s: where %d ret %d\n", str, where, ret);
}

static void
msg_callback(int write_p, int version, int content_type,
	const void *buf, size_t len, SSL *ssl, void *arg)
{
	const char *str = write_p ? "write" : "read";
	const char *ver;
	const char *content;

	switch (version) {
	case SSL2_VERSION:
		ver = "SSL2";
		break;
	case SSL3_VERSION:
		ver = "SSL3";
		break;
	case TLS1_VERSION:
		ver = "TLS1";
		break;
	default:
		ver = "unknown";
	}

	switch (content_type) {
	case 20:
		content = "change_cipher_spec";
		break;
	case 21:
		content = "alert";
		break;
	case 22:
		content = "handshake";
		break;
	default:
		content = "unknown";
	}

	debug(2, "msg %s: %s %s: len %d\n", str, ver, content, len);
}

static struct vencrypt *
get_vencrypt(struct connection *cx)
{
	struct vencrypt *vencrypt;
	struct options *opt;

	if (cx->vencrypt)
		return cx->vencrypt;

	vencrypt = malloc(sizeof(*vencrypt));
	if (!vencrypt)
		return NULL;

	opt = malloc(sizeof(*opt));
	if (!opt) {
		free(vencrypt);
		return NULL;
	}

	memset(vencrypt, 0, sizeof(*vencrypt));
	memset(opt, 0, sizeof(*opt));
	vencrypt->opt = opt;

	return vencrypt;
}

static int
start_tls(struct connection *cx, const char *ciphers)
{
	struct vencrypt *vencrypt = cx->vencrypt;
	struct options *opt = vencrypt->opt;
	SSL_CTX *ssl_ctx;
	SSL *ssl;
	BIO *socket_bio;

	debug(2, "start_tls\n");

	cx->max_read = 0;
	vnc_want_read(cx);

	/* This byte is not mentioned in the mail from Stewart on the QEMU
	 * list about VeNCrypt, but it appears to be an intermediate result
	 * so that the client knows if the TLS handshake is about to begin.
	 * Zero from the server means no go.
	 */
	if (!cx->input.data[cx->input.rpos++]) {
		debug(1, "server failure?\n");
		return -1;
	}

	remove_dead_data(&cx->input);

	SSL_load_error_strings();
	ERR_load_BIO_strings();
	SSL_library_init();

	switch (opt->method) {
	case 1:
		ssl_ctx = SSL_CTX_new(TLSv1_client_method());
		break;
	case 2:
		ssl_ctx = SSL_CTX_new(SSLv2_client_method());
		break;
	case 3:
		ssl_ctx = SSL_CTX_new(SSLv3_client_method());
		break;
	case -1:
		ssl_ctx = SSL_CTX_new(SSLv23_client_method());
		break;
	default:
		ssl_ctx = SSL_CTX_new(SSLv23_client_method());
		SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2);
	}
	if (!ssl_ctx) {
		debug(1, "Failed to create SSL_CTX\n");
		log_error();
		return -1;
	}
	vencrypt->ssl_ctx = ssl_ctx;

	if (get_debug_level() >= 2) {
		SSL_CTX_set_info_callback(ssl_ctx, info_callback);
		SSL_CTX_set_msg_callback(ssl_ctx, msg_callback);
	}

	SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
	SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

	if (opt->cert) {
		if (!SSL_CTX_use_certificate_chain_file(ssl_ctx, opt->cert)) {
			debug(1, "Failed to load certificate chain\n");
			log_error();
			goto err_ctx;
		}
	}

	if (opt->ciphers)
		ciphers = opt->ciphers;
	if (!SSL_CTX_set_cipher_list(ssl_ctx, ciphers)) {
		debug(1, "Failed to set cipher list\n");
		log_error();
		goto err_ctx;
	}

	SSL_CTX_set_default_passwd_cb(ssl_ctx, pem_passwd_cb);
	SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, cx);

	if (opt->priv_key) {
		if (!SSL_CTX_use_PrivateKey_file(ssl_ctx,
			opt->priv_key, SSL_FILETYPE_PEM))
		{
			debug(1, "Failed to load private key\n");
			log_error();
			goto err_ctx;
		}
	}

	if (opt->verify_file || opt->verify_dir) {
		if (!SSL_CTX_load_verify_locations(ssl_ctx,
			opt->verify_file, opt->verify_dir))
		{
			debug(1, "Failed to load verify locations\n");
			log_error();
			goto err_ctx;
		}
	}

	SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);

	vencrypt->ssl_bio = BIO_new_ssl(ssl_ctx, 1);
	if (!vencrypt->ssl_bio) {
		debug(1, "Failed to create SSL BIO\n");
		log_error();
		goto err_ctx;
	}

	BIO_get_ssl(vencrypt->ssl_bio, &ssl);
	if (!ssl) {
		debug(1, "No SSL object in SSL BIO\n");
		log_error();
		goto err_ctx;
	}

	socket_bio = BIO_new_socket(cx->sfd, BIO_NOCLOSE);
	if (!socket_bio) {
		debug(1, "Failed to create socket BIO\n");
		log_error();
		goto err_ssl_bio;
	}

	if (cx->input.wpos != cx->input.rpos) {
		BIO *fbio = BIO_new(BIO_f_buffer());

		debug(1, "premature data (%d bytes) received w/o ssl\n",
			cx->input.wpos - cx->input.rpos);

		if (!fbio) {
			debug(1, "Failed to create buffer BIO\n");
			log_error();
			goto err_socket_bio;
		}

		if (!BIO_set_buffer_read_data(fbio,
			&cx->input.data[cx->input.rpos],
			cx->input.wpos - cx->input.rpos))
		{
			debug(1, "Failed to buffer data\n");
			log_error();
			BIO_free_all(fbio);
			goto err_socket_bio;
		}

		cx->input.rpos = cx->input.wpos;
		remove_dead_data(&cx->input);

		BIO_push(fbio, socket_bio);
		socket_bio = fbio;
	}

	if (!BIO_push(vencrypt->ssl_bio, socket_bio)) {
		debug(1, "Failed to join SSL and socket BIOs\n");
		log_error();
		goto err_socket_bio;
	}

	cx->write_ready = tls_write_ready;
	cx->read_ready = tls_read_ready;
	cx->safe_write = tls_safe_write;
	debug(1, "tls up\n");
	return 0;

err_socket_bio:
	BIO_free_all(socket_bio);
err_ssl_bio:
	BIO_free_all(vencrypt->ssl_bio);
	vencrypt->ssl_bio = NULL;
err_ctx:
	SSL_CTX_free(ssl_ctx);
	vencrypt->ssl_ctx = NULL;
	return -1;
}

static int
vnc_security_vencrypt_plain(struct connection *cx)
{
	uint8_t buf[521];
	int ulen, plen;

	debug(2, "vnc_security_vencrypt_plain\n");

	if (!cx->passwd || !cx->username) {
		if (cx->username)
			free(cx->username);
		if (cx->passwd)
			free(cx->passwd);
		if (get_password(cx, "Enter username", 256, NULL, 256))
			return close_connection(cx, -1);
	}

	ulen = strlen(cx->username);
	plen = strlen(cx->passwd);
	insert32_hilo(&buf[0], ulen);
	insert32_hilo(&buf[4], plen);
	memcpy(&buf[8], cx->username, ulen);
	memcpy(&buf[8 + ulen], cx->passwd, plen);

	if (safe_write(cx, buf, 8 + ulen + plen))
		return close_connection(cx, -1);

	cx->action = vnc_security_result;
	return 1;
}

static int
vnc_security_vencrypt_verify_no_peer(struct connection *cx)
{
	struct vencrypt *vencrypt = cx->vencrypt;
	SSL *ssl;
	X509 *peer;

	debug(2, "vnc_security_vencrypt_verify_no_peer\n");

	BIO_get_ssl(vencrypt->ssl_bio, &ssl);
	if (!ssl) {
		debug(1, "No SSL object?\n");
		return close_connection(cx, -1);
	}
	if (!SSL_is_init_finished(ssl))
		return 0;
	/* Handshake done, check that the connection is really anonymous. */
	peer = SSL_get_peer_certificate(ssl);
	if (peer) {
		debug(1, "Unexpected peer certificate present.\n");
		return close_connection(cx, -1);
	}

	cx->action = vencrypt->action;
	return 1;
}

static int
vnc_security_vencrypt_tls(struct connection *cx)
{
	debug(2, "vnc_security_vencrypt_tls\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->max_read = 1;
		return 0;
	}

	if (start_tls(cx, "aNULL:!eNULL:@STRENGTH"))
		return close_connection(cx, -1);

	cx->action = vnc_security_vencrypt_verify_no_peer;
	tls_read(cx);
	return !cx->close_connection;
}

/* Intended to match the output of:
 * openssl x509 -sha1 -in cert.pem -noout -fingerprint
 */
static char *
cert_fingerprint(X509 *cert)
{
	unsigned char *der = NULL;
	unsigned char *tmp;
	int der_len;
	EVP_MD_CTX ctx;
	unsigned char fp[EVP_MAX_MD_SIZE];
	unsigned int fp_len;
	unsigned int i;
	char *fingerprint = NULL;
	char *ptr;

	der_len = i2d_X509(cert, NULL);
	if (der_len < 0) {
		debug(1, "Length of cert in DER format failure.\n");
		goto cleanup;
	}
	der = tmp = malloc(der_len);
	if (!der) {
		debug(1, "Out of memory.\n");
		goto cleanup;
	}
	if (i2d_X509(cert, &tmp) < 0) {
		debug(1, "Failed to convert cert to DER format.\n");
		goto cleanup;
	}

	EVP_MD_CTX_init(&ctx);
	if (!EVP_DigestInit_ex(&ctx, EVP_sha1(), NULL)) {
		debug(1, "Failed to initialize digest.\n");
		goto cleanup;
	}
	if (!EVP_DigestUpdate(&ctx, der, der_len)) {
		debug(1, "Failed to update digest.\n");
		EVP_MD_CTX_cleanup(&ctx);
		goto cleanup;
	}
	if (!EVP_DigestFinal_ex(&ctx, fp, &fp_len)) {
		debug(1, "Failed to finalize digest.\n");
		EVP_MD_CTX_cleanup(&ctx);
		goto cleanup;
	}
	if (!EVP_MD_CTX_cleanup(&ctx)) {
		debug(1, "Failed to cleanup digest.\n");
		goto cleanup;
	}

	fingerprint = malloc(fp_len * 3);
	if (!fingerprint) {
		debug(1, "Out of memory.\n");
		goto cleanup;
	}

	sprintf(fingerprint, "%02x", fp[0]);
	ptr = fingerprint + 2;
	for (i = 1; i < fp_len; ++i, ptr += 3)
		sprintf(ptr, ":%02x", fp[i]);

cleanup:
	if (der)
		free(der);
	return fingerprint;
}

static int
add_prop(struct cert_level *level, const char *name, const char *val)
{
	struct cert_prop *prop = malloc(sizeof(*prop));
	if (!prop)
		return -1;

	prop->name = name;
	prop->val = strdup(val);
	if (!prop->val) {
		free(prop);
		return -1;
	}

	GG_SIMPLEQ_INSERT_TAIL(&level->props, prop, next_prop);
	return 0;
}

static void
free_level(struct cert_level *level)
{
	while (!GG_SIMPLEQ_EMPTY(&level->props)) {
		struct cert_prop *prop = GG_SIMPLEQ_FIRST(&level->props);
		free(prop->val);
		GG_SIMPLEQ_REMOVE_HEAD(&level->props, next_prop);
	}
	free(level);
}

static int
vnc_security_vencrypt_verify_peer(struct connection *cx)
{
	struct vencrypt *vencrypt = cx->vencrypt;
	SSL *ssl;
	STACK_OF(X509) *sk;
	X509 *peer;
	char *fingerprint;
	BIO *mem;
	char *buf;
	const char nul = '\0';
	int result;
	GG_SIMPLEQ_HEAD(chain, cert_level) chain;

	GG_SIMPLEQ_INIT(&chain);

	debug(2, "vnc_security_vencrypt_verify_peer\n");

	BIO_get_ssl(vencrypt->ssl_bio, &ssl);
	if (!ssl) {
		debug(1, "No SSL object?\n");
		return close_connection(cx, -1);
	}
	if (!SSL_is_init_finished(ssl))
		return 0;
	/* Handshake done, check the peer certificate. */
	peer = SSL_get_peer_certificate(ssl);
	if (!peer) {
		debug(1, "No peer certificate.\n");
		return close_connection(cx, -1);
	}
	if (SSL_get_verify_result(ssl) == X509_V_OK) {
		cx->action = vencrypt->action;
		return 1;
	}

	mem = BIO_new(BIO_s_mem());
	BIO_set_close(mem, BIO_CLOSE);

	sk = SSL_get_peer_cert_chain(ssl);
	if (sk) {
		int i;
		X509 *cert;

		for (i = 0; i < sk_X509_num(sk); ++i) {
			struct cert_level *level = malloc(sizeof(*level));
			if (!level) {
				result = 1;
				goto out;
			}
			GG_SIMPLEQ_INIT(&level->props);
			GG_SIMPLEQ_INSERT_TAIL(&chain, level, next_cert);

			cert = sk_X509_value(sk, i);

			BIO_reset(mem);
			X509_NAME_print_ex(mem, X509_get_subject_name(cert),
				0, XN_FLAG_MULTILINE);
			BIO_write(mem, &nul, sizeof(nul));
			BIO_get_mem_data(mem, &buf);
			add_prop(level, "subject", buf);

			BIO_reset(mem);
			X509_NAME_print_ex(mem, X509_get_issuer_name(cert),
				0, XN_FLAG_MULTILINE);
			BIO_write(mem, &nul, sizeof(nul));
			BIO_get_mem_data(mem, &buf);
			add_prop(level, "issuer", buf);

			BIO_reset(mem);
			ASN1_TIME_print(mem, X509_get_notBefore(cert));
			BIO_write(mem, &nul, sizeof(nul));
			BIO_get_mem_data(mem, &buf);
			add_prop(level, "not before", buf);

			BIO_reset(mem);
			ASN1_TIME_print(mem, X509_get_notAfter(cert));
			BIO_write(mem, &nul, sizeof(nul));
			BIO_get_mem_data(mem, &buf);
			add_prop(level, "not after", buf);

			fingerprint = cert_fingerprint(cert);
			add_prop(level, "fingerprint", fingerprint);
			free(fingerprint);

			BIO_reset(mem);
			PEM_write_bio_X509(mem, cert);
			BIO_write(mem, &nul, sizeof(nul));
			BIO_get_mem_data(mem, &buf);
			add_prop(level, "PEM", buf);

			level->selfsign = !X509_NAME_cmp(
				X509_get_subject_name(cert),
				X509_get_issuer_name(cert));
		}
	}
	BIO_free(mem);

	{
		struct cert_level *level;
		struct cert_prop *prop;
		GG_SIMPLEQ_FOREACH(level, &chain, next_cert)
			GG_SIMPLEQ_FOREACH(prop, &level->props, next_prop)
				debug(2, "%s:\n%s\n", prop->name, prop->val);
	}

	result = show_cert_chain(cx, GG_SIMPLEQ_FIRST(&chain));
out:
	while (!GG_SIMPLEQ_EMPTY(&chain)) {
		struct cert_level *level = GG_SIMPLEQ_FIRST(&chain);
		GG_SIMPLEQ_REMOVE_HEAD(&chain, next_cert);
		free_level(level);
	}

	if (result)
		return close_connection(cx, -1);

	cx->action = vencrypt->action;
	return 1;
}

static int
vnc_security_vencrypt_x509(struct connection *cx)
{
	debug(2, "vnc_security_vencrypt_x509\n");

	if (cx->input.wpos < cx->input.rpos + 1) {
		cx->max_read = 1;
		return 0;
	}

	if (start_tls(cx, "ALL:!aNULL:@STRENGTH"))
		return close_connection(cx, -1);

	cx->action = vnc_security_vencrypt_verify_peer;
	tls_read(cx);
	return !cx->close_connection;
}

static int
vnc_security_vencrypt_handle_subtype(struct connection *cx)
{
	struct vencrypt *vencrypt = cx->vencrypt;

	debug(2, "vnc_security_vencrypt_handle_subtype\n");

	if (vencrypt->subtype < 0x100) {
		cx->security = vencrypt->subtype;
		cx->action = vnc_handle_security;
		return 1;
	}

	switch (vencrypt->subtype) {
	case 256:
		cx->action = vnc_security_vencrypt_plain;
		break;

	case 257:
		cx->action = vnc_security_vencrypt_tls;
		vencrypt->action = vnc_security_none;
		break;

	case 258:
		cx->action = vnc_security_vencrypt_tls;
		vencrypt->action = vnc_auth;
		break;

	case 259:
		cx->action = vnc_security_vencrypt_tls;
		vencrypt->action = vnc_security_vencrypt_plain;
		break;

	case 260:
		cx->action = vnc_security_vencrypt_x509;
		vencrypt->action = vnc_security_none;
		break;

	case 261:
		cx->action = vnc_security_vencrypt_x509;
		vencrypt->action = vnc_auth;
		break;

	case 262:
		cx->action = vnc_security_vencrypt_x509;
		vencrypt->action = vnc_security_vencrypt_plain;
		break;

	default:
		return close_connection(cx, -1);
	}

	return 1;
}

static int
vnc_security_vencrypt_subtype(struct connection *cx)
{
	struct vencrypt *vencrypt = cx->vencrypt;
	uint8_t bad_version;
	uint8_t subtypes;
	uint32_t *subtype;
	int i, j;
	int weight = -1;
	int current_weight;
	uint32_t subtype_code = 0;
	uint8_t buf[4];

	debug(2, "vnc_security_vencrypt_subtype\n");

	if (cx->input.wpos < cx->input.rpos + 1)
		return 0;

	bad_version = cx->input.data[cx->input.rpos];
	if (bad_version)
		return close_connection(cx, -1);

	if (cx->input.wpos < cx->input.rpos + 2)
		return 0;

	subtypes = cx->input.data[cx->input.rpos + 1];
	if (!subtypes)
		return close_connection(cx, -1);

	i = vencrypt->version >= 0x0002 ? 4 : 1;
	if (cx->input.wpos < cx->input.rpos + 2 + i * subtypes)
		return 0;

	subtype = malloc(subtypes * sizeof(*subtype));
	if (!subtype)
		return close_connection(cx, -1);

	cx->input.rpos += 2;
	if (vencrypt->version >= 0x0002) {
		memcpy(subtype, &cx->input.data[cx->input.rpos],
			subtypes * sizeof(*subtype));
		cx->input.rpos += subtypes * sizeof(*subtype);
		for (i = 0; i < subtypes; ++i)
			subtype[i] = get32_hilo((uint8_t *)&subtype[i]);
	}
	else {
		for (i = 0; i < subtypes; ++i) {
			subtype[i] = cx->input.data[cx->input.rpos++] + 237;
			if (subtype[i] < 256 || subtype[i] > 262)
				return close_connection(cx, -1);
		}
	}

	for (i = 0; i < subtypes; ++i) {

		debug(1, "subtype: %d - %s\n",
			subtype[i], security_name(subtype[i]));

		for (j = 0; cx->allow_security[j]; ++j) {
			if (cx->allow_security[j] == 16)
				continue;
			if (cx->allow_security[j] == 19)
				continue;
			if (cx->allow_security[j] == subtype[i])
				break;
		}
		if (!cx->allow_security[j])
			continue;
		current_weight = 256 - j;

		if (current_weight && current_weight > weight) {
			weight = current_weight;
			subtype_code = subtype[i];
		}
	}

	free(subtype);

	if (subtypes) {
		if (weight == -1 && cx->force_security) {
			/* No allowed subtype found, be a little dirty and
			 * request the first non-tight subtype. The server
			 * just might allow it...
			 */
			for (j = 0; ; ++j) {
				if (cx->allow_security[j] == 16)
					continue;
				if (cx->allow_security[j] == 19)
					continue;
				break;
			}
			subtype_code = cx->allow_security[j];
		}

		if (!subtype_code)
			return close_connection(cx, -1);

		vencrypt->subtype = subtype_code;

		if (vencrypt->version >= 0x0002) {
			insert32_hilo(buf, subtype_code);

			if (safe_write(cx, buf, sizeof(buf)))
				return close_connection(cx, -1);
		}
		else {
			buf[0] = subtype_code - 237;
			if (safe_write(cx, buf, 1))
				return close_connection(cx, -1);
		}
	}
	else
		vencrypt->subtype = 1;

	remove_dead_data(&cx->input);

	cx->action = vnc_security_vencrypt_handle_subtype;
	return 1;
}

void
vnc_security_vencrypt_end(struct connection *cx, int keep_opt)
{
	struct vencrypt *vencrypt = cx->vencrypt;
	struct options *opt;

	if (!vencrypt)
		return;

	BIO_free_all(vencrypt->ssl_bio);
	SSL_CTX_free(vencrypt->ssl_ctx);
	opt = vencrypt->opt;

	if (keep_opt) {
		memset(vencrypt, 0, sizeof(*vencrypt));
		vencrypt->opt = opt;
		return;
	}

	if (opt->ciphers)
		free(opt->ciphers);
	if (opt->cert)
		free(opt->cert);
	if (opt->priv_key)
		free(opt->priv_key);
	if (opt->verify_file)
		free(opt->verify_file);
	if (opt->verify_dir)
		free(opt->verify_dir);
	free(opt);
	free(cx->vencrypt);
	cx->vencrypt = NULL;
}

int
vnc_security_vencrypt(struct connection *cx)
{
	struct vencrypt *vencrypt;
	uint8_t vencrypt_major;
	uint8_t vencrypt_minor;
	uint16_t version;
	uint8_t buf[2];

	debug(2, "vnc_security_vencrypt\n");

	if (cx->input.wpos < cx->input.rpos + 2)
		return 0;

	vencrypt_major = cx->input.data[cx->input.rpos++];
	vencrypt_minor = cx->input.data[cx->input.rpos++];
	version = (vencrypt_major << 8) | vencrypt_minor;

	debug(1, "server has vencrypt version %u.%u\n",
		vencrypt_major, vencrypt_minor);

	if (!version) {
		debug(1, "vencrypt version 0.0 illegal\n");
		return close_connection(cx, -1);
	}
	if (version > 0x0002) {
		vencrypt_major = 0;
		vencrypt_minor = 2;
		version = (vencrypt_major << 8) | vencrypt_minor;
	}

	debug(1, "requesting vencrypt version %u.%u\n",
		vencrypt_major, vencrypt_minor);

	vencrypt = get_vencrypt(cx);
	if (!vencrypt)
		return close_connection(cx, -1);
	cx->vencrypt = vencrypt;

	vencrypt->version = version;

	buf[0] = vencrypt_major;
	buf[1] = vencrypt_minor;
	if (safe_write(cx, buf, sizeof(buf)))
		return close_connection(cx, -1);

	remove_dead_data(&cx->input);

	cx->action = vnc_security_vencrypt_subtype;
	return 1;
}

int
vencrypt_set_method(struct connection *cx, const char *method)
{
	struct vencrypt *vencrypt;

	vencrypt = get_vencrypt(cx);
	if (!vencrypt)
		return -1;
	cx->vencrypt = vencrypt;

	if (!strcmp(method, "TLSv1"))
		vencrypt->opt->method = 1;
	else if (!strcmp(method, "SSLv2"))
		vencrypt->opt->method = 2;
	else if (!strcmp(method, "SSLv3"))
		vencrypt->opt->method = 3;
	else if (!strcmp(method, "SSLv23"))
		vencrypt->opt->method = -1;
	else
		return -1;

	return 0;
}

int
vencrypt_set_cert(struct connection *cx, const char *cert)
{
	struct vencrypt *vencrypt;

	vencrypt = get_vencrypt(cx);
	if (!vencrypt)
		return -1;
	cx->vencrypt = vencrypt;

	if (vencrypt->opt->cert)
		free(vencrypt->opt->cert);

	vencrypt->opt->cert = strdup(cert);
	if (!vencrypt->opt->cert)
		return -1;

	return 0;
}

int
vencrypt_set_ciphers(struct connection *cx, const char *ciphers)
{
	struct vencrypt *vencrypt;

	vencrypt = get_vencrypt(cx);
	if (!vencrypt)
		return -1;
	cx->vencrypt = vencrypt;

	if (vencrypt->opt->ciphers)
		free(vencrypt->opt->ciphers);

	vencrypt->opt->ciphers = strdup(ciphers);
	if (!vencrypt->opt->ciphers)
		return -1;

	return 0;
}

int
vencrypt_set_priv_key(struct connection *cx, const char *priv_key)
{
	struct vencrypt *vencrypt;

	vencrypt = get_vencrypt(cx);
	if (!vencrypt)
		return -1;
	cx->vencrypt = vencrypt;

	if (vencrypt->opt->priv_key)
		free(vencrypt->opt->priv_key);

	vencrypt->opt->priv_key = strdup(priv_key);
	if (!vencrypt->opt->priv_key)
		return -1;

	return 0;
}

int
vencrypt_set_verify_file(struct connection *cx, const char *verify_file)
{
	struct vencrypt *vencrypt;

	vencrypt = get_vencrypt(cx);
	if (!vencrypt)
		return -1;
	cx->vencrypt = vencrypt;

	if (vencrypt->opt->verify_file)
		free(vencrypt->opt->verify_file);

	vencrypt->opt->verify_file = strdup(verify_file);
	if (!vencrypt->opt->verify_file)
		return -1;

	return 0;
}

int
vencrypt_set_verify_dir(struct connection *cx, const char *verify_dir)
{
	struct vencrypt *vencrypt;

	vencrypt = get_vencrypt(cx);
	if (!vencrypt)
		return -1;
	cx->vencrypt = vencrypt;

	if (vencrypt->opt->verify_dir)
		free(vencrypt->opt->verify_dir);

	vencrypt->opt->verify_dir = strdup(verify_dir);
	if (!vencrypt->opt->verify_dir)
		return -1;

	return 0;
}
