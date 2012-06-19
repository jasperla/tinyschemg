/* $OpenBSD$ */

/*
 * Copyright (c) 2010 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 2012 Jasper Lievisse Adriaanse <jasper@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include "def.h"
#include "scheme-private.h"
#include "dynload.h"
#include "pathnames.h"

scheme sc0;
scheme *sc;

#define is_string(x) sc->vptr->is_string(x)
#define strval(x) sc->vptr->string_value(x)
#define is_int(x) sc->vptr->is_integer(x)
#define intval(x) sc->vptr->ivalue(x)

#define carval(x) sc->vptr->pair_car(x)
#define cdrval(x) sc->vptr->pair_cdr(x)

static char outbuf[BUFSIZ];

static pointer
mgscheme_insert(scheme *sc, pointer args)
{
	char *p = NULL;

	if (args != sc->NIL) {
		if (is_string(carval(args)))
			p = strval(carval(args));
	}
	if (p) {
		while (*p) {
			if (*p == '\n')
				lnewline();
			else
				linsert(1, *p);
			p++;
		}
	}
	return sc->T;
}

void
mgscheme_load_user_init(void)
{
	struct stat sb;
	static char init_scm_cmd[NFILEN];
	char *path;
	int ret;

	path = adjustname(_PATH_MG_DIR"/init.scm", TRUE);
	if (stat(path, &sb) == -1 && errno == ENOENT)
		return;

	ret = snprintf(init_scm_cmd, sizeof(init_scm_cmd), "(load \"%s\")", path);
	if (ret < 0 || ret >= sizeof(init_scm_cmd))
		return;

	scheme_load_string(sc, init_scm_cmd);

	return;
}

void
mgscheme_init(void)
{
       	sc = &sc0;
	scheme_init(sc);
	scheme_set_output_port_string(sc, outbuf, outbuf + BUFSIZ);
	scheme_load_string(sc, "(load \"" _PATH_INIT_SCM "\")");
	scheme_define(sc, sc->global_env, mk_symbol(sc,"load-extension"),
	    mk_foreign_func(sc, scm_load_ext));
	scheme_define(sc, sc->global_env,
	    mk_symbol(sc, "insert"),
	    mk_foreign_func(sc, mgscheme_insert));
	/* Now try to load a user provided ~/.mg.d/init.scm */
	mgscheme_load_user_init();
}

int
mgscheme(int f, int n)
{
	struct buffer *schemebuf;
	char *bufp;
	char buf[NFILEN];

	bzero(buf, sizeof(buf));

	if ((bufp = eread("Eval scheme: ", buf, NFILEN, EFNEW )) == NULL)
		return (ABORT);
	scheme_load_string(sc, bufp);
	schemebuf = bfind("*scheme*", TRUE);
	schemebuf->b_flag |= BFREADONLY;

	if (schemebuf == NULL)
		return (ABORT);
	addline(schemebuf, outbuf);
	return (popbuftop(schemebuf, WNONE));
}

