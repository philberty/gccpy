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

struct gpy_obj_integer_t {
  int Int;
  /* other possible members */
};

gpy_object_t * gpy_obj_integer_new (gpy_typedef_t * type,
				    gpy_object_t * args)
{
  gpy_object_t * retval = NULL_OBJECT;

  bool check = gpy_args_check_fmt (args, "i.");
  gpy_assert (check);

  int val = gpy_args_lit_parse_int (&args[0]);
  struct gpy_obj_integer_t * self = (struct gpy_obj_integer_t *)
    gpy_malloc (sizeof (struct gpy_obj_integer_t));
  self->Int = val;

  retval = gpy_create_object_state (type, self);
  return retval;
}

/* Destroys self (type) not the object state */
void gpy_obj_integer_destroy (gpy_object_t * self)
{
  gpy_assert (self->T == TYPE_OBJECT_STATE);
  gpy_object_state_t x = OBJECT_STATE (self);
  struct gpy_obj_integer_t *x1 =
    (struct gpy_obj_integer_t *) x.state;
  gpy_free (x1);
  x.state = NULL;
}

void gpy_obj_integer_print (gpy_object_t * self, FILE * fd, bool newline)
{
  gpy_assert (self->T == TYPE_OBJECT_STATE);
  gpy_object_state_t x = OBJECT_STATE (self);

  struct gpy_obj_integer_t *x1 =
    (struct gpy_obj_integer_t *) x.state;

  fprintf (fd, "%i ", x1->Int);
  if (newline)
    fprintf (fd, "\n");
}

int gpy_obj_integer_getInt (gpy_object_t * self)
{
  gpy_assert (self->T == TYPE_OBJECT_STATE);
  gpy_object_state_t x = OBJECT_STATE (self);
  gpy_assert (!strcmp (OBJECT_DEFINITION (self)->identifier, "Int"));

  struct gpy_obj_integer_t *x1 =
    (struct gpy_obj_integer_t *) x.state;
  return x1->Int;
}

gpy_object_t *
gpy_obj_integer_add (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_state_t x = OBJECT_STATE (o1);
  gpy_object_state_t y = OBJECT_STATE (o2);

  if (!strcmp (x.identifier, "Int"))
    {
      if (!strcmp (y.identifier, "Int"))
	{
	  struct gpy_obj_integer_t *t1 = (struct gpy_obj_integer_t*) x.state;
	  struct gpy_obj_integer_t *t2 = (struct gpy_obj_integer_t*) y.state;

          int z = t1->Int + t2->Int;
	  retval = gpy_rr_fold_integer (z);
	}
      else
	fatal ("invalid object type <%s>!\n", y.identifier);
    }
  else
      fatal ("invalid object type <%s>!\n", x.identifier);
  return retval;
}

gpy_object_t *
gpy_obj_integer_minus (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_state_t x = OBJECT_STATE (o1);
  gpy_object_state_t y = OBJECT_STATE (o2);

  if (!strcmp (x.identifier, "Int"))
    {
      if (!strcmp (y.identifier, "Int"))
	{
	  struct gpy_obj_integer_t *t1 = (struct gpy_obj_integer_t*) x.state;
	  struct gpy_obj_integer_t *t2 = (struct gpy_obj_integer_t*) y.state;

          int z = t1->Int - t2->Int;
	  retval = gpy_rr_fold_integer (z);
	}
      else
	fatal ("invalid object type <%s>!\n", y.identifier);
    }
  else
    fatal ("invalid object type <%s>!\n", x.identifier);
  return retval;
}

gpy_object_t *
gpy_obj_integer_mult (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_state_t x = OBJECT_STATE (o1);
  gpy_object_state_t y = OBJECT_STATE (o2);

  if (!strcmp (x.identifier, "Int"))
    {
      if (!strcmp (y.identifier, "Int"))
	{
	  struct gpy_obj_integer_t *t1 = (struct gpy_obj_integer_t*) x.state;
	  struct gpy_obj_integer_t *t2 = (struct gpy_obj_integer_t*) y.state;

          int z = t1->Int * t2->Int;
	  retval = gpy_rr_fold_integer (z);
	}
      else
	fatal ("invalid object type <%s>!\n", y.identifier);
    }
  else
    fatal ("invalid object type <%s>!\n", x.identifier);
  return retval;
}


gpy_object_t *
gpy_obj_integer_less_than (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_state_t x = OBJECT_STATE (o1);
  gpy_object_state_t y = OBJECT_STATE (o2);

  if (!strcmp (x.identifier, "Int"))
    {
      if (!strcmp (y.identifier, "Int"))
	{
	  struct gpy_obj_integer_t *t1 = (struct gpy_obj_integer_t*) x.state;
	  struct gpy_obj_integer_t *t2 = (struct gpy_obj_integer_t*) y.state;

	  int x = t1->Int;
	  int y = t2->Int;
	  
	  retval = gpy_rr_fold_integer (x < y);
	}
      else
	fatal ("invalid object type <%s>!\n", y.identifier);
    }
  else
    fatal ("invalid object type <%s>!\n", x.identifier);
  return retval;
}

gpy_object_t *
gpy_obj_integer_greater_than (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_state_t x = OBJECT_STATE (o1);
  gpy_object_state_t y = OBJECT_STATE (o2);

  if (!strcmp (x.identifier, "Int"))
    {
      if (!strcmp (y.identifier, "Int"))
	{
	  struct gpy_obj_integer_t *t1 = (struct gpy_obj_integer_t*) x.state;
	  struct gpy_obj_integer_t *t2 = (struct gpy_obj_integer_t*) y.state;

	  int x = t1->Int;
	  int y = t2->Int;
	  retval = gpy_rr_fold_integer (x > y);
	}
      else
	fatal ("invalid object type <%s>!\n", y.identifier);
    }
  else
    fatal ("invalid object type <%s>!\n", x.identifier);
  return retval;
}

gpy_object_t *
gpy_obj_integer_equal_to (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_state_t x = OBJECT_STATE (o1);
  gpy_object_state_t y = OBJECT_STATE (o2);

  if (!strcmp (x.identifier, "Int"))
    {
      if (!strcmp (y.identifier, "Int"))
	{
	  struct gpy_obj_integer_t *t1 = (struct gpy_obj_integer_t*) x.state;
	  struct gpy_obj_integer_t *t2 = (struct gpy_obj_integer_t*) y.state;

	  int x = t1->Int;
	  int y = t2->Int;
	  retval = gpy_rr_fold_integer (x == y);
	}
      else
	fatal ("invalid object type <%s>!\n", y.identifier);
    }
  else
    fatal ("invalid object type <%s>!\n", x.identifier);
  return retval;
}

gpy_object_t *
gpy_obj_integer_not_eq_to (gpy_object_t * o1, gpy_object_t * o2)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_state_t x = OBJECT_STATE (o1);
  gpy_object_state_t y = OBJECT_STATE (o2);

  if (!strcmp (x.identifier, "Int"))
    {
      if (!strcmp (y.identifier, "Int"))
	{
	  struct gpy_obj_integer_t * t1 = (struct gpy_obj_integer_t *)
	    x.state;
	  struct gpy_obj_integer_t * t2 = (struct gpy_obj_integer_t *)
	    y.state;

	  int x = t1->Int;
	  int y = t2->Int;
	  retval = gpy_rr_fold_integer (x != y);
	}
      else
	fatal ("invalid object type <%s>!\n", y.identifier);
    }
  else
    fatal ("invalid object type <%s>!\n", x.identifier);
  return retval;
}

bool gpy_obj_integer_eval_bool (gpy_object_t * x)
{
  bool retval = false;
  gpy_object_state_t t = OBJECT_STATE (x);
  struct gpy_obj_integer_t *state = (struct gpy_obj_integer_t*)
    t.state;

  if (state->Int)
    retval = true;
  return retval;
}

static struct gpy_number_prot_t integer_binary_ops = {
  &gpy_obj_integer_add,
  &gpy_obj_integer_minus,
  NULL,
  &gpy_obj_integer_mult,
  NULL,
  &gpy_obj_integer_less_than,
  NULL,
  &gpy_obj_integer_greater_than,
  NULL,
  &gpy_obj_integer_equal_to,
  &gpy_obj_integer_not_eq_to,
  NULL,
  NULL,
};

static struct gpy_typedef_t integer_obj = {
  "Int",
  sizeof (struct gpy_obj_integer_t),
  &gpy_obj_integer_new,
  &gpy_obj_integer_destroy,
  &gpy_obj_integer_print,
  NULL,
  NULL,
  &gpy_obj_integer_eval_bool,
  &integer_binary_ops,
  NULL
};

/*
  Should be used for handling any Field initilizers!
*/
void gpy_obj_integer_mod_init (gpy_vector_t * const vec)
{
  gpy_vec_push (vec, &integer_obj);
}
