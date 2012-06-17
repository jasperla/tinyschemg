
#include "def.h"
#include "scheme-private.h"
#include "dynload.h"
#include <ctype.h>

scheme sc0;
scheme *sc;


#define is_string(x) sc->vptr->is_string(x)
#define strval(x) sc->vptr->string_value(x)
#define is_int(x) sc->vptr->is_integer(x)
#define intval(x) sc->vptr->ivalue(x)

#define carval(x) sc->vptr->pair_car(x)
#define cdrval(x) sc->vptr->pair_cdr(x)

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
mgscheme_init(void)
{
	sc = &sc0;
	scheme_init(sc);
	scheme_set_input_port_file(sc, stdin);
	scheme_set_output_port_file(sc, stdout);
	scheme_load_string(sc, "(load \"init.scm\")");
	scheme_define(sc, sc->global_env, mk_symbol(sc,"load-extension"),
	    mk_foreign_func(sc, scm_load_ext));
	scheme_define(sc, sc->global_env,
	    mk_symbol(sc, "insert"),
	    mk_foreign_func(sc, mgscheme_insert));
}

int
mgscheme(int f, int n)
{
	char *bufp;
	char buf[NFILEN];

	if ((bufp = eread("Eval scheme: ", buf, NFILEN, EFNEW )) == NULL)
		return (ABORT);
	scheme_load_string(sc, bufp);
	return (TRUE);
}

