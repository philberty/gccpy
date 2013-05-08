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
#include <stdarg.h>

#include <gpython/gpython.h>
#include <gpython/vectors.h>
#include <gpython/objects.h>
#include <gpython/runtime.h>

#define GPY_ARG_LIT_CHECK(A,I,X)				\
  gpy_assert (A[I]->T == TYPE_OBJECT_LIT);			\
  gpy_assert (A[I]->o.literal->type == X);			\
  ++I;

bool gpy_args_check_fmt (gpy_object_t ** args, const char * fmt)
{
  bool retval = true;

  int idx = 0;
  const char * i = fmt;
  for (i = fmt; *i != '\0'; ++i)
    {
      switch (*i)
	{
	case ',':
	  break;

	case '.':
	  break;

	case 'i':
	  {
	    GPY_ARG_LIT_CHECK (args, idx, TYPE_INTEGER);
	  }
	  break;

	case 's':
	  {
	    GPY_ARG_LIT_CHECK (args, idx, TYPE_STRING);
	  }
	  break;

	case 'p':
	  {
	    GPY_ARG_LIT_CHECK (args, idx, TYPE_ADDR);
	  }
	  break;

	case 'A':
	  {
	    GPY_ARG_LIT_CHECK (args, idx, TYPE_ATTRIB_L);
	  }
	  break;

	case 'V':
	  {
	    GPY_ARG_LIT_CHECK (args, idx, TYPE_VEC);
	  }
	  break;

	default:
	  {
	    error ("unhandled literal argument type <%c>!\n", *i);
	    retval = false;
	  }
	  break;
	}
    }
  return retval;
}


int gpy_args_lit_parse_int (gpy_object_t * arg)
{
  int retval = -1;
  gpy_assert (arg->T == TYPE_OBJECT_LIT);
  gpy_assert (arg->o.literal->type == TYPE_INTEGER);

  retval = arg->o.literal->literal.integer;

  return retval;
}

char * gpy_args_lit_parse_string (gpy_object_t * arg)
{
  char * retval = NULL;
  gpy_assert (arg->T == TYPE_OBJECT_LIT);
  gpy_assert (arg->o.literal->type == TYPE_STRING);

  retval = strdup (arg->o.literal->literal.string);

  return retval;
}

unsigned char * gpy_args_lit_parse_pointer (gpy_object_t * arg)
{
  unsigned char * retval = NULL;
  gpy_assert (arg->T == TYPE_OBJECT_LIT);
  gpy_assert (arg->o.literal->type == TYPE_ADDR);

  retval = arg->o.literal->literal.addr;

  return retval;
}

gpy_object_attrib_t ** gpy_args_lit_parse_attrib_table (gpy_object_t * arg)
{
  gpy_object_attrib_t ** retval = NULL;
  gpy_assert (arg->T == TYPE_OBJECT_LIT);
  gpy_assert (arg->o.literal->type == TYPE_ATTRIB_L);

  retval = arg->o.literal->literal.attribs;

  return retval;
}

gpy_object_t ** gpy_args_lit_parse_vec (gpy_object_t * arg)
{
  gpy_object_t ** retval = NULL;
  gpy_assert (arg->T == TYPE_OBJECT_LIT);
  gpy_assert (arg->o.literal->type == TYPE_VEC);

  retval = arg->o.literal->literal.vec;

  return retval;
}

gpy_object_t * gpy_create_object_decl (gpy_typedef_t * type,
				       void * self)
{
  gpy_object_state_t * state = (gpy_object_state_t *)
    gpy_malloc (sizeof(gpy_object_state_t));
  state->identifier = strdup (type->identifier);
  state->ref_count = 0;
  state->state = self;
  state->definition = type;

  gpy_object_t * retval = (gpy_object_t *)
    gpy_malloc (sizeof(gpy_object_t));
  retval->T = TYPE_OBJECT_DECL;
  retval->o.object_state = state;

  return retval;
}

gpy_object_t * gpy_create_object_state (gpy_typedef_t * type,
					void * self)
{
  gpy_object_state_t * state = (gpy_object_state_t *)
    gpy_malloc (sizeof(gpy_object_state_t));
  state->identifier = strdup (type->identifier);
  state->ref_count = 0;
  state->state = self;
  state->definition = type;

  gpy_object_t * retval = (gpy_object_t *)
    gpy_malloc (sizeof(gpy_object_t));
  retval->T = TYPE_OBJECT_STATE;
  retval->o.object_state = state;

  return retval;
}

void gpy_wrap_builtins (gpy_typedef_t * const type, size_t len)
{
  struct gpy_builtinAttribs_t * builtins = type->builtins;
  struct gpy_builtinAttribs_t atm;

  if (len > 1)
    {
      gpy_object_t ** folded = (gpy_object_t **)
	gpy_calloc (len -1, sizeof (gpy_object_t *));

      int idx;
      for (idx = 0; builtins[idx].identifier != NULL; ++idx)
	{
	  atm = builtins [idx];
	  gpy_object_t * builtin = NULL_OBJECT;

	  gpy_object_t ** args = (gpy_object_t **)
	    gpy_calloc (4, sizeof(gpy_object_t*));

	  gpy_literal_t i;
	  i.type = TYPE_STRING;
	  i.literal.string = (char *)atm.identifier;

	  gpy_literal_t p;
	  p.type = TYPE_ADDR;
	  p.literal.addr = (unsigned char *) atm.addr;

	  gpy_literal_t n;
	  n.type = TYPE_INTEGER;
	  n.literal.integer = atm.nargs;

	  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = &i };
	  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = &p };
	  gpy_object_t a3 = { .T = TYPE_OBJECT_LIT, .o.literal = &n };
	  gpy_object_t a4 = { .T = TYPE_NULL, .o.literal = NULL };

	  args[0] = &a1;
	  args[1] = &a2;
	  args[2] = &a3;
	  args[3] = &a4;

	  gpy_typedef_t * def = __gpy_func_type_node;
	  builtin = def->tp_new (def, args);
	  gpy_free (args);
	  gpy_assert (builtin->T == TYPE_OBJECT_DECL);

	  folded [idx] = builtin;
	}
      /* Now to append/create the attribute access table .. */
      struct gpy_object_attrib_t ** members = (struct gpy_object_attrib_t **)
	gpy_calloc (len, sizeof (struct gpy_object_attrib_t *));

      for (idx = 0; idx < len - 1; ++idx)
	{
	  atm = builtins [idx];
	  struct gpy_object_attrib_t * fattr = (struct gpy_object_attrib_t *)
	    gpy_malloc (sizeof (struct gpy_object_attrib_t));

	  fattr->identifier = strdup (atm.identifier);
	  fattr->T = GPY_CATTR;
	  fattr->offset = 0;
	  fattr->addr = folded [idx];

	  members[idx] = fattr;
	}
      // sentinal
      members[idx] = NULL;
      type->members_defintion = members;
      gpy_free (folded);
    }
}
