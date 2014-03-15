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
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <gpython/gpython.h>
#include <gpython/vectors.h>
#include <gpython/objects.h>
#include <gpython/runtime.h>

struct gpy_object_dict {
  int length;
};

gpy_object_t * gpy_obj_dict_new (gpy_typedef_t * type,
				 gpy_object_t * args)
{
  return NULL;
}

void gpy_obj_dict_destroy (gpy_object_t * self)
{
  return;
}

void gpy_obj_dict_print (gpy_object_t * self, FILE * fd,
			 bool newline)
{
  return;
}

static
gpy_object_t * gpy_obj_dict_clear (gpy_object_t * self,
				   gpy_object_t ** args)
{
  return NULL;
}

static
bool gpy_obj_dict_eval_bool (gpy_object_t * self)
{
  return false;
}

static
gpy_object_t ** gpy_obj_dict_getRefSlice (gpy_object_t * decl,
					  gpy_object_t * slice)
{
  return NULL;
}

static
gpy_object_t * gpy_obj_dict_getSlice (gpy_object_t * decl,
				      gpy_object_t * slice)
{
  gpy_object_t ** rs = gpy_obj_dict_getRefSlice (decl, slice);
  return *rs;
}

static struct gpy_builtinAttribs_t dict_methods[] = {
  /* 1 argument since we have to pass in self */
  { "clear", 1, (GPY_CFUNC) &gpy_obj_dict_clear },
  /* ... */
  { NULL, 0, NULL }
};

static struct gpy_typedef_t dict_obj = {
  "Dict",
  sizeof (struct gpy_object_dict),
  &gpy_obj_dict_new,
  &gpy_obj_dict_destroy,
  &gpy_obj_dict_print,
  NULL,
  NULL,
  &gpy_obj_dict_eval_bool,
  NULL,
  NULL,
  dict_methods,
  &gpy_obj_dict_getSlice,
  &gpy_obj_dict_getRefSlice,
};

void gpy_obj_dict_mod_init (gpy_vector_t * const vec)
{
  gpy_wrap_builtins (&dict_obj, nitems (dict_methods));
  gpy_vec_push (vec, &dict_obj);
}
