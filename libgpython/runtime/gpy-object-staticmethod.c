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

#ifdef USE_LIBFFI
# include <ffi.h>
#endif

#include <gpython/gpython.h>
#include <gpython/vectors.h>
#include <gpython/objects.h>
#include <gpython/runtime.h>

struct gpy_object_staticmethod_t {
  unsigned char * code;
  char * identifier;
  unsigned int nargs;
};

gpy_object_t * gpy_object_staticmethod_new (gpy_typedef_t * type,
					    gpy_object_t * args)
{
  gpy_object_t * retval = NULL_OBJECT;

  bool check = gpy_args_check_fmt (args, "s,p,i.");
  gpy_assert (check);

  char * id = gpy_args_lit_parse_string (&args[0]);
  unsigned char * code_addr = gpy_args_lit_parse_pointer (&args[1]);
  int nargs = gpy_args_lit_parse_int (&args[2]);

  struct gpy_object_staticmethod_t * self =
    (struct gpy_object_staticmethod_t *) gpy_malloc (type->state_size);
  self->identifier = id;
  self->code = code_addr;
  self->nargs = nargs;

  retval = gpy_create_object_decl (type, self);

  return retval;
}

/* free's the object state not the */
void gpy_object_staticmethod_dealloc (gpy_object_t * self)
{
  gpy_assert (self->T == TYPE_OBJECT_DECL);
  gpy_object_state_t object_state = OBJECT_STATE (self);

  gpy_free (object_state.state);
  object_state.state = NULL;
}

void gpy_object_staticmethod_print (gpy_object_t * self, FILE *fd, bool newline)
{
  fprintf (fd, "static method instance <%p> ", (void *) self);
  if (newline)
    fprintf (fd, "\n");
}

gpy_object_t * gpy_object_staticmethod_call (gpy_object_t * self,
					     gpy_object_t ** arguments)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_assert (self->T == TYPE_OBJECT_DECL);

  unsigned char * code = gpy_object_staticmethod_getaddr (self);
  int nargs = gpy_object_staticmethod_nparms (self);
  if (code)
    {
      switch (nargs)
	{
	case 0:
	  {
	    gpy_ffiCall0 c = (gpy_ffiCall0) code;
	    c ();
	  }
	  break;

	case 1:
	  {
	    gpy_ffiCall1 c = (gpy_ffiCall1) code;
	    c (*arguments);
	  }
	  break;

	case 2:
	  {
	    gpy_ffiCall2 c = (gpy_ffiCall2) code;
	    c (*arguments, *(arguments + 1));
	  }
	  break;

	case 3:
	  {
	    gpy_ffiCall3 c = (gpy_ffiCall3) code;
	    c (*arguments, *(arguments + 1), *(arguments + 2));
	  }
	  break;

	case 4:
	  {
	    gpy_ffiCall4 c = (gpy_ffiCall4) code;
	    c (*arguments, *(arguments + 1), (*arguments + 2),
	       *(arguments + 3));
	  }
	  break;

	case 5:
	  {
	    gpy_ffiCall5 c = (gpy_ffiCall5) code;
	    c (*arguments, *(arguments + 1), (*arguments + 2),
	       *(arguments + 3), *(arguments + 4));
	  }
	  break;

	default:
	  {
	    ffi_cif cif;
	    ffi_type *args[nargs];
	    void *values[nargs];

	    int idx;
	    for (idx = 0; idx < nargs; ++idx)
	      {
		args[idx] = &ffi_type_pointer;
	      values[idx] = (void *)(arguments + idx);
	      }
	    gpy_assert (ffi_prep_cif (&cif, FFI_DEFAULT_ABI, nargs,
				      &ffi_type_void, args)
			== FFI_OK);
	    ffi_call (&cif, (void (*)(void))code, NULL, values);
	  }
	  break;
	}
    }
  return retval;
}

int gpy_object_staticmethod_nparms (gpy_object_t * self)
{
  gpy_object_state_t state = OBJECT_STATE (self);
  struct gpy_object_staticmethod_t * s =
    (struct gpy_object_staticmethod_t *) state.state;
  return s->nargs;
}

unsigned char * gpy_object_staticmethod_getaddr (gpy_object_t * self)
{
  gpy_object_state_t state = OBJECT_STATE (self);
  struct gpy_object_staticmethod_t * s =
    (struct gpy_object_staticmethod_t *) state.state;
  return s->code;
}

static struct gpy_typedef_t functor_obj = {
  "staticmethod",
  sizeof (struct gpy_object_staticmethod_t),
  &gpy_object_staticmethod_new,
  &gpy_object_staticmethod_dealloc,
  &gpy_object_staticmethod_print,
  &gpy_object_staticmethod_call,
  &gpy_object_staticmethod_nparms,
  NULL,
  NULL,
  NULL
};

void gpy_obj_staticmethod_mod_init (gpy_vector_t * const vec)
{
  gpy_vec_push (vec, &functor_obj);
}
