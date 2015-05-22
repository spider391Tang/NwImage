/*
******************************************************************************

   VNC viewer handshake header.

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

#ifndef HANDSHAKE_H
#define HANDSHAKE_H

const char *security_name(uint32_t type);
int vnc_handle_security(struct connection *cx);
int vnc_finish_handshake(struct connection *cx);
int vnc_client_init(struct connection *cx);
int vnc_security_result(struct connection *cx);
int vnc_security_none(struct connection *cx);
int vnc_auth(struct connection *cx);
int vnc_security_tight_init(struct connection *cx);
int vnc_security_tight(struct connection *cx);
int vnc_security_vencrypt(struct connection *cx);
void vnc_security_vencrypt_end(struct connection *cx, int keep_opt);
int vnc_handshake(struct connection *cx);

#endif /* HANDSHAKE_H */
