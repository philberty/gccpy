/* This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>. */

#include "gpython.h"

gpy_dot_tree_t * dot_build_class_decl (gpy_dot_tree_t * ident,
				       gpy_dot_tree_t  * suite)
{
  gpy_dot_tree_t * decl = DOT_alloc;

  DOT_TYPE(decl) = D_STRUCT_CLASS;
  DOT_T_FIELD(decl) = D_TD_NULL;
  DOT_FIELD(decl) = ident;

  decl->opaT = D_TD_DOT;
  decl->opa.t = suite;
  decl->opbT = D_TD_NULL;

  DOT_CHAIN(decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_func_decl (gpy_dot_tree_t * ident,
				      gpy_dot_tree_t * parms,
				      gpy_dot_tree_t * suite)
{
  gpy_dot_tree_t * decl = DOT_alloc;

  DOT_TYPE(decl) = D_STRUCT_METHOD;
  DOT_T_FIELD(decl) = D_TD_NULL;
  DOT_FIELD(decl) = ident;

  decl->opaT = D_TD_DOT;
  decl->opa.t = parms;
  decl->opbT = D_TD_DOT;
  decl->opb.t = suite;

  DOT_CHAIN(decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_conditional_struct (gpy_dot_tree_t * ifblock,
					       gpy_dot_tree_t * elifblock,
					       gpy_dot_tree_t * elseblock)
{
  gpy_dot_tree_t * decl = DOT_alloc;

  DOT_TYPE(decl) = D_STRUCT_CONDITIONAL;
  DOT_T_FIELD(decl) = D_TD_NULL;
  DOT_FIELD(decl) = ifblock;

  decl->opaT = D_TD_DOT;
  decl->opa.t = elifblock;
  decl->opbT = D_TD_DOT;
  decl->opb.t = elseblock;

  DOT_CHAIN(decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_decl1 (opcode_t o, gpy_dot_tree_t * t1)
{
  gpy_dot_tree_t * decl = DOT_alloc;

  DOT_TYPE(decl) = o;
  DOT_T_FIELD(decl) = D_TD_NULL;
  DOT_FIELD(decl) = NULL_DOT;

  decl->opaT = D_TD_DOT;
  decl->opa.t = t1;
  decl->opbT = D_TD_NULL;

  DOT_CHAIN(decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_decl2 (opcode_t o,
				  gpy_dot_tree_t * t1,
				  gpy_dot_tree_t * t2)
{
  gpy_dot_tree_t * decl = DOT_alloc;

  DOT_TYPE (decl) = o;
  if ((o == D_MODIFY_EXPR)
      || (o == D_ADD_EXPR)
      || (o == D_MINUS_EXPR)
      || (o == D_MULT_EXPR)
      || (o == D_DIVD_EXPR)
      || (o == D_CALL_EXPR)
      || (o == D_EQ_EQ_EXPR)
      || (o == D_LESS_EXPR)
      || (o == D_LESS_EQ_EXPR)
      || (o == D_GREATER_EXPR)
      || (o == D_GREATER_EQ_EXPR)
      )
    DOT_T_FIELD(decl) = D_D_EXPR;
  else
    DOT_T_FIELD(decl) = D_TD_NULL;

  DOT_FIELD (decl) = NULL_DOT;

  decl->opaT = D_TD_DOT;
  decl->opa.t = t1;
  decl->opbT = D_TD_DOT;
  decl->opb.t = t2;

  DOT_CHAIN(decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_integer (int i)
{
  gpy_dot_tree_t * decl = DOT_alloc;
  DOT_TYPE(decl) = D_PRIMITIVE;

  // later on have this field point to a function
  // which returns the GENERIC code for folding the
  // primtive so when it comes to folding primitives we can just
  // use the func pointer to find the function we need
  // instead of the current switch case
  DOT_FIELD(decl) = NULL_DOT;
  DOT_T_FIELD(decl) = D_TD_NULL;

  decl->opaT = D_TD_COM;
  decl->opa.tc = DOT_CM_alloc;

  decl->opa.tc->T = D_T_INTEGER;
  decl->opa.tc->o.integer = i;

  decl->opbT = D_TD_NULL;
  DOT_CHAIN(decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_string (char * s)
{
  gpy_dot_tree_t * decl = DOT_alloc;
  DOT_TYPE (decl) = D_PRIMITIVE;

  DOT_FIELD (decl) = NULL_DOT;
  DOT_T_FIELD (decl) = D_TD_NULL;

  decl->opaT = D_TD_COM;
  decl->opa.tc = DOT_CM_alloc;

  decl->opa.tc->T = D_T_STRING;
  decl->opa.tc->o.string = xstrdup (s);

  decl->opbT = D_TD_NULL;
  DOT_CHAIN (decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_identifier (const char * s)
{
  gpy_dot_tree_t * decl = DOT_alloc;

  DOT_TYPE(decl) = D_IDENTIFIER;
  DOT_FIELD(decl) = NULL_DOT;
  DOT_T_FIELD(decl) = D_TD_NULL;

  decl->opaT = D_TD_COM;
  decl->opa.tc = DOT_CM_alloc;
  decl->opa.tc->T = D_T_STRING;
  decl->opa.tc->o.string = s;

  decl->opbT = D_TD_NULL;

  DOT_CHAIN(decl) = NULL_DOT;

  return decl;
}

gpy_dot_tree_t * dot_build_for (gpy_dot_tree_t * id,
				gpy_dot_tree_t * expr,
				gpy_dot_tree_t * suite)
{
  gpy_dot_tree_t * decl = DOT_alloc;
  memset (decl, 0, sizeof(gpy_dot_tree_t));

  DOT_TYPE (decl) = D_STRUCT_FOR;
  DOT_FIELD (decl) = id;
  DOT_T_FIELD (decl) = D_TD_DOT;

  decl->opaT = D_TD_DOT;
  decl->opa.t = expr;
  decl->opbT = D_TD_DOT;
  decl->opb.t = suite;

  DOT_CHAIN (decl) = NULL_DOT;

  return decl;
}
