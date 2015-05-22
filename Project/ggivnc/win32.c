/*
******************************************************************************

   Win32 specific cruft.

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

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#define WIN32_LEAN_AND_MEAN
#ifdef HAVE_WINDOWS_H
# include <windows.h>
# include <io.h>
#endif
#ifdef HAVE_WINSOCK2_H
# include <winsock2.h>
#endif
#ifdef HAVE_WINSOCK_H
# include <winsock.h>
#endif
#ifdef HAVE_SHLOBJ_H
# include <shlobj.h>
#endif

#include <limits.h>

#include "vnc.h"
#include "vnc-compat.h"

#include "resource.h"

static BOOL CALLBACK
locate_wnd_callback(HWND wnd, LPARAM param)
{
	DWORD pid;
	HICON icon;

	GetWindowThreadProcessId(wnd, &pid);
	if(pid != GetCurrentProcessId())
		return TRUE;

#ifndef HAVE_WMH
	SetWindowText(wnd, "ggivnc");
#endif /* HAVE_WMH */

	icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_GGIVNC));
	if (icon)
		SendMessage(wnd, WM_SETICON, ICON_BIG, (LPARAM)icon);

	icon = (HICON)LoadImage(GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDI_GGIVNC), IMAGE_ICON, 16, 16, 0);
	if (icon)
		SendMessage(wnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

	return TRUE;
}

void
set_icon(void)
{
	/* Try to locate the window, if any... */
	EnumWindows(locate_wnd_callback, 0);
}

int
socket_init(void)
{
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;

	wVersionRequested = MAKEWORD(2, 0);

	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0)
		return -1;

	if (LOBYTE(wsaData.wVersion) < 1) {
		WSACleanup();
		return -1;
	}

	if (LOBYTE(wsaData.wVersion) == 1 && HIBYTE(wsaData.wVersion) < 1) {
		WSACleanup();
		return -1;
	}

	if (LOBYTE(wsaData.wVersion) > 2) {
		WSACleanup();
		return -1;
	}

	return 0;
}

void
socket_cleanup(void)
{
	WSACleanup();
}

void
console_init(void)
{
	/* Try to attach to the console of the parent.
	 * Only available on XP/2k3 and later.
	 */
	HMODULE kernel32;
	BOOL (WINAPI *attach_console)(DWORD);
	FILE *f;
	int fd;

	kernel32 = LoadLibrary("kernel32.dll");
	if (!kernel32)
		return;
	attach_console = GetProcAddress(kernel32, "AttachConsole");
	if (!attach_console)
		goto free_lib;

	if (!attach_console((DWORD)-1))
		goto free_lib;

	/* This is horrible, but there is no fdreopen... */
	fd = _open_osfhandle(
		(intptr_t)GetStdHandle(STD_OUTPUT_HANDLE), _O_TEXT);
	if (fd >= 0) {
		f = _fdopen(fd, "wt");
		if (f)
			*stdout = *f;
		else
			close(fd);
	}
	fd = _open_osfhandle(
		(intptr_t)GetStdHandle(STD_ERROR_HANDLE), _O_TEXT);
	if (fd >= 0) {
		f = _fdopen(fd, "wt");
		if (f)
			*stderr = *f;
		else
			close(fd);
	}

free_lib:
	FreeLibrary(kernel32);
}

static inline FARPROC
proc_address(HMODULE module, LPCSTR entry_point)
{
	return module ? GetProcAddress(module, entry_point) : NULL;
}

#ifdef HAVE_SHLOBJ_H

static BOOL WINAPI
ProbeSHGetSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath,
	int nFolder, BOOL fCreate);
static BOOL (WINAPI *SHGetSpecialFolderPathAnsi)(HWND, LPSTR, int, BOOL)
	= ProbeSHGetSpecialFolderPathA;

/*
 * The version to use if we are forced to emulate.
 */
static BOOL WINAPI
EmulateSHGetSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath,
	int nFolder, BOOL fCreate)
{
	return FALSE;
}

/*
 * Stub that probes to decide which version to use.
 * Probing needed since the Internet Explorer 4.0 Desktop Update
 * is required on NT4 and Win95.
 */
static BOOL WINAPI
ProbeSHGetSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath,
	int nFolder, BOOL fCreate)
{
	HINSTANCE hinst;

	hinst = LoadLibrary("Shell32");
	SHGetSpecialFolderPathAnsi =
		proc_address(hinst, "SHGetSpecialFolderPathA");

	if(!SHGetSpecialFolderPathAnsi)
		SHGetSpecialFolderPathAnsi = EmulateSHGetSpecialFolderPathA;

	return SHGetSpecialFolderPathAnsi(hwndOwner, lpszPath,
					nFolder, fCreate);
}

#endif /* HAVE_SHLOBJ_H */

char *
get_appdata_path(void)
{
#ifdef HAVE_SHLOBJ_H
	static char path[PATH_MAX];
	if(SHGetSpecialFolderPathAnsi(NULL, path, CSIDL_APPDATA, FALSE))
		return path;
#endif /* HAVE_SHLOBJ_H */
	return NULL;
}

static BOOL WINAPI
ProbeMoveFileExA(LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName,
	DWORD dwFlags);
static BOOL (WINAPI *MoveFileExAnsi)(LPCTSTR, LPCTSTR, DWORD)
	= ProbeMoveFileExA;

/*
 * The version to use if we are forced to emulate.
 */
static BOOL WINAPI
EmulateMoveFileExA(LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName,
	DWORD dwFlags)
{
	if (dwFlags & MOVEFILE_REPLACE_EXISTING)
		unlink(lpNewFileName);
	return MoveFile(lpExistingFileName, lpNewFileName);
}

/*
 * Stub that probes to decide which version to use.
 * Probing since Win2k is required.
 */
static BOOL WINAPI
ProbeMoveFileExA(LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName,
	DWORD dwFlags)
{
	HINSTANCE hinst;

	hinst = LoadLibrary("Kernel32");
	MoveFileExAnsi = proc_address(hinst, "MoveFileExA");

	if (!MoveFileExAnsi)
		MoveFileExAnsi = EmulateMoveFileExA;

	return MoveFileExAnsi(lpExistingFileName, lpNewFileName, dwFlags);
}

int
rename(const char *old_path, const char *new_path)
{
	return !MoveFileExAnsi(old_path, new_path, MOVEFILE_REPLACE_EXISTING);
}

#ifdef _MSC_VER
int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	return main(__argc, __argv);
}
#endif
