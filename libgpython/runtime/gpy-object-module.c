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

static gpy_object_attrib_t ** gpy_obj_module_fill (int, char **, int, void *);

static
gpy_object_attrib_t ** gpy_obj_module_fill (int len, char ** idents,
					    int offset, void * self)
{
  gpy_object_attrib_t ** attribs = (gpy_object_attrib_t **)
    gpy_calloc (len + 1, sizeof (gpy_object_attrib_t *));
  memset (attribs, 0, (len + 1) * sizeof (gpy_object_attrib_t *));

  unsigned char ** selfptr = (unsigned char **) self;

  int i;
  for (i = 0; i < len; ++i)
    {
      char * id = idents [i];

      gpy_object_attrib_t * att = (gpy_object_attrib_t *)
	gpy_malloc (sizeof (gpy_object_attrib_t));

      int offs = i * sizeof (gpy_object_t *);
      selfptr [i] = (unsigned char *) (__GPY_RR_STACK_PTR + offset) + offs;

      att->identifier = strdup (id);
      att->T = GPY_MOD;
      att->offset = offs;

      attribs [i] = att;
    }
  attribs [i] = NULL;

  return attribs;
}

gpy_object_t * gpy_obj_module_new (gpy_typedef_t * type,
				   gpy_object_t * args)
{
  gpy_object_t * retval = NULL_OBJECT;

  bool check = gpy_args_check_fmt (args, "i,i,s,S.");
  gpy_assert (check);

  int offset = gpy_args_lit_parse_int (&args [0]);
  int len = gpy_args_lit_parse_int (&args [1]);
  char * ident = gpy_args_lit_parse_string (&args [2]);
  char ** attribs = gpy_args_lit_parse_sarray (&args[3]);

  void * self = gpy_malloc (len * sizeof (gpy_object_t **));
  memset (self, 0, sizeof (gpy_object_t **) * len);

  // Need to fill up self with references to MODULE_STACK address'es
  gpy_object_attrib_t ** attribsDef = gpy_obj_module_fill (len, attribs, offset, self);

  gpy_typedef_t * mtype = gpy_malloc (sizeof (gpy_typedef_t));
  memset (mtype, 0, sizeof (gpy_typedef_t));

  mtype->identifier = ident;
  mtype->state_size = (len * sizeof (gpy_object_t **));
  mtype->tp_new = type->tp_new;
  mtype->tp_dealloc = type->tp_dealloc;
  mtype->tp_print = type->tp_print;
  mtype->members_defintion = attribsDef;

  retval = gpy_create_object_decl (mtype, self);

  return retval;
}

void gpy_obj_module_destroy (gpy_object_t * self)
{
  gpy_assert (self->T == TYPE_OBJECT_DECL);
  gpy_object_state_t object_state = OBJECT_STATE (self);

  gpy_free (object_state.state);
  object_state.state = NULL;
}

void gpy_obj_module_print (gpy_object_t * self,
			   FILE * fd,
			   bool newline)
{
  gpy_typedef_t * type = OBJECT_DEFINITION (self);
  const char * ident = type->identifier;

  fprintf (fd, "Module \'%s\'", ident);
  if (newline)
    fprintf (fd, "\n");
}

static struct gpy_typedef_t module_obj = {
  "Module",
  0,
  &gpy_obj_module_new,
  &gpy_obj_module_destroy,
  &gpy_obj_module_print,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
};

void gpy_obj_module_mod_init (gpy_vector_t * const vec)
{
  gpy_vec_push (vec, &module_obj);
}
