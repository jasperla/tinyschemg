/* $OpenBSD$ */

/* dynload.c Dynamic Loader for TinyScheme */
/* Original Copyright (c) 1999 Alexander Shendi     */
/* Modifications for NT and dl_* interface, scm_load_ext: D. Souflis */
/* Refurbished by Stephen Gildea */

/*
 * Copyright (c) 1999 Alexander Shendi
 * Copyright (c) 2000, Dimitrios Souflis
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of Dimitrios Souflis nor the names of the
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _SCHEME_SOURCE
#include "dynload.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef MAXPATHLEN
# define MAXPATHLEN 1024
#endif

static void make_filename(const char *name, char *filename);
static void make_init_fn(const char *name, char *init_fn);

typedef void *HMODULE;
typedef void (*FARPROC)();
#define SUN_DL
#include <dlfcn.h>

#if   defined(SUN_DL)

#include <dlfcn.h>

#define PREFIX "lib"
#define SUFFIX ".so"

static HMODULE dl_attach(const char *module) {
  HMODULE so=dlopen(module,RTLD_LAZY);
  if(!so) {
    fprintf(stderr, "Error loading scheme extension \"%s\": %s\n", module, dlerror());
  }
  return so;
}

static FARPROC dl_proc(HMODULE mo, const char *proc) {
  const char *errmsg;
  FARPROC fp=(FARPROC)dlsym(mo,proc);
  if ((errmsg = dlerror()) == 0) {
    return fp;
  }
  fprintf(stderr, "Error initializing scheme module \"%s\": %s\n", proc, errmsg);
 return 0;
}

static void dl_detach(HMODULE mo) {
 (void)dlclose(mo);
}
#endif

pointer scm_load_ext(scheme *sc, pointer args)
{
   pointer first_arg;
   pointer retval;
   char filename[MAXPATHLEN], init_fn[MAXPATHLEN+6];
   char *name;
   HMODULE dll_handle;
   void (*module_init)(scheme *sc);

   if ((args != sc->NIL) && is_string((first_arg = pair_car(args)))) {
      name = string_value(first_arg);
      make_filename(name,filename);
      make_init_fn(name,init_fn);
      dll_handle = dl_attach(filename);
      if (dll_handle == 0) {
         retval = sc -> F;
      }
      else {
         module_init = (void(*)(scheme *))dl_proc(dll_handle, init_fn);
         if (module_init != 0) {
            (*module_init)(sc);
            retval = sc -> T;
         }
         else {
            retval = sc->F;
         }
      }
   }
   else {
      retval = sc -> F;
   }

  return(retval);
}

static void make_filename(const char *name, char *filename) {
 (void)strlcpy(filename,name,sizeof(filename));
 (void)strlcat(filename,SUFFIX,sizeof(filename));
}

static void make_init_fn(const char *name, char *init_fn) {
 const char *p=strrchr(name,'/');
 if(p==0) {
     p=name;
 } else {
     p++;
 }
 (void)strlcpy(init_fn,"init_",sizeof(init_fn));
 (void)strlcat(init_fn,p,sizeof(init_fn));
}


/*
Local variables:
c-file-style: "k&r"
End:
*/
