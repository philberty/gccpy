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

static vec<gpydot,va_gc> * optimized;
static int gpy_tmpCt = 0;

static void dot_pass_constFold (gpy_dot_tree_t *);
static void dot_pass_constFoldWhile (gpy_dot_tree_t *);

static gpy_dot_tree_t * dot_pass_constFoldExpr (gpy_dot_tree_t *);
static gpy_dot_tree_t * dot_pass_doFold (gpy_dot_tree_t *);
static char * dot_pass_getTmpID (void);

#define BUFFER_SIZE 15

static
char * dot_pass_getTmpID (void)
{
  char * buf = (char *) xcalloc (BUFFER_SIZE, sizeof (char));
  memset (buf, 0, BUFFER_SIZE);

  snprintf (buf, BUFFER_SIZE, "GCCPY_TMP.%i", gpy_tmpCt);
  gpy_tmpCt++;
  return buf;
}

static
gpy_dot_tree_t * dot_pass_doFold (gpy_dot_tree_t * primitive)
{
  gcc_assert (DOT_TYPE (primitive) == D_PRIMITIVE);

  char * fold_tmp = dot_pass_getTmpID ();
  gpy_dot_tree_t * ref1 = dot_build_identifier (fold_tmp);
  gpy_dot_tree_t * ref2 = dot_build_identifier (fold_tmp);

  gpy_dot_tree_t * fold = dot_build_decl2 (D_MODIFY_EXPR, ref1, primitive);
  vec_safe_push (optimized, fold);

  return ref2;
}

static
gpy_dot_tree_t * dot_pass_constFoldExpr (gpy_dot_tree_t * expr)
{
  bool done = false;
  while (!done)
    {
      switch (DOT_TYPE (expr))
	{
	case D_MODIFY_EXPR:
	  {
	    if (DOT_TYPE (DOT_rhs_TT (expr)) == D_PRIMITIVE)
	      DOT_rhs_TT (expr) = dot_pass_doFold (DOT_rhs_TT (expr));
	  }
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
	    if (DOT_TYPE (DOT_lhs_TT (expr)) == D_PRIMITIVE)
	      {
		debug ("shit2!\n");
		DOT_lhs_TT (expr) = dot_pass_doFold (DOT_lhs_TT (expr));
	      }

	    if (DOT_TYPE (DOT_rhs_TT (expr)) == D_PRIMITIVE) {
	      debug ("shit1\n");
	      DOT_rhs_TT (expr) = dot_pass_doFold (DOT_rhs_TT (expr));
	    }
	  }
	  break;

	default:
	  fatal_error ("const fold super error!\n");
	  break;
	}

      gpy_dot_tree_t * term = DOT_rhs_TT (expr);
      if (DOT_TYPE (term) == D_PRIMITIVE
	  || DOT_TYPE (term) == D_IDENTIFIER
	  || DOT_TYPE (term) == D_SLICE
	  || DOT_TYPE (term) == D_T_LIST
	  || DOT_TYPE (term) == D_ATTRIB_REF
	  || DOT_TYPE (term) == D_CALL_EXPR)
	done = true;
    }
  return expr;
}

static
void dot_pass_constFoldWhile (gpy_dot_tree_t * decl)
{
  gpy_dot_tree_t * expr = DOT_lhs_TT (decl);
  gpy_dot_tree_t * folded = dot_pass_constFoldExpr (expr);
  DOT_lhs_TT (decl) = folded;
  vec_safe_push (optimized, decl);
}

static
void dot_pass_constFold (gpy_dot_tree_t * node)
{
  if (DOT_T_FIELD (node) == D_D_EXPR)
    vec_safe_push (optimized, node);
  else
    {
      switch (DOT_TYPE (node))
	{
	case D_STRUCT_WHILE:
	  dot_pass_constFoldWhile (node);
	  break;

	default:
	  vec_safe_push (optimized, node);
	  break;
	}
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
      int i;
      gpy_dot_tree_t * it = NULL_DOT;
      for (i = 0; decls->iterate (i, &it); ++i)
	dot_pass_constFold (it);
      retval = optimized;
    }
  return retval;
}
