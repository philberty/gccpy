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

struct gpy_object_string {
  char * string;
};

gpy_object_t * gpy_obj_string_new (gpy_typedef_t * type,
				   gpy_object_t ** args)
{
  gpy_object_t * retval = NULL_OBJECT;

  bool check = gpy_args_check_fmt (args, "s.");
  gpy_assert (check);

  char * string = gpy_args_lit_parse_string (args [0]);
  struct gpy_object_string * self = (struct gpy_object_string *)
    gpy_malloc (sizeof (struct gpy_object_string));
  self->string = gpy_strdup (string);

  retval = gpy_create_object_state (type, self);

  return retval;
}

void gpy_obj_string_destroy (gpy_object_t * self)
{
  gpy_assert (self->T == TYPE_OBJECT_DECL);
  gpy_object_state_t * object_state = self->o.object_state;

  gpy_free (object_state->state);
  object_state->state = NULL;
}

void gpy_obj_string_print (gpy_object_t * self,
			   FILE * fd,
			   bool newline)
{
  
  gpy_assert (self->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * x = self->o.object_state;
  struct gpy_object_string * state = (struct gpy_object_string *)
    x->state;

  fprintf (fd, "%s", state->string);
  if (newline)
    fprintf (fd, "\n");
}

static struct gpy_typedef_t string_obj = {
  "String",
  0,
  &gpy_obj_string_new,
  &gpy_obj_string_destroy,
  &gpy_obj_string_print,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
};

void gpy_obj_string_mod_init (gpy_vector_t * const vec)
{
  gpy_vec_push (vec, &string_obj);
}
