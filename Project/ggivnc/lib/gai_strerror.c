/*
******************************************************************************

   gai_strerror stub for ggivnc.

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

#if !defined HAVE_GETADDRINFO || defined _WIN32

#define EAI_ADDRFAMILY_STR \
"The host does not have any addresses in the requested address family."
#define EAI_AGAIN_STR \
"The name server returned a temporary failure indication. Try again."
#define EAI_BADFLAGS_STR \
"ai_flags contains invalid flags."
#define EAI_FAIL_STR \
"The name server returned a permanent failure indication."
#define EAI_FAMILY_STR \
"The address family is not supported at all."
#define EAI_MEMORY_STR \
"Out of memory."
#define EAI_NODATA_STR \
"The host exists, but does not have any addresses defined."
#define EAI_NONAME_STR \
"The node or service is not known; or both node and service are NULL."
#define EAI_SERVICE_STR \
"The service is not available for the requested socket type."
#define EAI_SOCKTYPE_STR \
"The requested socket type is not supported at all."
#define EAI_SYSTEM_STR \
"Other system error, check errno for details."

const char *
gai_strerror(int errcode)
{
	switch (errcode) {
#ifdef EAI_ADDRFAMILY
	case EAI_ADDRFAMILY:
		return EAI_ADDRFAMILY_STR;
#endif
	case EAI_AGAIN:
		return EAI_AGAIN_STR;
	case EAI_BADFLAGS:
		return EAI_BADFLAGS_STR;
	case EAI_FAIL:
		return EAI_FAIL_STR;
	case EAI_FAMILY:
		return EAI_FAMILY_STR;
	case EAI_MEMORY:
		return EAI_MEMORY_STR;
	case EAI_NODATA:
		return EAI_NODATA_STR;
	case EAI_NONAME:
		return EAI_NONAME_STR;
	case EAI_SERVICE:
		return EAI_SERVICE_STR;
	case EAI_SOCKTYPE:
		return EAI_SOCKTYPE_STR;
#ifdef EAI_SYSTEM
	case EAI_SYSTEM:
		return EAI_SYSTEM_STR;
#endif
	}
	return "Unknown addrinfo error.";
}


#else /* !HAVE_GETADDRINFO || _WIN32 */

const char *
gai_strerror(int error)
{
	return "gai_strerror stub.\n";
}

#endif /* !HAVE_GETADDRINFO || _WIN32 */
