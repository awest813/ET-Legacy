/*
 * ET: Legacy Emscripten compatibility layer.
 *
 * The platform layer itself is sys_unix.c (POSIX, which Emscripten's
 * libc mostly implements). This file provides the POSIX symbols
 * Emscripten does NOT implement (process creation, signals, passwd)
 * plus the browser-specific platform glue hooks.
 */
#ifndef DEDICATED
#include "../sdl/sdl_defs.h"
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"

#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <emscripten.h>

// ---------------------------------------------------------------------------
// POSIX symbols missing from Emscripten's libc (used by sys_unix.c)
//
// NOTE: deliberately NOT defined here - Emscripten's libstubs.a provides
// abort()ing stubs for getpwuid/fork/kill/exec*/system/waitpid and its
// stub object gets pulled into the link as a whole, which would create
// duplicate symbols. The sys_unix.c call sites that could actually run
// in a browser are guarded with __EMSCRIPTEN__ instead; the rest only
// trigger on user-initiated process launching.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Browser glue
// ---------------------------------------------------------------------------

/*
 * Open a URL in a new browser tab (replaces xdg-open/start).
 */
void Sys_EmscriptenOpenURL(const char *url)
{
	if (url && *url)
	{
		EM_ASM({
			try { window.open(UTF8ToString($0), '_blank'); } catch (e) {}
		}, url);
	}
}

/*
 * Called by sys_main.c on error dialogs / opening URLs where the unix
 * layer would fork an external helper. Intercepted through weak
 * overrides below.
 */
void Sys_EmscriptenDialogMessage(const char *error)
{
	if (error && *error)
	{
		EM_ASM({
			var el = document.getElementById('etl_status');
			if (el)
			{
				el.textContent = 'Engine error: ' + UTF8ToString($0);
				el.style.color = '#ff5555';
				el.style.display = 'block';
			}
			console.error('ET:Legacy error: ' + UTF8ToString($0));
		}, error);
	}
}

const char *Sys_EmscriptenHomePath(void)
{
	return "/home/web_user";
}

/*
 * Final status surface for the browser page (called from Sys_Exit).
 */
void Sys_EmscriptenOnExit(int exitCode)
{
	EM_ASM({
		var el = document.getElementById('etl_status');
		var msg = 'Engine stopped (exit ' + $0 + ')';
		if (el)
		{
			el.textContent = msg;
			el.style.display = 'block';
			el.style.color = ($0 === 0) ? '#88ff88' : '#ff5555';
		}
		console.log('ET:Legacy exited with code ' + $0);
	}, exitCode);
}
