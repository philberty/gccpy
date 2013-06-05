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

#include <gpython/gpython.h>
#include <gpython/vectors.h>
#include <gpython/objects.h>
#include <gpython/runtime.h>

struct gpy_object_list {
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

static gpy_object_t *
gpy_obj_list_add (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_assert (o1->T == TYPE_OBJECT_STATE);
  gpy_assert (o2->T == TYPE_OBJECT_STATE);

  gpy_object_state_t * x = o1->o.object_state;
  gpy_object_state_t * y = o2->o.object_state;

  if (strcmp (x->identifier, "List") != 0 ||
      strcmp (y->identifier, "List") != 0)
    {
      fatal ("invalid object types for '+': <%s> and <%s>\n",
             x->identifier, y->identifier);
    }

  struct gpy_object_list * l1 = (struct gpy_object_list *) x->state;
  struct gpy_object_list * l2 = (struct gpy_object_list *) y->state;

  gpy_vector_t * vec1 = l1->enclosure;
  gpy_vector_t * vec2 = l2->enclosure;

  int n = vec1->length + vec2->length;

  gpy_object_t ** elems = (gpy_object_t **)
    gpy_calloc (n+1, sizeof (gpy_object_t *));

  int i;
  for (i = 0; i < n; ++i)
    {
      gpy_object_state_t * x = o1->o.object_state;
      if (i < vec1->length)
        elems[i] = GPY_VEC_index (gpy_object_t *, vec1, i);
      else
        elems[i] = GPY_VEC_index (gpy_object_t *, vec2, i - vec1->length);
    }
  elems [n] = NULL;

  gpy_literal_t num;
  num.type = TYPE_INTEGER;
  num.literal.integer = n;

  gpy_literal_t elements;
  elements.type = TYPE_VEC;
  elements.literal.vec = elems;

  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = &num };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = &elements };
  gpy_object_t a3 = { .T = TYPE_NULL, .o.literal = NULL };

  gpy_object_t ** args = (gpy_object_t **)
    gpy_calloc (3, sizeof (gpy_object_t *));
  args [0] = &a1;
  args [1] = &a2;
  args [2] = &a3;

  gpy_typedef_t * def = o1->o.object_state->definition;
  gpy_object_t * retval = def->tp_new (def, args);

  gpy_free (args);
  gpy_free (elems);

  gpy_assert (retval->T == TYPE_OBJECT_STATE);
  return retval;
}

static
gpy_object_t * gpy_obj_list_append (gpy_object_t * self,
				    gpy_object_t ** args)
{
  gpy_assert (self->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * x = self->o.object_state;
  struct gpy_object_list * state = (struct gpy_object_list *)
    x->state;

  gpy_vector_t * vec = state->enclosure;

  gpy_object_t * append = *args;
  gpy_assert (append);
  gpy_vec_push (vec, append);

  return NULL;
}

static bool
gpy_obj_list_eval_bool (gpy_object_t * x)
{
  gpy_assert(x->T == TYPE_OBJECT_STATE);

  bool retval = false;
  gpy_object_state_t * t = x->o.object_state;
  struct gpy_object_list * state = (struct gpy_object_list *) t->state;
  gpy_vector_t * vec = state->enclosure;

  if (vec->length != 0)
    retval = true;

  return retval;
}

/*
  tp_slice is very simple so far it only accepts x [1],
  only simple expressions, slices of [lower:upper] does
  not work yet symanticaly.
*/
static
gpy_object_t * gpy_obj_list_getSlice (gpy_object_t * decl,
                                      gpy_object_t * slice)
{
  gpy_assert (decl->T == TYPE_OBJECT_STATE);
  gpy_object_t * retval = NULL;

  int idx = gpy_obj_integer_getInt (slice);
  gpy_object_state_t * t = decl->o.object_state;
  struct gpy_object_list * state = (struct gpy_object_list *) t->state;
  gpy_vector_t * vec = state->enclosure;

  // i know gpy_vec_index does bounds checking but this
  // will allow for more traceability maybe...
  if ((idx >= 0) && (idx < vec->length))
    retval = GPY_VEC_index (gpy_object_t *, vec, idx);
  else
    fatal ("index <%i> is out of bounds on list <%p>\n",
           idx, (void *) decl);
  return retval;
}

static struct gpy_builtinAttribs_t list_methods[] = {
  /* 2 arguments since we have to pass in self, append_item */
  { "append", 2, (GPY_CFUNC) &gpy_obj_list_append },
  /* ... */
  { NULL, 0, NULL }
};

static struct gpy_number_prot_t list_binary_ops = {
  .n_add = &gpy_obj_list_add,
};

static struct gpy_typedef_t list_obj = {
  "List",
  sizeof (struct gpy_object_list),
  &gpy_obj_list_new,
  &gpy_obj_list_destroy,
  &gpy_obj_list_print,
  NULL,
  NULL,
  &gpy_obj_list_eval_bool,
  &list_binary_ops,
  NULL,
  list_methods,
  &gpy_obj_list_getSlice,
};

void gpy_obj_list_mod_init (gpy_vector_t * const vec)
{
  gpy_wrap_builtins (&list_obj, nitems (list_methods));
  gpy_vec_push (vec, &list_obj);
}
