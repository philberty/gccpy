
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

/*
   This file was generated via autogen @see gpy-runtime.{def/tpl}
     - To regenerate with new definitions $ autogen gpy-runtime.def
 */


#include <gpython.h>

vec<tree,va_gc> * gpy_builtin_types_vec;
static tree gpy_build_py_object_type (void);
static tree gpy_build_py_vector_type (void);


tree GPY_RR_extendRRStack (tree size, tree id, tree elems)
{
  tree fntype = build_function_type_list (sizetype,
	                 integer_type_node,
			 gpy_const_char_ptr,
			 ptr_type_node,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_extendRRStack"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 3, size, id, elems);
}

tree GPY_RR_fold_string (tree decl)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
			 gpy_const_char_ptr,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_string"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 1, decl);
}

tree GPY_RR_foldImport (tree decl, tree entry)
{
  tree fntype = build_function_type_list (void_type_node,
	                 gpy_object_type_ptr_ptr,
			 gpy_const_char_ptr,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_foldImport"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 2, decl, entry);
}

tree GPY_RR_foldBuiltinImport (tree decl, tree entry)
{
  tree fntype = build_function_type_list (void_type_node,
	                 gpy_object_type_ptr_ptr,
			 unsigned_type_node,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_foldBuiltinImport"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 2, decl, entry);
}

tree GPY_RR_makeSlice (tree object, tree slice)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
	                 gpy_object_type_ptr,
			 gpy_object_type_ptr,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_getSlice"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 2, object, slice);
}

tree GPY_RR_makeRefSlice (tree object, tree slice)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr_ptr,
	                 gpy_object_type_ptr,
			 gpy_object_type_ptr,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_getRefSlice"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 2, object, slice);
}

tree GPY_RR_fold_attrib (tree ident, tree addr, tree offset, tree nargs)
{
  tree fntype = build_function_type_list (gpy_attrib_type_ptr,
                         gpy_const_char_ptr,
                         gpy_unsigned_char_ptr,
                         integer_type_node,
			 integer_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_attribute"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 4, ident, addr, offset, nargs);
}

tree GPY_RR_fold_attrib_list (vec<tree,va_gc> * attribs)
{
  tree fntype = build_function_type_list (gpy_attrib_type_ptr_ptr,
			 integer_type_node,
			 va_list_type_node,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_attrib_list"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr_loc_vec (BUILTINS_LOCATION, fndecl, attribs);
}

tree GPY_RR_fold_class_decl (tree attrib_list, tree size, tree identifier)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
	       	         gpy_attrib_type_ptr_ptr,
			 integer_type_node,
                         gpy_const_char_ptr,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_class_decl"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 3, attrib_list, size, identifier);
}

tree GPY_RR_fold_func_decl (tree identifier, tree func, tree nargs)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
                         gpy_const_char_ptr,
			 ptr_type_node,
			 integer_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_staticmethod_decl"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 3, identifier, build_fold_addr_expr (func), nargs);
}

tree GPY_RR_fold_classmethod_decl (tree identifier, tree func, tree nargs)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
                         gpy_const_char_ptr,
			 ptr_type_node,
			 integer_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_classmethod_decl"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 3, identifier, build_fold_addr_expr (func), nargs);
}

tree GPY_RR_fold_integer (tree integer)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
		         integer_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_integer"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 1, integer);
}

tree GPY_RR_fold_encList (vec<tree,va_gc> * elms)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
		         integer_type_node,
			 va_list_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_encList"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr_loc_vec (BUILTINS_LOCATION, fndecl, elms);
}

tree GPY_RR_incr_ref_count (tree obj)
{
  tree fntype = build_function_type_list (void_type_node,
		         gpy_object_type_ptr,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_incr_ref_count"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 1, obj);
}

tree GPY_RR_decr_ref_count (tree obj)
{
  tree fntype = build_function_type_list (void_type_node,
		         gpy_object_type_ptr,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_decr_ref_count"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 1, obj);
}

tree GPY_RR_eval_print (vec<tree,va_gc> * arguments)
{
  tree fntype = build_function_type_list (void_type_node,
			 integer_type_node,
			 integer_type_node,
			 va_list_type_node,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_eval_print"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr_loc_vec (BUILTINS_LOCATION, fndecl, arguments);
}

tree GPY_RR_eval_return (tree val)
{
  tree fntype = build_function_type_list (void_type_node,
	                 gpy_object_type_ptr,
			 NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_eval_return"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 1, val);
}

tree GPY_RR_eval_expression (tree x, tree y, tree op)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
	       	         gpy_object_type_ptr,
			 integer_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_eval_expression"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 3, x, y, op);
}

tree GPY_RR_fold_attrib_ref (tree base, tree attrib)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr_ptr,
	       	      	 gpy_object_type_ptr,
			 gpy_const_char_ptr,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_eval_attrib_reference"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 2, base, attrib);
}

tree GPY_RR_fold_call (vec<tree,va_gc> * arguments)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
	       	      	 integer_type_node,
			 va_list_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_call"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr_loc_vec (BUILTINS_LOCATION, fndecl, arguments);
}

tree GPY_RR_fold_argument (tree args, tree offset)
{
  tree fntype = build_function_type_list (gpy_object_type_ptr,
	       	      	 gpy_object_type_ptr_ptr,
			 integer_type_node,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_fold_argument"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 2, args, offset);
}

tree GPY_RR_eval_boolean (tree res)
{
  tree fntype = build_function_type_list (boolean_type_node,
	       	      	 gpy_object_type_ptr,
                         NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_eval_boolean"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  return build_call_expr (fndecl, 1, res);
}


static
tree gpy_build_py_vector_type (void)
{
  tree vec_struct_type = make_node (RECORD_TYPE);

  tree field = build_decl (BUILTINS_LOCATION, FIELD_DECL, get_identifier ("vector"),
			   build_pointer_type (ptr_type_node));
  DECL_CONTEXT (field) = vec_struct_type;
  TYPE_FIELDS (vec_struct_type) = field;
  tree last_field = field;

  field = build_decl (BUILTINS_LOCATION, FIELD_DECL, get_identifier ("size"),
		      integer_type_node);
  DECL_CONTEXT (field) = vec_struct_type;
  DECL_CHAIN (last_field) = field;
  last_field = field;

  field = build_decl (BUILTINS_LOCATION, FIELD_DECL, get_identifier ("length"),
		      integer_type_node);
  DECL_CONTEXT (field) = vec_struct_type;
  DECL_CHAIN (last_field) = field;

  layout_type (vec_struct_type);
  tree ident = get_identifier ("gpy_vector_t");
  tree type_decl = build_decl (BUILTINS_LOCATION, TYPE_DECL, ident,
			       vec_struct_type);
  DECL_ARTIFICIAL (type_decl) = 1;
  TYPE_NAME (vec_struct_type) = ident;

  gpy_preserve_from_gc (type_decl);
  rest_of_decl_compilation (type_decl, 1, 0);

  return vec_struct_type;
}

static
tree gpy_build_py_attrib_type (void)
{
  tree attrib_struct_type = make_node (RECORD_TYPE);

  tree field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			   get_identifier ("identifier"),
			   gpy_const_char_ptr);
  DECL_CONTEXT (field) = attrib_struct_type;
  TYPE_FIELDS (attrib_struct_type) = field;

  tree last_field = field;

  field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
		      get_identifier ("offset"),
		      unsigned_type_node);
  DECL_CONTEXT (field) = attrib_struct_type;
  DECL_CHAIN (last_field) = field;
  last_field = field;

  field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
		      get_identifier ("addr"),
		      gpy_object_type_ptr);
  DECL_CONTEXT (field) = attrib_struct_type;
  DECL_CHAIN (last_field) = field;

  layout_type (attrib_struct_type);
  tree ident = get_identifier ("gpy_object_attrib_t");
  tree type_decl = build_decl (BUILTINS_LOCATION, TYPE_DECL, ident,
			       attrib_struct_type);
  DECL_ARTIFICIAL (type_decl) = 1;
  TYPE_NAME (attrib_struct_type) = ident;

  gpy_preserve_from_gc (type_decl);
  rest_of_decl_compilation (type_decl, 1, 0);

  return attrib_struct_type;
}

static
tree gpy_build_py_object_type (void)
{
  tree object_state_struct_Type = make_node (RECORD_TYPE);

  tree name = get_identifier("identifier");
  tree field = build_decl(BUILTINS_LOCATION, FIELD_DECL, name,
			  build_pointer_type(char_type_node));
  DECL_CONTEXT(field) = object_state_struct_Type;
  TYPE_FIELDS(object_state_struct_Type) = field;
  tree last_field = field;

  name = get_identifier("ref_count");
  field = build_decl(BUILTINS_LOCATION, FIELD_DECL, name, integer_type_node);
  DECL_CONTEXT(field) = object_state_struct_Type;
  DECL_CHAIN(last_field) = field;
  last_field = field;

  name = get_identifier("state");
  field = build_decl(BUILTINS_LOCATION, FIELD_DECL, name,
		     ptr_type_node);
  DECL_CONTEXT(field) = object_state_struct_Type;
  DECL_CHAIN(last_field) = field;
  last_field = field;

  name = get_identifier("definition");
  field = build_decl(BUILTINS_LOCATION, FIELD_DECL, name,
		     ptr_type_node);
  DECL_CONTEXT(field) = object_state_struct_Type;
  DECL_CHAIN(last_field) = field;
  last_field = field;

  layout_type(object_state_struct_Type);

  // Give the struct a name for better debugging info.
  name = get_identifier ("gpy_object_state_t");
  tree object_state_type_decl = build_decl (BUILTINS_LOCATION, TYPE_DECL, name,
					    object_state_struct_Type);
  DECL_ARTIFICIAL(object_state_type_decl) = 1;
  TYPE_NAME(object_state_struct_Type) = object_state_type_decl;
  gpy_preserve_from_gc(object_state_type_decl);
  rest_of_decl_compilation(object_state_type_decl, 1, 0);

  tree object_state_ptr_type = build_pointer_type (object_state_struct_Type);
  gpy_preserve_from_gc (object_state_ptr_type);

  tree union_type__ = make_node (UNION_TYPE);

  name = get_identifier ("object_state");
  field = build_decl (BUILTINS_LOCATION, FIELD_DECL, name,
		      object_state_ptr_type);
  DECL_CONTEXT(field) = union_type__;
  DECL_CHAIN(last_field) = field;
  last_field = field;

  name = get_identifier ("literal");
  field = build_decl (BUILTINS_LOCATION, FIELD_DECL, name,
		      ptr_type_node);
  DECL_CONTEXT(field) = union_type__;
  DECL_CHAIN(last_field) = field;
  last_field = field;

  layout_type (union_type__);

  name = get_identifier("o");
  tree union_type_decl = build_decl(BUILTINS_LOCATION, TYPE_DECL, name,
				    union_type__);
  DECL_ARTIFICIAL(union_type_decl) = 1;
  TYPE_NAME(union_type__) = union_type_decl;
  gpy_preserve_from_gc(union_type_decl);
  rest_of_decl_compilation(union_type_decl, 1, 0);

  tree gpy_object_struct_Type = make_node (RECORD_TYPE);

  name = get_identifier("type");
  field = build_decl(BUILTINS_LOCATION, FIELD_DECL, name, integer_type_node);
  DECL_CONTEXT(field) = gpy_object_struct_Type;
  DECL_CHAIN(last_field) = field;
  last_field = field;

  field = build_decl(BUILTINS_LOCATION, FIELD_DECL, name,
		     union_type_decl);
  DECL_CONTEXT(field) = gpy_object_struct_Type;
  DECL_CHAIN(last_field) = field;
  last_field = field;

  layout_type (object_state_struct_Type);

  name = get_identifier ("gpy_object_t");
  tree gpy_object_type_decl = build_decl(BUILTINS_LOCATION, TYPE_DECL, name,
					 gpy_object_struct_Type);
  DECL_ARTIFICIAL(gpy_object_type_decl) = 1;
  TYPE_NAME(gpy_object_struct_Type) = name;
  gpy_preserve_from_gc(gpy_object_type_decl);
  rest_of_decl_compilation(gpy_object_type_decl, 1, 0);

  return build_pointer_type (gpy_object_struct_Type);
}

tree gpy_dot_type_const_string_tree (const char * str)
{
  tree index_type = build_index_type (size_int (strlen (str)));
  tree const_char_type = build_qualified_type (unsigned_char_type_node,
					       TYPE_QUAL_CONST);
  tree string_type = build_array_type (const_char_type, index_type);
  string_type = build_variant_type_copy (string_type);
  TYPE_STRING_FLAG (string_type) = 1;
  tree string_val = build_string (strlen (str), str);
  TREE_TYPE (string_val) = string_type;
  return string_val;
}

void gpy_dot_types_init (void)
{
  tree const_char_type = build_qualified_type (unsigned_char_type_node,
					       TYPE_QUAL_CONST);
  tree ctype = build_pointer_type (const_char_type);

  vec_safe_push (gpy_builtin_types_vec, gpy_build_py_object_type ());
  gpy_preserve_from_gc (gpy_object_type_ptr);

  vec_safe_push (gpy_builtin_types_vec, build_pointer_type (gpy_object_type_ptr));
  gpy_preserve_from_gc (gpy_object_type_ptr_ptr);

  vec_safe_push (gpy_builtin_types_vec, ctype);
  gpy_preserve_from_gc (gpy_const_char_ptr);

  vec_safe_push (gpy_builtin_types_vec, gpy_build_py_vector_type ());
  gpy_preserve_from_gc (gpy_vector_type);

  vec_safe_push (gpy_builtin_types_vec, build_pointer_type (gpy_vector_type));
  gpy_preserve_from_gc (gpy_vector_type_ptr);

  vec_safe_push (gpy_builtin_types_vec, build_pointer_type (gpy_vector_type_ptr));
  gpy_preserve_from_gc (gpy_vector_type_ptr_ptr);

  vec_safe_push (gpy_builtin_types_vec, gpy_build_py_attrib_type ());
  gpy_preserve_from_gc (gpy_attrib_type);

  vec_safe_push (gpy_builtin_types_vec, build_pointer_type (gpy_attrib_type));
  gpy_preserve_from_gc (gpy_attrib_type_ptr);

  vec_safe_push (gpy_builtin_types_vec, build_pointer_type (gpy_attrib_type_ptr));
  gpy_preserve_from_gc (gpy_attrib_type_ptr_ptr);
}


