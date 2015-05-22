/*
******************************************************************************

   mkstemp replacement for ggivnc.

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

#include "config.h"

#include <string.h>
#ifndef HAVE_IO_H
#include <io.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "vnc.h"
#include "vnc-compat.h"

#ifndef HAVE_MKTEMP
static char *
mktemp(char *template)
{
	return template;
}
#endif /* HAVE_MKTEMP */

int
mkstemp(char *template)
{
	int i;
	int fd = -1;
	char *tmp = strdup(template);

	if (!tmp)
		return -1;

	for (i = 0; i < 10; ++i) {
		if (!mktemp(template))
			break;
		fd = open(template, O_RDWR | O_CREAT | O_EXCL,
			S_IREAD | S_IWRITE);
		if (fd >= 0)
			break;
		strcpy(template, tmp);
	}

	free(tmp);
	return fd;
}
