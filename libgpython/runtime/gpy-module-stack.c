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

// Holds all runtime information to primitive types
gpy_vector_t * __GPY_GLOBL_PRIMITIVES;

// runtime stack for all module runtime data

/*
  NOTE on thread-saftey for future, this stack will need
  Global Lock, and a per thread __CALL_STACK & __RET_ADDR
*/

/*
__STACK
 -> _LENGTH
 -> --------
 -> * maybe __CALL_STACK //TODO
 -> * maybe __RET_ADDR   //TODO
 -> --------
 -> MODULE_A
 -> MODULE_B
...
*/
#define __GPY_INIT_LEN              3
static int __GPY_GLOBAL_STACK_LEN = 0;
static gpy_hash_tab_t stack_table;

gpy_object_t ** __GPY_MODULE_RR_STACK;
gpy_object_t ** __GPY_RR_STACK_PTR;

static
void gpy_rr_init_primitives (void)
{
  gpy_obj_integer_mod_init (__GPY_GLOBL_PRIMITIVES);
  gpy_obj_staticmethod_mod_init (__GPY_GLOBL_PRIMITIVES);
  gpy_obj_class_mod_init (__GPY_GLOBL_PRIMITIVES);
  gpy_obj_classmethod_mod_init (__GPY_GLOBL_PRIMITIVES);
  gpy_obj_list_mod_init (__GPY_GLOBL_PRIMITIVES);
}

static
void gpy_rr_init_runtime_stack (void)
{
  gpy_dd_hash_init_table (&stack_table);

  __GPY_GLOBL_PRIMITIVES = gpy_malloc (sizeof (gpy_vector_t));
  gpy_vec_init (__GPY_GLOBL_PRIMITIVES);
  gpy_rr_init_primitives ();

  __GPY_MODULE_RR_STACK = (gpy_object_t **) gpy_calloc
    (__GPY_INIT_LEN, sizeof (gpy_object_t *));
  *__GPY_MODULE_RR_STACK = gpy_rr_fold_integer (__GPY_INIT_LEN);
  __GPY_RR_STACK_PTR = (__GPY_MODULE_RR_STACK + __GPY_INIT_LEN);

  __GPY_GLOBAL_STACK_LEN += __GPY_INIT_LEN;
}

/* remember to update the stack pointer's and the stack size */
void gpy_rr_extend_runtime_stack (int nslots)
{
  // calculate the size of reallocation
  size_t size = sizeof (gpy_object_t *) * (__GPY_INIT_LEN + nslots);
  __GPY_MODULE_RR_STACK = gpy_realloc (__GPY_MODULE_RR_STACK, size);
  __GPY_GLOBAL_STACK_LEN += nslots;

  // update the stack pointer to the begining of all modules
  __GPY_RR_STACK_PTR = __GPY_MODULE_RR_STACK;
  __GPY_RR_STACK_PTR += __GPY_GLOBAL_STACK_LEN + (nslots - 1);
}

void gpy_rr_initRRStack (int slots,
			 gpy_object_t ** sptr,
			 const char * stack_id)
{
  /* Make sure it doesn't already exist! */
  gpy_hashval_t h = gpy_dd_hash_string (stack_id);
  gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (&stack_table, h);
  if (!e)
    {
      /* extend the stack and setup the stack pointer for the callee' */
      gpy_rr_extend_runtime_stack (slots);
      sptr = __GPY_RR_STACK_PTR;
      gpy_dd_hash_insert (h, sptr, &stack_table);
    }
  else
    fatal ("Stack id <%s> already exists!\n", stack_id);
}

void gpy_rr_init_runtime (void)
{
  gpy_rr_init_runtime_stack ();
}

void gpy_rr_cleanup_final (void)
{
  /*
    Cleanup the runtime stack and all other object data
   */
  mpfr_free_cache ();
}

gpy_object_attrib_t * gpy_rr_fold_attribute (const unsigned char * identifier,
					     unsigned char * code_addr,
					     unsigned int offset, int nargs)
{
  gpy_object_attrib_t * attrib = gpy_malloc (sizeof (gpy_object_attrib_t));
  attrib->identifier = identifier;
  if (code_addr)
    {
      gpy_object_t * f = gpy_rr_fold_classmethod_decl (identifier, code_addr, nargs);
      attrib->addr = f;
    }
  else
    attrib->addr = NULL;

  attrib->offset = offset;
  return attrib;
}

gpy_object_attrib_t ** gpy_rr_fold_attrib_list (int n, ...)
{
  gpy_object_attrib_t ** retval = NULL;
  if (n > 0)
    {
      /* +1 for the sentinal */
      retval = (gpy_object_attrib_t **)
	gpy_calloc (n+1, sizeof (gpy_object_attrib_t *));

      va_list ap;
      int idx;
      va_start (ap, n);
      for (idx = 0; idx < n; ++idx)
	{
	  gpy_object_attrib_t * i = va_arg (ap, gpy_object_attrib_t *);
	  retval[idx] = i;
	}
      /* sentinal */
      retval[idx] = NULL;
      va_end (ap);
    }
  return retval;
}

gpy_object_t * gpy_rr_fold_encList (int n, ...)
{
  gpy_object_t * retval = NULL;
  va_list ap;
  va_start (ap, n);

  gpy_object_t ** elems = (gpy_object_t **)
    gpy_calloc (n+1, sizeof (gpy_object_t *));

  int i;
  for (i = 0; i < n; ++i)
    {
      gpy_object_t * elem = va_arg (ap, gpy_object_t *);
      elems [i] = elem;
    }
  elems [i] = NULL;
  va_end (ap);

  /* + 2 for first argument the length of elements and for sentinal */
  gpy_object_t ** args = (gpy_object_t **)
    gpy_calloc (3, sizeof (gpy_object_t *));

  gpy_literal_t num;
  num.type = TYPE_INTEGER;
  num.literal.integer = n;

  gpy_literal_t elements;
  elements.type = TYPE_VEC;
  elements.literal.vec = elems;

  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = &num };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = &elements };
  gpy_object_t a3 = { .T = TYPE_NULL, .o.literal = NULL };
  args [0] = &a1;
  args [1] = &a2;
  args [2] = &a3;

  gpy_typedef_t * def = __gpy_list_type_node;
  retval = def->tp_new (def, args);

  gpy_free (args);
  gpy_free (elems);

  gpy_assert (retval->T == TYPE_OBJECT_STATE);
  return retval;
}

gpy_object_t * gpy_rr_fold_class_decl (gpy_object_attrib_t ** attribs,
				       int size, const char * identifier)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_object_t ** args = (gpy_object_t **)
    gpy_calloc (4, sizeof(gpy_object_t*));

  gpy_literal_t A;
  A.type = TYPE_ATTRIB_L;
  A.literal.attribs = attribs;

  gpy_literal_t i;
  i.type = TYPE_INTEGER;
  i.literal.integer = size;

  gpy_literal_t s;
  s.type = TYPE_STRING;
  s.literal.string = (char *) identifier;

  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = &A };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = &i };
  gpy_object_t a3 = { .T = TYPE_OBJECT_LIT, .o.literal = &s };
  gpy_object_t a4 = { .T = TYPE_NULL, .o.literal = NULL };

  args[0] = &a1;
  args[1] = &a2;
  args[2] = &a3;
  args[3] = &a4;

  gpy_typedef_t * def = __gpy_class_type_node;
  retval = def->tp_new (def, args);
  gpy_free (args);

  gpy_assert (retval->T == TYPE_OBJECT_DECL);
  return retval;
}

gpy_object_t * gpy_rr_fold_staticmethod_decl (const char * identifier,
					      unsigned char * code_addr,
					      int nargs)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_object_t ** args = (gpy_object_t **)
    gpy_calloc (4, sizeof(gpy_object_t*));

  gpy_literal_t i;
  i.type = TYPE_STRING;
  i.literal.string = (char *)identifier;

  gpy_literal_t p;
  p.type = TYPE_ADDR;
  p.literal.addr = code_addr;

  gpy_literal_t n;
  n.type = TYPE_INTEGER;
  n.literal.integer = nargs;

  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = &i };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = &p };
  gpy_object_t a3 = { .T = TYPE_OBJECT_LIT, .o.literal = &n };
  gpy_object_t a4 = { .T = TYPE_NULL, .o.literal = NULL };

  args[0] = &a1;
  args[1] = &a2;
  args[2] = &a3;
  args[3] = &a4;

  gpy_typedef_t * def = __gpy_staticmethod_type_node;
  retval = def->tp_new (def, args);
  gpy_free (args);

  gpy_assert (retval->T == TYPE_OBJECT_DECL);

  return retval;
}

gpy_object_t * gpy_rr_fold_classmethod_decl (const char * identifier,
					     unsigned char * code_addr,
					     int nargs)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_object_t ** args = (gpy_object_t **)
    gpy_calloc (4, sizeof(gpy_object_t*));

  gpy_literal_t s;
  s.type = TYPE_STRING;
  s.literal.string = (char *)identifier;

  gpy_literal_t p;
  p.type = TYPE_ADDR;
  p.literal.addr = code_addr;

  gpy_literal_t n;
  n.type = TYPE_INTEGER;
  n.literal.integer = nargs;

  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = &s };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = &p };
  gpy_object_t a3 = { .T = TYPE_OBJECT_LIT, .o.literal = &n };
  gpy_object_t a4 = { .T = TYPE_NULL, .o.literal = NULL };

  args[0] = &a1;
  args[1] = &a2;
  args[2] = &a3;
  args[3] = &a4;

  gpy_typedef_t * def = __gpy_classmethod_type_node;
  retval = def->tp_new (def, args);
  gpy_free (args);

  gpy_assert (retval->T == TYPE_OBJECT_DECL);

  return retval;
}

gpy_object_t * gpy_rr_fold_call (gpy_object_t * decl, int nargs, ...)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_assert (decl->T == TYPE_OBJECT_DECL);
  gpy_typedef_t * type = decl->o.object_state->definition;

  /* + 1 for sentinal */
  gpy_object_t ** args = calloc (nargs + 1, sizeof (gpy_object_t *));
  int idx = 0;
  if (nargs > 0)
    {
      va_list ap;
      va_start (ap, nargs);
      for (idx = 0; idx < nargs; ++idx)
	{
	  args[idx] = va_arg (ap, gpy_object_t *);
	}
    }
  args[idx] = NULL;

  if (type->tp_call)
    {
      /* args length checks ... */
      int nparms = type->tp_nparms (decl);
      if (nargs == nparms)
	retval = type->tp_call (decl, args);
      else
	{
	  fatal ("call takes %i arguments (%i given)!\n", nparms, nargs);
	  retval = NULL;
	}
    }
  else
    fatal ("name is not callable!\n");
  gpy_free (args);

  return retval;
}

unsigned char * gpy_rr_eval_attrib_reference (gpy_object_t * base,
					      const char * attrib)
{
  unsigned char * retval = NULL;
  gpy_typedef_t * type = base->o.object_state->definition;

  struct gpy_object_attrib_t ** members = type->members_defintion;
  gpy_object_state_t * objs = base->o.object_state;

  int idx, offset = -1;
  for (idx = 0; members[idx] != NULL; ++idx)
    {
      struct gpy_object_attrib_t * it = members[idx];
      if (!strcmp (attrib, it->identifier))
	{
	  offset = it->offset;
	  unsigned char * state = (unsigned char *)objs->state;
	  retval = state + offset;
	  break;
	}
    }
  gpy_assert (retval);
  return retval;
}

gpy_object_t * gpy_rr_fold_integer (const int x)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_object_t ** args = (gpy_object_t **)
    gpy_calloc (2, sizeof(gpy_object_t*));

  gpy_literal_t i;
  i.type = TYPE_INTEGER;
  i.literal.integer = x;

  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = &i };
  gpy_object_t a2 = { .T = TYPE_NULL, .o.literal = NULL };

  args[0] = &a1;
  args[1] = &a2;

  gpy_typedef_t * Int_def = __gpy_integer_type_node;
  retval = Int_def->tp_new (Int_def, args);
  gpy_free(args);
  gpy_assert (retval->T == TYPE_OBJECT_STATE);

  return retval;
}

inline
void * gpy_rr_get_object_state (gpy_object_t * o)
{
  gpy_assert (o);
  return o->o.object_state->state;
}

/**
 * int fd: we could use bit masks to represent:
 *   stdout/stderr ...
 **/
void gpy_rr_eval_print (int fd, int count, ...)
{
  va_list vl; int idx;
  va_start (vl,count);

  gpy_object_t * it = NULL;
  for (idx = 0; idx<count; ++idx)
    {
      it = va_arg (vl, gpy_object_t *);
      struct gpy_typedef_t * definition = it->o.object_state->definition;

      switch (fd)
	{
	case 1:
	  definition->tp_print (it, stdout, false);
	  break;

	case 2:
	  definition->tp_print (it, stderr, false);
	  break;

	default:
	  fatal ("invalid print file-descriptor <%i>!\n", fd );
	  break;
	}
    }

  fprintf (stdout, "\n");
  va_end (vl);
}

inline
void gpy_rr_incr_ref_count (gpy_object_t * x1)
{
  gpy_assert (x1->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * x = x1->o.object_state;

  debug ("incrementing ref count on <%p>:<%i> to <%i>!\n",
	 (void*) x, x->ref_count, (x->ref_count + 1));
  x->ref_count++;
}

inline
void gpy_rr_decr_ref_count (gpy_object_t * x1)
{
  gpy_assert (x1->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * x = x1->o.object_state;

  debug ("decrementing ref count on <%p>:<%i> to <%i>!\n",
	 (void*) x, x->ref_count, (x->ref_count - 1));
  x->ref_count--;
}

bool gpy_rr_eval_boolean (gpy_object_t * x)
{
  bool retval = false;

  gpy_assert (x->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * state = x->o.object_state;

  struct gpy_typedef_t * def = state->definition;
  if (def->tp_eval_boolean)
    retval = def->tp_eval_boolean (x);

  return retval;
}

gpy_object_t * gpy_rr_eval_expression (gpy_object_t * x1,
				       gpy_object_t * y1,
				       unsigned int op)
{
  gpy_object_t * retval = NULL;

  gpy_assert (x1->T == TYPE_OBJECT_STATE);
  gpy_assert (y1->T == TYPE_OBJECT_STATE);
  gpy_object_state_t * x = x1->o.object_state;
  gpy_object_state_t * y = y1->o.object_state;

  struct gpy_typedef_t * def = x1->o.object_state->definition;
  struct gpy_number_prot_t * binops = (*def).binary_protocol;
  struct gpy_number_prot_t binops_l = (*binops);

  binary_op o = NULL;
  switch (op)
    {
    case 1:
      o = binops_l.n_add;
      break;

    case 2:
      o = binops_l.n_sub;
      break;

    case 4:
      o = binops_l.n_let;
      break;

    case 5:
      o = binops_l.n_get;
      break;

    case 6:
      o = binops_l.n_eee;
      break;

      /* FINISH .... */

    default:
      fatal("unhandled binary operation <%x>!\n", op );
      break;
    }

  if (o)
    retval = o (x1,y1);
  else
    fatal ("no binary protocol!\n");
  return retval;
}
