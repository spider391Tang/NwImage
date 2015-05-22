/*
******************************************************************************

   getaddrinfo replacement.

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
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
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
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifdef HAVE_WINDOWS_H
# include <windows.h>
#endif
#ifdef HAVE_WINSOCK2_H
# include <winsock2.h>
#endif
#ifdef HAVE_WINSOCK_H
# include <winsock.h>
#endif
#ifdef HAVE_WINSOCK_H
# include <ws2tcpip.h>
#endif
#endif

#include "vnc.h"
#include "vnc-compat.h"

#ifdef _WIN32

typedef WSAAPI int (gai_func)(const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **res);
typedef WSAAPI void (fai_func)(struct addrinfo *res);

static gai_func *getaddrinfo_win32;
static fai_func *freeaddrinfo_win32;

static int
win32_native(void)
{
	static int init;
	char path[MAX_PATH];
	HMODULE dll;
	UINT len = 0;

	if (init)
		return init - 1;

	dll = GetModuleHandle("ws2_32.dll");
	if (dll && GetProcAddress(dll, "getaddrinfo"))
		goto found;

	dll = GetModuleHandle("wship6.dll");
	if (dll && GetProcAddress(dll, "getaddrinfo"))
		goto found;

	len = GetSystemDirectory(path, sizeof(path));
	if (!len || len > sizeof(path) - 12) {
		init = 1;
		return 0;
	}

	strcpy(&path[len], "\\ws2_32.dll");
	dll = LoadLibrary(path);
	if (dll) {
		if (GetProcAddress(dll, "getaddrinfo"))
			goto found;
		FreeLibrary(dll);
	}

	strcpy(&path[len], "\\wship6.dll");
	dll = LoadLibrary(path);
	if (dll) {
		if (GetProcAddress(dll, "getaddrinfo"))
			goto found;
		FreeLibrary(dll);
	}

	init = 1;
	return 0;

found:
	getaddrinfo_win32 = (gai_func *)GetProcAddress(dll, "getaddrinfo");
	freeaddrinfo_win32 = (fai_func *)GetProcAddress(dll, "freeaddrinfo");
	if (!(getaddrinfo_win32 && freeaddrinfo_win32)) {
		if (len)
			FreeLibrary(dll);
		init = 1;
	}
	else
		init = 2;

	return init - 1;
}

#endif /* _WIN32 */

static struct addrinfo *
add_ipv4(int protocol, int socktype, struct in_addr *addr, uint16_t port,
	const struct addrinfo *hints, const char *canonname)
{
	struct addrinfo *ai;
	struct sockaddr_in *sa;

	ai = malloc(sizeof(*ai));
	if (!ai)
		return NULL;
	memset(ai, 0, sizeof(*ai));

	sa = malloc(sizeof(*sa));
	if (!sa) {
		free(ai);
		return NULL;
	}

	if (hints->ai_flags & AI_CANONNAME) {
		ai->ai_canonname = strdup(canonname);
		if (!ai->ai_canonname) {
			free(sa);
			free(ai);
			return NULL;
		}
	}

	ai->ai_addr = (struct sockaddr *)sa;
	sa->sin_family = AF_INET;
	sa->sin_addr = *addr;
	sa->sin_port = port;
	ai->ai_addrlen = sizeof(*sa);

	ai->ai_family = AF_INET;
	ai->ai_protocol = protocol;
	ai->ai_socktype = socktype;

	return ai;
}

int
getaddrinfo(const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **res)
{
	struct hostent *h;
	struct sockaddr_in *sa;
	struct addrinfo *ai;
	struct addrinfo **cur = &ai;
	uint16_t tcp_port = 0;
	uint16_t udp_port = 0;
	struct addrinfo hint;
	int i;

#ifdef _WIN32
	if (win32_native())
		return getaddrinfo_win32(node, service, hints, res);
#endif

	if (hints)
		hint = *hints;
	else {
		memset(&hint, 0, sizeof(hint));
		hint.ai_family = PF_UNSPEC;
	}

	if (hint.ai_flags & ~AI_MASK)
		return EAI_BADFLAGS;
	if (hint.ai_family == PF_UNSPEC)
		hint.ai_family = PF_INET;
	if (hint.ai_family != PF_INET)
		return EAI_FAMILY;
	if (!hint.ai_protocol) {
		/* socktype allowed to control protocol */
		if (hint.ai_socktype == SOCK_STREAM)
			hint.ai_protocol = IPPROTO_TCP;
		else if (hint.ai_socktype == SOCK_DGRAM)
			hint.ai_protocol = IPPROTO_UDP;
	}
	else if (hint.ai_protocol == IPPROTO_TCP)
		;
	else if (hint.ai_protocol == IPPROTO_UDP)
		;
	else
		return EAI_SOCKTYPE;
	if (!hint.ai_socktype) {
		/* protocol allowed to control socktype */
		if (hint.ai_protocol == IPPROTO_TCP)
			hint.ai_socktype = SOCK_STREAM;
		else if (hint.ai_protocol == IPPROTO_UDP)
			hint.ai_socktype = SOCK_DGRAM;
	}
	else if (hint.ai_socktype == SOCK_STREAM)
		;
	else if (hint.ai_socktype == SOCK_DGRAM)
		;
	else
		return EAI_SOCKTYPE;

	if (!node && !service)
		return EAI_NONAME;

	if (!node) {
		if (hint.ai_flags & AI_PASSIVE)
			node = "0.0.0.0";
		else
			node = "127.0.0.1";
	}

	if (service) {
		if (!*service)
			return EAI_NONAME;
		if (!(hint.ai_flags & AI_NUMERICSERV)) {
			struct servent *s;
			if (!hint.ai_socktype ||
				hint.ai_socktype == SOCK_STREAM)
			{
				s = getservbyname(service, "tcp");
				if (s)
					tcp_port = s->s_port;
			}
			if (!hint.ai_socktype ||
				hint.ai_socktype == SOCK_DGRAM)
			{
				s = getservbyname(service, "udp");
				if (s)
					udp_port = s->s_port;
			}
		}
		if (!tcp_port && !udp_port) {
			char *end;
			unsigned long port;
			port = strtoul(service, &end, 10);
			if (*end || !port)
				return EAI_NONAME;
			tcp_port = udp_port = htons(port);
			if (ntohs(tcp_port) != port)
				return EAI_NONAME;
		}
		else if (tcp_port && !udp_port) {
			hint.ai_protocol = IPPROTO_TCP;
			hint.ai_socktype = SOCK_STREAM;
		}
		else if (!tcp_port && udp_port) {
			hint.ai_protocol = IPPROTO_UDP;
			hint.ai_socktype = SOCK_DGRAM;
		}
	}

	if (hint.ai_flags & AI_NUMERICHOST) {
		struct in_addr addr;
#ifdef HAVE_INET_ATON
		if (!inet_aton(node, &addr))
			return EAI_NONAME;
#else
		addr.s_addr = inet_addr(node);
		if (addr.s_addr == INADDR_NONE)
			return EAI_NONAME;
#endif

		if (!hint.ai_socktype || hint.ai_socktype == SOCK_STREAM) {
			*cur = add_ipv4(IPPROTO_TCP, SOCK_STREAM,
				&addr, tcp_port, &hint, node);
			if (!*cur) {
				freeaddrinfo(ai);
				return EAI_MEMORY;
			}
			cur = &(*cur)->ai_next;
		}

		if (!hint.ai_socktype || hint.ai_socktype == SOCK_DGRAM) {
			*cur = add_ipv4(IPPROTO_UDP, SOCK_DGRAM,
				&addr, udp_port, &hint, node);
			if (!*cur) {
				freeaddrinfo(ai);
				return EAI_MEMORY;
			}
			cur = &(*cur)->ai_next;
		}

		*res = ai;
		return 0;
	}

	h = gethostbyname(node);

	if (!h) {
		switch (h_errno) {
		case HOST_NOT_FOUND:
			return EAI_NONAME;
		case NO_ADDRESS:
#if NO_ADDRESS != NO_DATA
		case NO_DATA:
#endif
			return EAI_NODATA;
		case NO_RECOVERY:
			return EAI_FAIL;
		case TRY_AGAIN:
			return EAI_AGAIN;
		default:
			return EAI_FAIL;
		}
	}
	if (h->h_addrtype != AF_INET)
		return EAI_FAMILY;

	*cur = NULL;
	for (i = 0; h->h_addr_list[i]; ++i) {
		if (h->h_addrtype != AF_INET)
			continue;

		if (h->h_length != sizeof(sa->sin_addr)) {
			freeaddrinfo(ai);
			return EAI_FAIL;
		}

		if (!hint.ai_socktype || hint.ai_socktype == SOCK_STREAM) {
			*cur = add_ipv4(IPPROTO_TCP, SOCK_STREAM,
				(struct in_addr *)h->h_addr_list[i], tcp_port,
				&hint, h->h_name ? h->h_name : node);
			if (!*cur) {
				freeaddrinfo(ai);
				return EAI_MEMORY;
			}
			cur = &(*cur)->ai_next;
		}

		if (!hint.ai_socktype || hint.ai_socktype == SOCK_DGRAM) {
			*cur = add_ipv4(IPPROTO_UDP, SOCK_DGRAM,
				(struct in_addr *)h->h_addr_list[i], udp_port,
				&hint, h->h_name ? h->h_name : node);
			if (!*cur) {
				freeaddrinfo(ai);
				return EAI_MEMORY;
			}
			cur = &(*cur)->ai_next;
		}
	}

	if (!ai)
		return EAI_NONAME;

	*res = ai;
	return 0;
}

void
freeaddrinfo(struct addrinfo *res)
{
	struct addrinfo *next;

#ifdef _WIN32
	if (win32_native()) {
		freeaddrinfo_win32(res);
		return;
	}
#endif

	for (next = res; res; res = next) {
		if (res->ai_canonname)
			free(res->ai_canonname);
		if (res->ai_addr)
			free(res->ai_addr);
		next = res->ai_next;
		free(res);
	}
}
