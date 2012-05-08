/* This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <gmp.h>
#include <mpfr.h>

#ifdef USE_LIBFFI
# include <ffi.h>
#endif

#include <gpython/gpython.h>
#include <gpython/vectors.h>
#include <gpython/objects.h>
#include <gpython/runtime.h>

struct gpy_object_classmethod_t {
  unsigned char * code;
  char * identifier;
  unsigned int nargs;
};

gpy_object_t * gpy_object_classmethod_new (gpy_typedef_t * type,
					   gpy_object_t ** args)
{
  gpy_object_t * retval = NULL_OBJECT;

  bool check = gpy_args_check_fmt (args, "s,p,i.");
  gpy_assert (check);

  char * id = gpy_args_lit_parse_string (args[0]);
  unsigned char * code_addr = gpy_args_lit_parse_pointer (args[1]);
  int nargs = gpy_args_lit_parse_int (args[2]);

  struct gpy_object_classmethod_t * self = gpy_malloc (type->state_size);
  self->identifier = id;
  self->code = code_addr;
  self->nargs = nargs;

  retval = gpy_create_object_decl (type, self);

  return retval;
}

/* free's the object state not the */
void gpy_object_classmethod_dealloc (gpy_object_t * self)
{
  gpy_assert (self->T == TYPE_OBJECT_DECL);
  gpy_object_state_t * object_state = self->o.object_state;

  gpy_free (object_state->state);
  object_state->state = NULL;
}

void gpy_object_classmethod_print (gpy_object_t * self, FILE *fd, bool newline)
{
  fprintf (fd, "class method instance <%p> ", (void *)self);
  if (newline)
    fprintf (fd, "\n");
}

#ifdef USE_LIBFFI

gpy_object_t * gpy_object_classmethod_call (gpy_object_t * self,
					    gpy_object_t ** args)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_assert (self->T == TYPE_OBJECT_DECL);

  unsigned char * code = gpy_object_classmethod_getaddr (self);
  int nargs = gpy_object_classmethod_nparms (self);
  printf ("nargs = %i!\n", nargs);
  if (code)
    {
      ffi_cif cif;
      ffi_type *args[nargs];
      void *values[nargs];
     
      int idx;
      for (idx = 0; idx < nargs; ++idx)
	{
	  args[idx] = &ffi_type_pointer;
	  values[idx] = (void *)(args + idx);
	}
      gpy_assert (ffi_prep_cif (&cif, FFI_DEFAULT_ABI, nargs,
				&ffi_type_void, args)
		  == FFI_OK);
      ffi_call (&cif, (void (*)(void))code, NULL, values);
    }
  return retval;
}

#else /* !defined(USE_LIBFFI) */

gpy_object_t * gpy_object_classmethod_call (gpy_object_t * self,
					    gpy_object_t ** args)
{
  fatal ("no libffi support!\n");
  return NULL;
}

#endif /* !defined(USE_LIBFFI) */

unsigned char * gpy_object_classmethod_getaddr (gpy_object_t * self)
{
  gpy_object_state_t * state = self->o.object_state;
  struct gpy_object_classmethod_t * s = state->state;
  return s->code;
}

int gpy_object_classmethod_nparms (gpy_object_t * self)
{
  gpy_object_state_t * state = self->o.object_state;
  struct gpy_object_classmethod_t * s = state->state;
  return s->nargs;
}

static struct gpy_typedef_t class_functor_obj = {
  "classmethod",
  sizeof (struct gpy_object_classmethod_t),
  &gpy_object_classmethod_new,
  &gpy_object_classmethod_dealloc,
  &gpy_object_classmethod_print,
  &gpy_object_classmethod_call,
  &gpy_object_classmethod_nparms,
  NULL,
  NULL
};

void gpy_obj_classmethod_mod_init (gpy_vector_t * const vec)
{
  gpy_vec_push (vec, &class_functor_obj);
}
