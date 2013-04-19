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

static tree dot_pass_types_FinalizeType (gpy_hash_tab_t *, const char *);
static tree dot_pass_types_GenClassType (gpy_dot_tree_t *);
static void dot_pass_types_ToplevelGenStmt (gpy_dot_tree_t *, gpy_hash_tab_t *);
static void dot_pass_types_ClassMethodSuite (gpy_dot_tree_t *, gpy_hash_tab_t *);

/* Currently we are going to ignore nested methods and classes within existing
   suites to keep things simple for now errors will be thrown from the first
   pass in sanity checking.

   We need to worry about any nested suites because there could possibly be
   another class definition and we still need to generate its type here for
   the next pass so we can calculate the attribute offsets

----------------------------------------------

  Process class method attribute's their suites for more decls to be part of
  the type for example:

  class foo:
    x = 1
    def __init__ (self):
      print x

  x = foo ()
  x.x = bla

  works very similar to at least with scoping and address access with scoping as:

  class foo:
    def __init__ (self):
      self.x = 1
      print x

  x = foo ()
  x.x = bla

  you do not need to declare attributes in the field of a class such that these
  attribs must be part of the type we must process the suites for references to
  self.<> to see if there is any such assignments to make sure the type is
  generated correctly

  This function iterates over any compound statements suite for any possible
  self.var decls to be part of the type we ignore any nested classes we come
  across within these
*/

static
tree dot_pass_types_FinalizeType (gpy_hash_tab_t * module,
				  const char * identifier)
{
  tree retval = NULL_TREE;
  if (module->length > 0)
    {
      tree field = NULL_TREE, last_field = NULL_TREE;
      tree module_struct = make_node (RECORD_TYPE);

      int i, y = 0;
      gpy_hash_entry_t * array = module->array;
      for (i = 0; i < module->size; ++i)
	{
	  if (array[i].data)
	    {
	      const char * dot_ident;
	      gpy_dot_tree_t * d = (gpy_dot_tree_t *) array[i].data;
	      if (d)
		{
		  switch (DOT_TYPE (d))
		    {
		    case D_STRUCT_CLASS:
		      dot_ident = DOT_IDENTIFIER_POINTER (DOT_FIELD (d));
		      break;

		    case D_STRUCT_METHOD:
		      dot_ident = DOT_IDENTIFIER_POINTER (DOT_FIELD (d));
		      break;

		    default:
		      dot_ident = DOT_IDENTIFIER_POINTER (d);
		      break;
		    }
		  field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
				      get_identifier (dot_ident),
				      gpy_object_type_ptr);
		  DECL_CONTEXT (field) = module_struct;
		  if (y == 0)
		    TYPE_FIELDS (module_struct) = field;
		  else
		    DECL_CHAIN (last_field) = field;
		  last_field = field;
		  y++;
		}
	    }
	}
      layout_type (module_struct);
      tree name = get_identifier (identifier);
      tree type_decl = build_decl (BUILTINS_LOCATION, TYPE_DECL,
				   name, module_struct);
      DECL_ARTIFICIAL (type_decl) = 1;
      TYPE_NAME (module_struct) = name;
      gpy_preserve_from_gc (type_decl);
      rest_of_decl_compilation (type_decl, 1, 0);

      retval = module_struct;
    }
  return retval;
}

static
void dot_pass_types_ClassMethodSuite (gpy_dot_tree_t * suite,
				      gpy_hash_tab_t * module)
{
  gpy_dot_tree_t * dot = suite;
  do {
    switch (DOT_TYPE (dot))
      {
      default:
	{
	  if (DOT_T_FIELD (dot) == D_D_EXPR)
	    {
	      gpy_dot_tree_t * itx = dot;
	      if (DOT_TYPE(itx) == D_MODIFY_EXPR)
		{
		  gcc_assert ((DOT_lhs_T(itx) == D_TD_DOT)
			      && (DOT_rhs_T(itx) == D_TD_DOT)
			      );
		  gpy_dot_tree_t * target = DOT_lhs_TT (itx);
		  switch (DOT_TYPE (target))
		    {
		    case D_ATTRIB_REF:
		      {
			gpy_dot_tree_t * root = target->opa.t;
			if (DOT_TYPE (root) == D_IDENTIFIER)
			  {
			    gpy_dot_tree_t * attrib = target->opb.t;
			    if (DOT_TYPE (attrib) == D_IDENTIFIER)
			      {
				const char * root_ident = DOT_IDENTIFIER_POINTER (root);
				if (!strcmp ("self", root_ident))
				  {
				    gpy_hashval_t h = gpy_dd_hash_string (DOT_IDENTIFIER_POINTER (attrib));
				    gpy_dd_hash_insert (h, attrib, module);
				  }
			      }
			  }
		      }
		      break;

		    default:
		      break;
		    }
		}
	    }
	}
	break;
      }
  } while ((dot = DOT_CHAIN (dot)));
}

static
tree dot_pass_types_GenClassType (gpy_dot_tree_t * node)
{
  gpy_dot_tree_t * suite = DOT_lhs_TT (node);
  gpy_dot_tree_t * dot;

  gpy_hash_tab_t module;
  gpy_dd_hash_init_table (&module);

  for (dot = suite; dot != NULL_DOT; dot = DOT_CHAIN (dot))
    {
      if (DOT_T_FIELD (dot) == D_D_EXPR)
	dot_pass_types_ToplevelGenStmt (dot, &module);
      else
	{
	  switch (DOT_TYPE (dot))
	    {
	    case D_STRUCT_METHOD:
	      {
		const char * cid = DOT_IDENTIFIER_POINTER (DOT_FIELD (dot));
		void ** e = gpy_dd_hash_insert (gpy_dd_hash_string (cid),
						dot, &module);
		gcc_assert (!e);
		dot_pass_types_ClassMethodSuite (DOT_rhs_TT (dot), &module);
	      }
	      break;

	    default:
	      break;
	    }
	}
    }
  /* Field initilizer function to type! */
  gcc_assert (!gpy_dd_hash_insert (gpy_dd_hash_string ("__field_init__"),
				   dot_build_identifier ("__field_init__"),
				   &module));
  tree retval = dot_pass_types_FinalizeType (&module,
					     dot_pass_concat (GPY_current_module_name,
							      DOT_IDENTIFIER_POINTER (DOT_FIELD (node))));
  free (module.array);
  return retval;
}

static
void dot_pass_types_ToplevelGenStmt (gpy_dot_tree_t * node,
				     gpy_hash_tab_t * module)
{
  if (DOT_T_FIELD (node) == D_D_EXPR)
    {
      if (DOT_TYPE (node) == D_MODIFY_EXPR)
	{
	  gcc_assert ((DOT_lhs_T (node) == D_TD_DOT)
		      && (DOT_rhs_T (node) == D_TD_DOT)
		      );
	  gpy_dot_tree_t * target = DOT_lhs_TT (node);
	  // remember to handle target lists here with DOT_CHAIN
	  do
	    {
	      switch (DOT_TYPE (target))
		{
		case D_IDENTIFIER:
		  {
		    gpy_hashval_t h = gpy_dd_hash_string (DOT_IDENTIFIER_POINTER (target));
		    gpy_dd_hash_insert (h, target, module);
		  }
		  break;

		default:
		  break;
		}
	    } while ((target = DOT_CHAIN (target)));
	}
      /* need to go through compound_stmts like conditionals and looks to
	 find any possible nested class's to generate their types
      */
    }
}

vec<tree, va_gc> * dot_pass_GenTypes (vec<gpydot,va_gc> * decls)
{
  vec<tree,va_gc> * retval;
  vec_alloc (retval, 0);

  gpy_hash_tab_t main_module;
  gpy_dd_hash_init_table (&main_module);

  int idx;
  gpy_dot_tree_t * idtx = NULL_DOT;
  for (idx = 0; vec_safe_iterate (decls, idx, &idtx); ++idx)
    {
      if (DOT_TYPE (idtx) == D_STRUCT_CLASS)
	{
	  tree module = dot_pass_types_GenClassType (idtx);
	  gcc_assert (module);
	  vec_safe_push (retval, module);

	  const char * mid = IDENTIFIER_POINTER (TYPE_NAME (module));
	  void ** e = gpy_dd_hash_insert (gpy_dd_hash_string (mid),
					  idtx, &main_module);
	  gcc_assert (!e);
	}
      else if (DOT_TYPE (idtx) == D_STRUCT_METHOD)
	{
	  const char * mid = DOT_IDENTIFIER_POINTER (DOT_FIELD (idtx));
	  void ** e = gpy_dd_hash_insert (gpy_dd_hash_string (mid),
					  idtx, &main_module);
	  gcc_assert (!e);
	  /* need to recursively go through the suite to find possible
	     nested classes to generate those types */
	}
      else
	dot_pass_types_ToplevelGenStmt (idtx, &main_module);
    }

  /* NEED to add in the __main_start__.... */
  char * module_entry = dot_pass_concat (GPY_current_module_name, "__main_start__");
  gcc_assert (!gpy_dd_hash_insert (gpy_dd_hash_string (module_entry),
				   dot_build_identifier (module_entry),
				   &main_module)
	      );
  vec_safe_push (retval, dot_pass_types_FinalizeType (&main_module,
						      GPY_current_module_name));
  free (main_module.array);
  return retval;
}
