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

#include <gpython/gpython.h>

// Holds all runtime information to primitive types
gpy_object_t NULL_OBJECT_REF;
gpy_vector_t __GPY_GLOBL_PRIMITIVES;

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
#define __GPY_INIT_LEN                3
static int __GPY_GLOBL_modOffs      = 0;
static int __GPY_GLOBAL_STACK_LEN   = 0;
static gpy_hash_tab_t stack_table;

static bool __GPY_GLOBAL_RETURN;
gpy_object_t ** __GPY_MODULE_RR_STACK;
gpy_object_t ** __GPY_RR_STACK_PTR;

#define GPY_MODULE_STACK_RET_ADDR  (__GPY_MODULE_RR_STACK + 1)
#define GPY_MODULE_STACK_RET_VAL   (__GPY_MODULE_RR_STACK + 1)[0]

static
void gpy_rr_init_primitives (void)
{
  gpy_obj_func_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_integer_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_staticmethod_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_class_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_classmethod_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_list_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_module_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_string_mod_init (&__GPY_GLOBL_PRIMITIVES);
  gpy_obj_dict_mod_init (&__GPY_GLOBL_PRIMITIVES);

  /* builtins */
  gpy_builtin_sys_init ();
}

static
void gpy_rr_init_runtime_stack (void)
{
  __GPY_GLOBAL_RETURN = false;
  gpy_rr_init_primitives ();

  __GPY_MODULE_RR_STACK = (gpy_object_t **)
    gpy_calloc (__GPY_INIT_LEN,	sizeof (gpy_object_t *));

  __GPY_GLOBAL_STACK_LEN += __GPY_INIT_LEN;
  __GPY_MODULE_RR_STACK [0] = gpy_rr_fold_integer (__GPY_GLOBAL_STACK_LEN);
  __GPY_RR_STACK_PTR = __GPY_MODULE_RR_STACK + __GPY_INIT_LEN;
}

/* remember to update the stack pointer's and the stack size */
void gpy_rr_extend_runtime_stack (int nslots)
{
  // calculate the size of reallocation
  size_t size = sizeof (gpy_object_t *) * (__GPY_GLOBAL_STACK_LEN + nslots);
  __GPY_MODULE_RR_STACK = gpy_realloc (__GPY_MODULE_RR_STACK, size);

  __GPY_GLOBAL_STACK_LEN += nslots;
  __GPY_RR_STACK_PTR = __GPY_MODULE_RR_STACK + __GPY_INIT_LEN;
  //gpy_free (__GPY_MODULE_RR_STACK [0]);
  __GPY_MODULE_RR_STACK [0] = gpy_rr_fold_integer (__GPY_GLOBAL_STACK_LEN);
}

/* More of a helper function than trying to do this directly in GENERIC */
char ** gpy_rr_modAttribList (int n, ...)
{
  char ** retval = NULL;
  if (n > 0)
    {
      retval = (char **) gpy_calloc (n + 1, sizeof (char *));

      va_list ap;
      va_start (ap, n);

      int i;
      for (i = 0; i < n; ++i)
	{
	  char * y = va_arg (ap, char *);
	  retval[i] = strdup (y);
	}

      retval[i] = NULL;
      va_end (ap);
    }
  return retval;
}

/**
 * @slots is the length to which the stack needs extended
 * @stack_id is the id of the module which is being loaded
 * @elems is the _ordered_ list of attributes of the class
 **/
int gpy_rr_extendRRStack (int slots,
			  const char * stack_id,
			  char ** elems)
{
  int retOffs = -1;
  /* Make sure it doesn't already exist! */
  gpy_hashval_t h = gpy_dd_hash_string (stack_id);
  gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (&stack_table, h);

  if (e == NULL || e->data == NULL)
    {
      /* extend the stack and setup the stack pointer for the callee' */
      gpy_rr_extend_runtime_stack (slots);

      gpy_moduleInfo_t * info = (gpy_moduleInfo_t *)
	gpy_malloc (sizeof (gpy_moduleInfo_t));
      info->offset = __GPY_GLOBL_modOffs;
      info->length = slots;
      info->modID = strdup (stack_id);
      info->idents = elems;

      gpy_assert (!gpy_dd_hash_insert (h, (void *) info, &stack_table));
      retOffs = info->offset;

      // update the offset for next module load...
      __GPY_GLOBL_modOffs += slots;
    }
  else
    fatal ("Stack id <%s> already exists!\n", stack_id);

  return retOffs;
}

void gpy_rr_init_runtime (void)
{
  gpy_rr_init_runtime_stack ();
  memset (&NULL_OBJECT_REF, 0, sizeof (gpy_object_t));
  NULL_OBJECT_REF.T = TYPE_NULL;
}

void gpy_rr_cleanup_final (void)
{
  /*
    Cleanup the runtime stack and all other object data
    .....
   */
  return;
}

gpy_object_attrib_t * gpy_rr_fold_attribute (const char * identifier,
					     unsigned char * code_addr,
					     unsigned int offset, int nargs)
{
  gpy_object_attrib_t * attrib = gpy_malloc (sizeof (gpy_object_attrib_t));
  attrib->identifier = identifier;
  attrib->T = GPY_GCCPY;
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

gpy_object_t * gpy_rr_fold_encDict (int n, ...)
{
  gpy_object_t * retval = NULL;
  return retval;
}

gpy_object_t * gpy_rr_fold_encList (int n, ...)
{
  gpy_object_t * retval = NULL;
  va_list ap;
  va_start (ap, n);

  gpy_object_t ** elems = (gpy_object_t **)
    alloca ((n + 1) * sizeof (gpy_object_t *));

  int i;
  for (i = 0; i < n; ++i)
    {
      gpy_object_t * elem = va_arg (ap, gpy_object_t *);
      elems [i] = elem;
    }
  elems [i] = NULL;
  va_end (ap);

  /* + 2 for first argument the length of elements and for sentinal */
  gpy_object_t args[3];

  gpy_literal_t num;
  num.type = TYPE_INTEGER;
  num.literal.integer = n;

  gpy_literal_t elements;
  elements.type = TYPE_VEC;
  elements.literal.vec = elems;

  gpy_object_t a0 = { .T = TYPE_OBJECT_LIT, .o.literal = num };
  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = elements };
  args [0] = a0;
  args [1] = a1;
  args [2] = NULL_OBJECT_REF;

  gpy_typedef_t * def = __gpy_list_type_node;
  retval = def->tp_new (def, args);
  gpy_assert (retval->T == TYPE_OBJECT_STATE);

  return retval;
}

gpy_object_t * gpy_rr_fold_class_decl (gpy_object_attrib_t ** attribs,
				       int size, const char * identifier)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_object_t args[4];

  gpy_literal_t A;
  A.type = TYPE_ATTRIB_L;
  A.literal.attribs = attribs;

  gpy_literal_t i;
  i.type = TYPE_INTEGER;
  i.literal.integer = size;

  gpy_literal_t s;
  s.type = TYPE_STRING;
  s.literal.string = (char *) identifier;

  gpy_object_t a0 = { .T = TYPE_OBJECT_LIT, .o.literal = A };
  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = i };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = s };

  args[0] = a0;
  args[1] = a1;
  args[2] = a2;
  args[3] = NULL_OBJECT_REF;

  gpy_typedef_t * def = __gpy_class_type_node;
  retval = def->tp_new (def, args);
  gpy_assert (retval->T == TYPE_OBJECT_DECL);

  return retval;
}

gpy_object_t * gpy_rr_fold_staticmethod_decl (const char * identifier,
					      unsigned char * code_addr,
					      int nargs)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_object_t args[4];

  gpy_literal_t i;
  i.type = TYPE_STRING;
  i.literal.string = (char *)identifier;

  gpy_literal_t p;
  p.type = TYPE_ADDR;
  p.literal.addr = code_addr;

  gpy_literal_t n;
  n.type = TYPE_INTEGER;
  n.literal.integer = nargs;

  gpy_object_t a0 = { .T = TYPE_OBJECT_LIT, .o.literal = i };
  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = p };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = n };

  args[0] = a0;
  args[1] = a1;
  args[2] = a2;
  args[3] = NULL_OBJECT_REF;

  gpy_typedef_t * def = __gpy_staticmethod_type_node;
  retval = def->tp_new (def, args);
  gpy_assert (retval->T == TYPE_OBJECT_DECL);

  return retval;
}

gpy_object_t * gpy_rr_fold_classmethod_decl (const char * identifier,
					     unsigned char * code_addr,
					     int nargs)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_object_t args[4];

  gpy_literal_t s;
  s.type = TYPE_STRING;
  s.literal.string = (char *)identifier;

  gpy_literal_t p;
  p.type = TYPE_ADDR;
  p.literal.addr = code_addr;

  gpy_literal_t n;
  n.type = TYPE_INTEGER;
  n.literal.integer = nargs;

  gpy_object_t a0 = { .T = TYPE_OBJECT_LIT, .o.literal = s };
  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = p };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = n };

  args[0] = a0;
  args[1] = a1;
  args[2] = a2;
  args[3] = NULL_OBJECT_REF;

  gpy_typedef_t * def = __gpy_classmethod_type_node;
  retval = def->tp_new (def, args);
  gpy_assert (retval->T == TYPE_OBJECT_DECL);

  return retval;
}

gpy_object_t ** gpy_rr_getRefSlice (gpy_object_t * decl, gpy_object_t * slice)
{
  gpy_typedef_t * type = OBJECT_DEFINITION (decl);

  if (type->tp_slice)
    return type->tp_ref_slice (decl, slice);
  else
    fatal ("Object <%p> has no slice assign hook!\n", (void *) decl);

  return NULL;
}

gpy_object_t * gpy_rr_getSlice (gpy_object_t * decl, gpy_object_t * slice)
{
  gpy_typedef_t * type = OBJECT_DEFINITION (decl);

  if (type->tp_slice)
    return type->tp_slice (decl, slice);
  else
    fatal ("Object <%p> has no slice hook!\n", (void *) decl);

  return NULL;
}

void gpy_rr_foldBuiltinImport (gpy_object_t ** decl,
			       const unsigned builtin)
{
  switch (builtin)
    {
      /* import sys... */
    case 1:
      gpy_rr_foldImport (decl, "sys");
      break;

    default:
      fatal ("Unable to import builtin!\n");
      break;
    }
}

void gpy_rr_foldImport (gpy_object_t ** decl,
			const char * module)
{
  gpy_moduleInfo_t * mod = NULL;
  gpy_hashval_t h = gpy_dd_hash_string (module);
  gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (&stack_table, h);

  gpy_assert (e);
  gpy_assert (e->data);
  mod = (gpy_moduleInfo_t *) e->data;

  gpy_object_t args[5];

  gpy_literal_t A;
  A.type = TYPE_INTEGER;
  A.literal.integer = mod->offset;

  gpy_literal_t i;
  i.type = TYPE_INTEGER;
  i.literal.integer = mod->length;

  gpy_literal_t s;
  s.type = TYPE_STRING;
  s.literal.string = mod->modID;

  gpy_literal_t S;
  S.type = TYPE_STR_ARRY;
  S.literal.sarray = mod->idents;

  gpy_object_t a0 = { .T = TYPE_OBJECT_LIT, .o.literal = A };
  gpy_object_t a1 = { .T = TYPE_OBJECT_LIT, .o.literal = i };
  gpy_object_t a2 = { .T = TYPE_OBJECT_LIT, .o.literal = s };
  gpy_object_t a3 = { .T = TYPE_OBJECT_LIT, .o.literal = S };

  args[0] = a0;
  args[1] = a1;
  args[2] = a2;
  args[3] = a3;
  args[4] = NULL_OBJECT_REF;

  gpy_typedef_t * def = __gpy_module_type_node;
  *decl = def->tp_new (def, args);
  gpy_assert ((*decl)->T == TYPE_OBJECT_DECL);
}

gpy_object_t * gpy_rr_fold_call (gpy_object_t * decl, int nargs, ...)
{
  gpy_object_t * retval = NULL_OBJECT;

  gpy_assert (decl->T == TYPE_OBJECT_DECL);
  gpy_typedef_t * type = decl->o.object_state.definition;

  /* + 1 for sentinal */
  gpy_object_t ** args = (gpy_object_t **)
    alloca ((nargs + 1) * sizeof (gpy_object_t *));

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

      // if its not a class method we can drop the first argument
      // since we dont need the self reference, this will be NULL
      // on normal calls outside of attrib references on modules
      bool iscmeth = false;
      if (!strcmp ("classmethod", type->identifier) ||
	  !strcmp ("func", type->identifier))
	iscmeth = true;

      if (iscmeth)
	{
	  if (nargs == nparms)
	    retval = type->tp_call (decl, args);
	  else
	    {
	      fatal ("call takes %i arguments (%i given)!\n", nparms, nargs);
	      retval = NULL;
	    }
	}
      else
	{
	  nargs -= 1;
	  if (nargs == nparms)
	    retval = type->tp_call (decl, args + 1);
	  else
	    {
	      fatal ("call takes %i arguments (%i given)!\n", nparms, nargs);
	      retval = NULL;
	    }
	}
    }
  else
    fatal ("name is not callable!\n");

  if (__GPY_GLOBAL_RETURN)
    {
      retval = GPY_MODULE_STACK_RET_VAL;
      __GPY_GLOBAL_RETURN = false;
    }

  return retval;
}

gpy_object_t ** gpy_rr_eval_attrib_reference (gpy_object_t * base,
					      const char * attrib)
{
  gpy_object_t ** retval = NULL;
  gpy_typedef_t * type = OBJECT_DEFINITION (base);
  struct gpy_object_attrib_t ** members = type->members_defintion;
  gpy_object_state_t objs = OBJECT_STATE (base);

  if (members)
    {
      int idx, offset = -1;
      for (idx = 0; members[idx] != NULL; ++idx)
	{
	  struct gpy_object_attrib_t * it = members[idx];
	  if (!strcmp (attrib, it->identifier))
	    {
	      if (it->T == GPY_GCCPY)
		{
		  /* when part of the type we can access the instance from the state */
		  offset = it->offset;
		  unsigned char * state = (unsigned char *)objs.state;
		  retval = (gpy_object_t **) (state + offset);
		}
	      else if (it->T == GPY_MOD)
		{
		  /* when part of the type we can access the instance from the state */
		  offset = it->offset;
		  unsigned char * state = (unsigned char *)objs.state;
		  unsigned char * sref = state + offset;

		  void ** ref = (void **) sref;
		  retval = (gpy_object_t **) *ref;
		}
	      else
		{
		  // this is probably an internal C attribute to an object.
		  gpy_assert (it->T = GPY_CATTR);
		  retval = (gpy_object_t **) &(it->addr);
		}
	      break;
	    }
	}
    }
  if (!retval)
    fatal ("object has no attribute <%s>\n", attrib);

  gpy_assert (retval);
  return retval;
}

gpy_object_t * gpy_rr_fold_string (const char * string)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_t args[2];

  gpy_literal_t s;
  s.type = TYPE_STRING;
  s.literal.string = (char *) string;

  gpy_object_t lit = { .T = TYPE_OBJECT_LIT, .o.literal = s };
  args [0] = lit;
  args [1] = NULL_OBJECT_REF;

  gpy_typedef_t * Str_def = __gpy_string_type_node;
  retval = Str_def->tp_new (Str_def, args);
  gpy_assert (retval->T == TYPE_OBJECT_STATE);

  return retval;
}

gpy_object_t * gpy_rr_fold_integer (const int x)
{
  gpy_object_t * retval = NULL_OBJECT;
  gpy_object_t args[2];

  gpy_literal_t i;
  i.type = TYPE_INTEGER;
  i.literal.integer = x;

  gpy_object_t myobj = { .T = TYPE_OBJECT_LIT, .o.literal = i };
  args [0] = myobj;
  args [1] = NULL_OBJECT_REF;

  gpy_typedef_t * Int_def = __gpy_integer_type_node;
  retval = Int_def->tp_new (Int_def, args);
  gpy_assert (retval->T == TYPE_OBJECT_STATE);

  return retval;
}

void gpy_rr_eval_return (gpy_object_t * o)
{
  gpy_object_t ** addr = GPY_MODULE_STACK_RET_ADDR;
  *addr = o;
  __GPY_GLOBAL_RETURN = true;
}

/**
 * int fd: we could use bit masks to represent:
 *   stdout/stderr ...
 **/
void gpy_rr_eval_print (int fd, int count, ...)
{
  va_list vl;
  int idx;
  va_start (vl,count);

  gpy_object_t * it = NULL;
  for (idx = 0; idx < count; ++idx)
    {
      it = va_arg (vl, gpy_object_t *);
      struct gpy_typedef_t * definition = it->o.object_state.definition;

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

bool gpy_rr_eval_boolean (gpy_object_t * x)
{
  bool retval = false;

  gpy_assert (x->T == TYPE_OBJECT_STATE);
  gpy_object_state_t state = x->o.object_state;

  struct gpy_typedef_t * def = state.definition;
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

  struct gpy_typedef_t * def = x1->o.object_state.definition;
  struct gpy_number_prot_t * binops = def->binary_protocol;
  struct gpy_number_prot_t binops_l = *binops;

  binary_op o = NULL;
  switch (op)
    {
    case 1:
      o = binops_l.n_add;
      break;

    case 2:
      o = binops_l.n_sub;
      break;

    case 3:
      o = binops_l.n_div;
      break;

    case 4:
      o = binops_l.n_mul;
      break;

    case 5:
      o = binops_l.n_pow;
      break;

    case 6:
      o = binops_l.n_let;
      break;

    case 8:
      o = binops_l.n_get;
      break;

    case 10:
      o = binops_l.n_eee;
      break;

    case 11:
      o = binops_l.n_nee;
      break;

    case 12:
      o = binops_l.n_orr;
      break;

    case 13:
      o = binops_l.n_and;
      break;

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
