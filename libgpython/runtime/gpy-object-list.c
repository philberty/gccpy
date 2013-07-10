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

struct gpy_object_list {
  gpy_object_t ** vector;
  int length;
  int size;
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

  int size = gpy_threshold_alloc (len);
  self->vector = (gpy_object_t **)
    gpy_calloc (size, sizeof (gpy_object_t *));

  self->length = len;
  self->size = size;

  int i;
  for (i = 0; i < len; ++i)
    self->vector[i] = vec [i];
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

  gpy_object_t ** gvec = state->vector;
  fprintf (fd, "[");
  int i;
  for (i = 0; i < state->length; ++i)
    {
      gpy_object_t * node = gvec [i];
      struct gpy_typedef_t * def = node->o.object_state->definition;
      def->tp_print (node, stdout, false);

      if (i < (state->length - 1))
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

  int n = l1->length + l2->length;
  int ns = gpy_threshold_alloc (n + 1);
  gpy_object_t ** elems = (gpy_object_t **)
    gpy_calloc (ns, sizeof (gpy_object_t *));

  int i, z;
  for (i = 0; i < l1->length; ++i)
      elems [i] = l1->vector [i];
  for (z = 0; z < l2->length; ++z)
    {
      elems [i] = l2->vector [z];
      i++;
    }
  elems [n] = NULL;

  struct gpy_object_list * self = (struct gpy_object_list *)
    gpy_malloc (sizeof (struct gpy_object_list));

  self->vector = elems;
  self->length = n;
  self->size = ns;

  gpy_object_t * retval = gpy_create_object_state (__gpy_list_type_node, self);
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

  gpy_object_t * append = *args;
  gpy_assert (append);

  if (state->length >= state->size)
    {
      signed long size = gpy_threshold_alloc (state->size);
      state->vector = (gpy_object_t **)
	gpy_realloc (state->vector, size * sizeof (gpy_object_t *));
      state->size = size;
    }
  state->vector [state->length] = append;
  state->length++;

  return NULL;
}

static bool
gpy_obj_list_eval_bool (gpy_object_t * x)
{
  gpy_assert (x->T == TYPE_OBJECT_STATE);

  bool retval = false;
  gpy_object_state_t * t = x->o.object_state;
  struct gpy_object_list * state = (struct gpy_object_list *) t->state;
  
  if (state->length > 0)
    retval = true;

  return retval;
}

/*
  tp_slice is very simple so far it only accepts x [1],
  only simple expressions, slices of [lower:upper] does
  not work yet symanticaly.
*/
static
gpy_object_t ** gpy_obj_list_getRefSlice (gpy_object_t * decl,
					  gpy_object_t * slice)
{
  gpy_assert (decl->T == TYPE_OBJECT_STATE);
  gpy_object_t ** retval = NULL;

  int i = gpy_obj_integer_getInt (slice);
  gpy_object_state_t * t = decl->o.object_state;
  struct gpy_object_list * state = (struct gpy_object_list *) t->state;

  if ((i >= 0) && (i < state->length))
      retval = state->vector + i;
  else
    fatal ("index <%i> is out of bounds on list <%p>\n", i, (void *) decl);

  return retval;
}

static
gpy_object_t * gpy_obj_list_getSlice (gpy_object_t * decl,
				      gpy_object_t * slice)
{
  gpy_object_t ** rs = gpy_obj_list_getRefSlice (decl, slice);
  return *rs;
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
  &gpy_obj_list_getRefSlice,
};

void gpy_obj_list_mod_init (gpy_vector_t * const vec)
{
  gpy_wrap_builtins (&list_obj, nitems (list_methods));
  gpy_vec_push (vec, &list_obj);
}
