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

#include <gpython/gpython.h>
#include <gpython/vectors.h>
#include <gpython/objects.h>

struct gpy_object_list {
  int length;
  gpy_vector_t * enclosure;
};

gpy_object_t * gpy_obj_list_new (gpy_typedef_t * type,
				 gpy_object_t ** args)
{
  gpy_object_t * retval = NULL_OBJECT;

  bool check = gpy_args_check_fmt (args, "i,V.");
  gpy_assert (check);

  int len = gpy_args_lit_parse_int (args [0]);
  gpy_object_t ** vec = gpy_args_lit_parse_vec (args [1]);

  struct gpy_object_list * self = (struct gpy_object_list *)
    gpy_malloc (sizeof (struct gpy_object_list));
  self->length = len;

  self->enclosure = (struct gpy_vector_t *)
    gpy_malloc (sizeof (gpy_vector_t));
  gpy_vec_init (self->enclosure);

  gpy_object_t * node;
  for (node = *vec++; node != NULL; node = *vec++)
    gpy_vec_push (self->enclosure, node);

  retval = gpy_create_object_state (type, self);

  return retval;
}

void gpy_obj_list_destroy (gpy_object_t * self)
{
  gpy_assert (self->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * x = self->o.object_state;
  struct gpy_obj_list *x1 = (struct gpy_obj_list *)
    x->state;

  gpy_free (x1);
  x->state = NULL;
}

void gpy_obj_list_print (gpy_object_t * self,
			 FILE * fd,
			 bool newline)
{
  gpy_assert (self->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * x = self->o.object_state;
  struct gpy_object_list * state = (struct gpy_object_list *)
    x->state;

  gpy_vector_t * vec = state->enclosure;
  void ** array = vec->vector;

  fprintf (fd, "[");
  int i;
  for (i = 0; i < vec->length; ++i)
    {
      gpy_object_t * node = GPY_VEC_index (gpy_object_t *, vec, i);
      struct gpy_typedef_t * def = node->o.object_state->definition;
      def->tp_print (node, stdout, false);

      if ((i + 1) < vec->length)
	fprintf (fd, ", ");
    }
  fprintf (fd, "]");

  if (newline)
    fprintf (fd , "\n");
}

static struct gpy_typedef_t list_obj = {
  "List",
  sizeof (struct gpy_object_list),
  &gpy_obj_list_new,
  &gpy_obj_list_destroy,
  &gpy_obj_list_print,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

void gpy_obj_list_mod_init (gpy_vector_t * const vec)
{
  gpy_vec_push (vec, &list_obj);
}
