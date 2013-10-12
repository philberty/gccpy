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

#include "gpython.h"

static vec<gpydot,va_gc> * folds;
static vec<gpydot,va_gc> * optimized;
static gpy_hash_tab_t statics;
static int gpy_tmpCt = 0;

static void dot_pass_constFoldWhile (gpy_dot_tree_t *);
static void dot_pass_constFoldReturn (gpy_dot_tree_t *);
static void dot_pass_constFoldPrint (gpy_dot_tree_t *);
static void dot_pass_constFoldMethod (gpy_dot_tree_t *);
static void dot_pass_constFoldClass (gpy_dot_tree_t *);
static void dot_pass_constFoldCondit (gpy_dot_tree_t *);

static gpy_dot_tree_t * dot_pass_constFoldChain (gpy_dot_tree_t *);
static gpy_dot_tree_t * dot_pass_constFoldExpr (gpy_dot_tree_t *);

static gpy_dot_tree_t * dot_pass_constFoldSuite (gpy_dot_tree_t *);
static void dot_pass_constFoldToplevel (gpy_dot_tree_t *);
static gpy_dot_tree_t * dot_pass_doFold (gpy_dot_tree_t *);
static char * dot_pass_getTmpID (void);

#define BUFFER_SIZE  32
#define TMP_IDENT    "GNU_PY_TMP"

static const char * foldingTypes [] = {
  "integers",
  NULL,
};
#define FOLD_TBL_INT  foldingTypes [0]

struct iCons {
  int val;
  char * reference;
};

static
char * dot_pass_getTmpID (void)
{
  char * buf = (char *) xcalloc (BUFFER_SIZE, sizeof (char));
  memset (buf, 0, BUFFER_SIZE);

  snprintf (buf, BUFFER_SIZE, "%s.%i", TMP_IDENT, gpy_tmpCt);
  gpy_tmpCt++;
  return buf;
}

static
gpy_dot_tree_t * dot_pass_doFold (gpy_dot_tree_t * primitive)
{
  gcc_assert (DOT_TYPE (primitive) == D_PRIMITIVE);
  gcc_assert (DOT_lhs_T (primitive) == D_TD_COM);
  gpy_dot_tree_t * retval = NULL_DOT;
  char * ref = NULL;
  bool found = false;

  switch (DOT_lhs_TC (primitive)->T)
    {
    case D_T_INTEGER:
      {
	int ival =  DOT_lhs_TC (primitive)->o.integer;
	gpy_hashval_t h = gpy_dd_hash_string (FOLD_TBL_INT);
	gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (&statics, h);

	if (e)
	  {
	    gcc_assert (e->data != NULL);
	    gpy_vector_t * vec = (gpy_vector_t *) e->data;

	    size_t i;
	    for (i = 0 ; i < VEC_length (vec); ++i)
	      {
		struct iCons * icv = VEC_index (struct iCons *, vec, i);
		if (icv->val == ival)
		  {
		    ref = icv->reference;
		    found = true;
		    break;
		  }
	      }
	    // still havent found it...
	    if (!found)
	      {
		struct iCons * ic = (struct iCons *) xmalloc (sizeof (struct iCons));
		memset (ic, 0, sizeof (struct iCons));
		
		ic->val = ival;
		ic->reference = dot_pass_getTmpID ();
		ref = ic->reference;
		found = true;

		gpy_dot_tree_t * ref1 = dot_build_identifier (ref);
		gpy_dot_tree_t * fold = dot_build_decl2 (D_MODIFY_EXPR, ref1, primitive);
		vec_safe_push (folds, fold);

		gpy_vec_push (vec, ic);
	      }
	  }
	  else
	    fatal_error ("something stupidly wrong!\n");
      }
      break;

    default:
      retval = primitive;
      break;
    }

  if (found)
    {
      gpy_dot_tree_t * ref2 = dot_build_identifier (ref);
      retval = ref2;
    }

  return retval;
}

static
gpy_dot_tree_t * dot_pass_constFoldExpr (gpy_dot_tree_t * expr)
{
  gpy_dot_tree_t * retval = expr;

  switch (DOT_TYPE (expr))
    {
    case D_MODIFY_EXPR:
      DOT_rhs_TT (expr) = dot_pass_constFoldExpr (DOT_rhs_TT (expr));
      break;

    case D_ADD_EXPR:
    case D_MINUS_EXPR:
    case D_MULT_EXPR:
    case D_DIVD_EXPR:
    case D_LESS_EXPR:
    case D_GREATER_EXPR:
    case D_EQ_EQ_EXPR:
    case D_NOT_EQ_EXPR:
      {
	DOT_lhs_TT (expr) = dot_pass_constFoldExpr (DOT_lhs_TT (expr));
	DOT_rhs_TT (expr) = dot_pass_constFoldExpr (DOT_rhs_TT (expr));
      }
      break;

    case D_PRIMITIVE:
      retval = dot_pass_doFold (expr);
      break;

    case D_CALL_EXPR:
      if (DOT_rhs_TT (expr))
	DOT_rhs_TT (expr) = dot_pass_constFoldChain (DOT_rhs_TT (expr));
      break;

      /* ignore cases */
    case D_IDENTIFIER:
    case D_ATTRIB_REF:
      break;

    case D_SLICE:
      DOT_rhs_TT (expr) = dot_pass_constFoldExpr (DOT_rhs_TT (expr));
      break;

    case D_T_LIST:
      if (DOT_lhs_TT (expr))
	DOT_lhs_TT (expr) = dot_pass_constFoldChain (DOT_lhs_TT (expr));
      break;

    default:
      fatal_error ("const fold super error %i!\n", DOT_TYPE (expr));
      break;
    }
  return retval;
}

static
gpy_dot_tree_t * dot_pass_constFoldChain (gpy_dot_tree_t * decl)
{
  gpy_vector_t vec;
  memset (&vec, 0, sizeof (gpy_vector_t));

  gpy_dot_tree_t * curr;
  for (curr = decl; curr != NULL_DOT; curr = DOT_CHAIN (curr))
    {
      gpy_dot_tree_t * fold = dot_pass_constFoldExpr (curr);
      gpy_vec_push (&vec, fold);
    }

  gpy_dot_tree_t * head,  * tail;
  size_t i;
  for (i = 0; i < vec.length; ++i)
    {
      curr = (gpy_dot_tree_t *)
	gpy_vec_index_diag (&vec, i, __FILE__, __LINE__);

      if (i == 0)
	{
	  head = tail = curr;
	  DOT_CHAIN (head) = NULL_DOT;
	}
      else
	DOT_CHAIN (tail) = curr;
      tail = curr;
    }
  DOT_CHAIN (tail) = NULL_DOT;
  return head;
}

static
void dot_pass_constFoldWhile (gpy_dot_tree_t * decl)
{
  gpy_dot_tree_t * expr = DOT_lhs_TT (decl);
  gpy_dot_tree_t * folded = dot_pass_constFoldExpr (expr);
  DOT_lhs_TT (decl) = folded;

  gpy_dot_tree_t * suite = dot_pass_constFoldSuite (DOT_rhs_TT (decl));
  DOT_rhs_TT (decl) = suite;
}

static
void dot_pass_constFoldReturn (gpy_dot_tree_t * decl)
{
  DOT_lhs_TT (decl) = dot_pass_constFoldExpr (DOT_lhs_TT (decl));
}

static
void dot_pass_constFoldPrint (gpy_dot_tree_t * decl)
{
  DOT_lhs_TT (decl) = dot_pass_constFoldChain (DOT_lhs_TT (decl));
}

static
void dot_pass_constFoldCondit (gpy_dot_tree_t * decl)
{
  gpy_dot_tree_t * ifblock = DOT_FIELD (decl);
  gpy_dot_tree_t * elifblock = DOT_lhs_TT (decl);
  gpy_dot_tree_t * elseblock = DOT_rhs_TT (decl);

  DOT_lhs_TT (ifblock) = dot_pass_constFoldExpr (DOT_lhs_TT (ifblock));
  DOT_rhs_TT (ifblock) = dot_pass_constFoldSuite (DOT_rhs_TT (ifblock));

  if (elifblock != NULL_DOT)
    {
      gpy_dot_tree_t * ptr;
      for (ptr = elifblock; ptr != NULL_DOT; ptr = DOT_CHAIN (ptr))
	{
	  DOT_lhs_TT (ptr) = dot_pass_constFoldExpr (DOT_lhs_TT (ptr));
	  DOT_rhs_TT (ptr) = dot_pass_constFoldSuite (DOT_rhs_TT (ptr));
	}
    }

  if (elseblock != NULL_DOT)
    DOT_lhs_TT (elseblock) = dot_pass_constFoldSuite (DOT_lhs_TT (elseblock));
}

static
void dot_pass_constFoldMethod (gpy_dot_tree_t * decl)
{
  gpy_dot_tree_t * suite = dot_pass_constFoldSuite (DOT_rhs_TT (decl));
  DOT_rhs_TT (decl) = suite;
}

static
void dot_pass_constFoldClass (gpy_dot_tree_t * decl)
{
  DOT_lhs_TT (decl) = dot_pass_constFoldSuite (DOT_lhs_TT (decl));
}

static
gpy_dot_tree_t * dot_pass_constFoldSuite (gpy_dot_tree_t * decl)
{
  gpy_vector_t vec;
  memset (&vec, 0, sizeof (gpy_vector_t));

  gpy_dot_tree_t * curr;
  for (curr = decl; curr != NULL_DOT; curr = DOT_CHAIN (curr))
    {
      gpy_dot_tree_t * fold = curr;
      if (DOT_T_FIELD (curr) == D_D_EXPR)
	fold = dot_pass_constFoldExpr (curr);
      else
	switch (DOT_TYPE (curr))
	  {
	  case D_PRINT_STMT:
	    dot_pass_constFoldPrint (curr);
	    break;
	    
	  case D_KEY_RETURN:
	    dot_pass_constFoldReturn (curr);
	    break;
	    
	  case D_STRUCT_CONDITIONAL:
	    dot_pass_constFoldCondit (curr);
	    break;
	    
	  case D_STRUCT_WHILE:
	    dot_pass_constFoldWhile (curr);
	    break;

	  case D_STRUCT_METHOD:
	    dot_pass_constFoldMethod (curr);
	    break;

	  default:
	    break;
	  }
      gpy_vec_push (&vec, fold);
    }

  gpy_dot_tree_t * head, * tail;
  size_t i;
  for (i = 0; i < vec.length; ++i)
    {
      curr = (gpy_dot_tree_t *)
	gpy_vec_index_diag (&vec, i, __FILE__, __LINE__);

      if (i == 0)
	{
	  head = tail = curr;
	  DOT_CHAIN (head) = NULL_DOT;
	}
      else
	DOT_CHAIN (tail) = curr;
      tail = curr;
    }
  DOT_CHAIN (tail) = NULL_DOT;
  return head;
}

static
void dot_pass_constFoldToplevel (gpy_dot_tree_t * node)
{
  if (DOT_T_FIELD (node) == D_D_EXPR)
    {
      gpy_dot_tree_t * folded = dot_pass_constFoldExpr (node);
      vec_safe_push (optimized, folded);
    }
  else
    {
      gpy_dot_tree_t * fold = node;
      switch (DOT_TYPE (node))
	{
	case D_PRINT_STMT:
	  dot_pass_constFoldPrint (node);
	  break;

	case D_KEY_RETURN:
	  dot_pass_constFoldReturn (node);
	  break;

	case D_STRUCT_CONDITIONAL:
	  dot_pass_constFoldCondit (node);
	  break;

	case D_STRUCT_METHOD:
	  dot_pass_constFoldMethod (node);
	  break;

	case D_STRUCT_CLASS:
	  dot_pass_constFoldClass (node);
	  break;

	case D_STRUCT_WHILE:
	  dot_pass_constFoldWhile (node);
	  break;

	default:
	  break;
	}
      vec_safe_push (optimized, fold);
    }
}

/*
  This pass should pass over the IL at its most basic form
  stright from the parser and preform some constant folding
  as expressions are folded out and evaluated at runtime due
  to dynamic typing so we cant rely on gcc's constant folding
*/
vec<gpydot,va_gc> * dot_pass_const_fold (vec<gpydot,va_gc> * decls)
{
  vec<gpydot,va_gc> * retval = decls;
  if (GPY_OPT_optimize)
    {
      gpy_dd_hash_init_table (&statics);
      size_t i;
      for (i = 0; foldingTypes [i] != NULL; ++i)
	{
	  const char * id = foldingTypes [i];
	  gpy_hashval_t h = gpy_dd_hash_string (id);

	  gpy_vector_t * vec = (gpy_vector_t *) xmalloc (sizeof (gpy_vector_t));
	  memset (vec, 0, sizeof (gpy_vector_t));
	  gcc_assert (gpy_dd_hash_insert (h, vec, &statics) == NULL);
	}

      gpy_dot_tree_t * it = NULL_DOT;
      for (i = 0; decls->iterate (i, &it); ++i)
	dot_pass_constFoldToplevel (it);

      GPY_VEC_stmts_append (gpydot, folds, optimized);
      retval = folds;
      
      for (i = 0; foldingTypes [i] != NULL; ++i)
	{
	  const char * id = foldingTypes [i];
	  gpy_hashval_t h = gpy_dd_hash_string (id);
	  gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (&statics, h);

	  if (e)
	    if (e->data)
	      {
		gpy_vector_t * vec = (gpy_vector_t *) e->data;
		size_t y;
		for (y = 0; y < VEC_length (vec); ++y)
		  {
		    void * elem = VEC_index (void *, vec, y);
		    free (elem);
		  }
		free (vec);
	      }
	}
    }
  return retval;
}

