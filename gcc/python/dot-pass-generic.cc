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

/* DEFINES */
#define DOT_CURRENT_CONTEXT(x_)					\
  VEC_index (dot_contextEntry_t, x_, VEC_length (x_) - 1)
/* ... ... ... */

static tree moduleInit = NULL_TREE;

/* PROTOTYPES... */
static tree dot_pass_genFndecl_Basic (tree, tree);

static tree dot_pass_getModuleType (const char *, gpy_hash_tab_t *);
static void dot_pass_setupContext (tree, dot_contextTable_t, vec<tree,va_gc> *, tree *);
static void dot_pass_generic_TU (gpy_hash_tab_t *, vec<gpydot,va_gc> *, vec<tree,va_gc> *);

static tree dot_pass_genFindAddr (const char *, const char *, vec<tree,va_gc> *);
static void dot_pass_genWalkClass (tree *, tree, tree, vec<tree,va_gc> *);
static void dot_pass_setupClassFieldContext (tree, tree, dot_contextEntry_t);

static tree dot_pass_lookupDecl (dot_contextTable_t, const char *);
static int dot_pass_pushDecl (tree, const char *, dot_contextEntry_t);

static tree dot_pass_genFunction (gpy_dot_tree_t *, dot_contextTable_t, const char *);
static vec<tree,va_gc> * dot_pass_genClass (gpy_dot_tree_t *, dot_contextTable_t, tree, const char *);

static tree dot_pass_genScalar (gpy_dot_tree_t *, tree *);
static tree dot_pass_genEnclosure (gpy_dot_tree_t *, tree *, dot_contextTable_t);
static void dot_pass_genCBlock (gpy_dot_tree_t *, tree *, dot_contextTable_t, tree, tree);

static tree dot_pass_lowerExpr (gpy_dot_tree_t *, dot_contextTable_t, tree *);
static void dot_pass_genPrintStmt (gpy_dot_tree_t * , tree *, dot_contextTable_t);
static void dot_pass_genReturnStmt (gpy_dot_tree_t * , tree *, dot_contextTable_t);
static tree dot_pass_genModifyExpr (gpy_dot_tree_t *, tree *, dot_contextTable_t);
static tree dot_pass_genBinExpr (gpy_dot_tree_t *, tree *, dot_contextTable_t);

static void dot_pass_genConditional (gpy_dot_tree_t *, tree *, dot_contextTable_t);
static void dot_pass_genWhile (gpy_dot_tree_t *, tree *, dot_contextTable_t);
static void dot_pass_genFor (gpy_dot_tree_t *, tree *, dot_contextTable_t);
static void dot_pass_genImport (gpy_dot_tree_t *, tree *, dot_contextTable_t);
static void dot_pass_genSuite (gpy_dot_tree_t * , tree *, dot_contextTable_t);
/* ... ... ... */

static
tree dot_pass_genFndecl_Basic (tree ident, tree fntype)
{
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL, ident, fntype);

  TREE_STATIC (fndecl) = 0;
  TREE_USED (fndecl) = 1;
  DECL_ARTIFICIAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  tree argslist = NULL_TREE;
  DECL_ARGUMENTS (fndecl) = argslist;
  /* Define the return type (represented by RESULT_DECL) for the main functin */
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL,
			     NULL_TREE, TREE_TYPE (fntype));
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_ARTIFICIAL (resdecl) = true;
  DECL_IGNORED_P (resdecl) = true;
  DECL_RESULT (fndecl) = resdecl;

  if (DECL_STRUCT_FUNCTION(fndecl) == NULL)
    push_struct_function(fndecl);
  else
    push_cfun(DECL_STRUCT_FUNCTION(fndecl));

  return fndecl;
}

static
tree dot_pass_getModuleType (const char * s,
			     gpy_hash_tab_t * modules)
{
  tree retval = error_mark_node;

  gpy_hashval_t h = gpy_dd_hash_string (s);
  gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (modules, h);
  if (e)
    {
      if (e->data)
        retval = (tree) e->data;
    }
  return retval;
}

static
void dot_pass_setupClassFieldContext (tree type, tree self,
				      dot_contextEntry_t context)
{
  tree field = NULL_TREE;
  for (field = TYPE_FIELDS (type); field != NULL_TREE;
       field = DECL_CHAIN (field))
    {
      const char * ident = IDENTIFIER_POINTER (DECL_NAME (field));
      tree ref = build3 (COMPONENT_REF, TREE_TYPE (field),
			 build_fold_indirect_ref (self),
			 field, NULL_TREE);
      gcc_assert (dot_pass_pushDecl (ref, ident, context));
    }
}

static
tree dot_pass_genFindAddr (const char * id,
			   const char * parent_ident,
			   vec<tree,va_gc> * decls)
{
  tree retval = null_pointer_node;
  tree ident = dot_pass_concat_identifier (parent_ident, id);
  const char * search = IDENTIFIER_POINTER (ident);

  int idx;
  tree decl = NULL_TREE;
  for (idx = 0; decls->iterate (idx, &decl); ++idx)
    {
      tree decl_name = DECL_NAME (decl);
      if (!strcmp (search, IDENTIFIER_POINTER (decl_name)))
	{
	  retval = decl;
	  break;
	}
    }
  return retval;
}

static
void dot_pass_genWalkClass (tree * block, tree type,
			    tree decl,
			    vec<tree,va_gc> * ldecls)
{
  const char * type_name = IDENTIFIER_POINTER (TYPE_NAME (type));
  vec<tree,va_gc> * attribs;
  vec_alloc (attribs, 0);

  tree field = NULL_TREE;
  int offset = 0;
  for (field = TYPE_FIELDS (type); field != NULL_TREE;
       field = DECL_CHAIN (field))
    {
      const char * ident = IDENTIFIER_POINTER (DECL_NAME (field));
      tree element_size = TYPE_SIZE_UNIT (TREE_TYPE (field));
      tree offs = fold_build2_loc (UNKNOWN_LOCATION, MULT_EXPR, sizetype,
				   build_int_cst (sizetype, offset),
				   element_size);
      tree str = gpy_dot_type_const_string_tree (ident);
      tree fnaddr = dot_pass_genFindAddr (ident, type_name, ldecls);

      tree fnaddr_tmp = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				    create_tmp_var_name ("FNAD"),
				    ptr_type_node);
      tree arguments = NULL_TREE;
      int n = 0;
      if (fnaddr != null_pointer_node)
	{
	  gcc_assert (TREE_CODE (fnaddr) == FUNCTION_DECL);
	  arguments = DECL_ARGUMENTS (fnaddr);
	  tree args;
	  for (args = arguments; args != NULL_TREE;
	       args = DECL_CHAIN (args))
	    n++;
	  append_to_statement_list (build2 (MODIFY_EXPR, ptr_type_node,
					    fnaddr_tmp,
					    build_fold_addr_expr (fnaddr)),
				    block);
	}
      else
	append_to_statement_list (build2 (MODIFY_EXPR, ptr_type_node,
					  fnaddr_tmp,
					  build_int_cst (ptr_type_node, 0)),
				  block);

      tree nargs = build_int_cst (integer_type_node, n);
      tree atdecl = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				create_tmp_var_name ("AT"),
				gpy_attrib_type_ptr);
      tree a = GPY_RR_fold_attrib (build_fold_addr_expr (str),
				   fnaddr_tmp,
				   offs, nargs);
      append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					atdecl, a),
				block);
      vec_safe_push (attribs, atdecl);
      offset++;
    }
  vec<tree,va_gc> * args;
  vec_alloc (args, 0);
  vec_safe_push (args, build_int_cst (integer_type_node,
				      attribs->length ()));
  GPY_VEC_stmts_append (tree, args, attribs);

  tree attribs_decl = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				  create_tmp_var_name ("AL"),
				  gpy_attrib_type_ptr_ptr);
  append_to_statement_list (build2 (MODIFY_EXPR, gpy_attrib_type_ptr_ptr,
				    attribs_decl,
				    GPY_RR_fold_attrib_list (args)),
			    block);
  tree class_str = gpy_dot_type_const_string_tree (type_name);
  tree fold_class = GPY_RR_fold_class_decl (attribs_decl, TYPE_SIZE_UNIT (type),
					    build_fold_addr_expr (class_str));

  switch (TREE_CODE (decl))
    {
    case POINTER_PLUS_EXPR:
      {
	tree fold = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				create_tmp_var_name ("__MOD_DECL_ACC"),
				gpy_object_type_ptr_ptr);
	append_to_statement_list (build2 (MODIFY_EXPR,
					  gpy_object_type_ptr_ptr,
					  fold, decl),
				  block);

	tree class_tmp = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				     create_tmp_var_name ("ATFC"),
				     gpy_object_type_ptr);
	append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					  class_tmp, fold_class),
				  block);
	tree refer = build_fold_indirect_ref (fold);
	append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					  refer,
					  class_tmp),
				  block);
      }
      break;

    default:
      fatal_error ("Error seting up class decl!\n");
      break;
    }
}

static
tree dot_pass_foldCIDS (int len, char ** cids)
{
  tree fntype = build_function_type_list (ptr_type_node,
					  integer_type_node,
					  va_list_type_node,
					  NULL_TREE);
  tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			    get_identifier ("gpy_rr_modAttribList"),
			    fntype);
  tree restype = TREE_TYPE (fndecl);
  tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
			     restype);
  DECL_CONTEXT (resdecl) = fndecl;
  DECL_RESULT (fndecl) = resdecl;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  int i;
  vec<tree,va_gc> *args;
  vec_alloc (args, 0);

  vec_safe_push (args, build_int_cst (integer_type_node, len));
  for (i = 0; i < len; ++i)
    {
      char * id = cids [i];
      tree tid = gpy_dot_type_const_string_tree (id);
      vec_safe_push (args, build_fold_addr_expr (tid));
    }
  return build_call_expr_loc_vec (BUILTINS_LOCATION, fndecl, args);
}

static
void dot_pass_setupContext (tree module,
			    dot_contextTable_t context,
			    vec<tree,va_gc> * generic,
			    tree * block)
{
  dot_contextEntry_t globls = VEC_index (dot_contextEntry_t, context, 0);
  dot_contextEntry_t globls_symbols = VEC_index (dot_contextEntry_t, context, 1);

  tree stack_pointer = build_decl (BUILTINS_LOCATION, VAR_DECL,
				   get_identifier (GPY_RR_stack_ptr),
				   gpy_object_type_ptr_ptr);
  TREE_PUBLIC (stack_pointer) = 1;
  TREE_USED (stack_pointer) = 1;
  DECL_EXTERNAL (stack_pointer) = 1;

  DECL_INITIAL (stack_pointer) = build_int_cst (integer_type_node, 0);
  rest_of_decl_compilation (stack_pointer, 1, 0);

  gpy_dd_hash_insert (gpy_dd_hash_string (GPY_RR_stack_ptr), stack_pointer, globls);
  vec_safe_push (generic, stack_pointer);

  int offset = 0, field_count = 0;
  tree field;
  for (field = TYPE_FIELDS (module); field != NULL_TREE;
       field = DECL_CHAIN (field))
    field_count++;

  char ** cids = (char **) xcalloc (field_count, sizeof (char *));
  memset (cids, 0, sizeof (char *)*field_count);

  int i = 0;
  for (field = TYPE_FIELDS (module); field != NULL_TREE;
       field = DECL_CHAIN (field))
    {
      gcc_assert (TREE_CODE (field) == FIELD_DECL);
      const char * ident = IDENTIFIER_POINTER (DECL_NAME (field));

      cids [i] = xstrdup (ident);
      i++;
    }

  tree fcids = dot_pass_foldCIDS (field_count, cids);
  tree fcids_decl = build_decl (BUILTINS_LOCATION, VAR_DECL,
				create_tmp_var_name ("FCIDS"),
				ptr_type_node);
  append_to_statement_list (build2 (MODIFY_EXPR, ptr_type_node,
				    fcids_decl, fcids),
			    block);
  tree str = gpy_dot_type_const_string_tree (GPY_current_module_name);
  tree stack_extendCall = GPY_RR_extendRRStack (build_int_cst (integer_type_node,
							       field_count),
						build_fold_addr_expr (str),
						fcids_decl);

  tree stack_offset = build_decl (BUILTINS_LOCATION, VAR_DECL,
				  create_tmp_var_name ("__MODULE_STK_OFFS"),
				  sizetype);
  TREE_STATIC (stack_offset) = 1;
  TREE_PUBLIC (stack_offset) = 0;
  TREE_USED (stack_offset) = 1;
  DECL_INITIAL (stack_offset) = build_int_cst (integer_type_node, 0);
  rest_of_decl_compilation (stack_offset, 1, 0);

  append_to_statement_list (build2 (MODIFY_EXPR, integer_type_node,
				    stack_offset, stack_extendCall),
			    block);
  
  offset = 0;
  field_count = 0;
  for (field = TYPE_FIELDS (module); field != NULL_TREE;
       field = DECL_CHAIN (field))
    {
      gcc_assert (TREE_CODE (field) == FIELD_DECL);
      const char * ident = IDENTIFIER_POINTER (DECL_NAME (field));

      tree element_size = TYPE_SIZE_UNIT (TREE_TYPE (field));

      tree offs1 = build2 (MULT_EXPR, sizetype,
			   build_int_cst (sizetype, offset),
			   element_size);
      tree offs2 = build2 (MULT_EXPR, sizetype,
			   stack_offset,
			   element_size);

      tree offs = build2 (PLUS_EXPR, sizetype,
			  offs1, offs2);
      tree addr = build2 (POINTER_PLUS_EXPR,
			  TREE_TYPE (stack_pointer),
			  stack_pointer, offs);

      gcc_assert (dot_pass_pushDecl (addr, ident, globls_symbols));
      offset++;
      field_count++;
    }

  // the module_init shizzle!
  moduleInit = build_decl (BUILTINS_LOCATION, VAR_DECL,
			   get_identifier ("__MOD_INIT_CHK"),
			   boolean_type_node);
  TREE_STATIC (moduleInit) = 1;
  TREE_PUBLIC (moduleInit) = 0;
  TREE_USED (moduleInit) = 1;
  DECL_INITIAL(moduleInit) = boolean_false_node;
  rest_of_decl_compilation (moduleInit, 1, 0);
}

static
int dot_pass_pushDecl (tree decl, const char * ident,
		       dot_contextEntry_t context)
{
  int retval = 1;
  gpy_hashval_t h = gpy_dd_hash_string (ident);
  void ** slot = gpy_dd_hash_insert (h, decl, context);
  if (slot)
    {
      error ("error pushing decl <%s>!\n", ident);
      retval = 0;
    }
  return retval;
}

static
tree dot_pass_lookupDecl (dot_contextTable_t context,
			  const char * identifier)
{
  tree retval = error_mark_node;
  gpy_hashval_t h = gpy_dd_hash_string (identifier);
  int length = VEC_length (context);

  int i;
  for (i = length - 1; i >= 0; --i)
    {
      dot_contextEntry_t ctx = VEC_index (dot_contextEntry_t, context, i);
      gpy_hash_entry_t * o = NULL;
      o = gpy_dd_hash_lookup_table (ctx, h);

      if (o)
	if (o->data)
	  {
	    retval = (tree) o->data;
	    break;
	  }
    }
  return retval;
}

static
tree dot_pass_genScalar (gpy_dot_tree_t * decl, tree * block)
{
  tree retval = error_mark_node;

  gcc_assert (DOT_TYPE (decl) == D_PRIMITIVE);
  gcc_assert (DOT_lhs_T (decl) == D_TD_COM);

  switch (DOT_lhs_TC (decl)->T)
    {
    case D_T_INTEGER:
      {
        retval = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			     create_tmp_var_name ("PI"),
                             gpy_object_type_ptr);
	tree fold_call = GPY_RR_fold_integer (build_int_cst (integer_type_node,
							     DOT_lhs_TC (decl)->o.integer));
        append_to_statement_list (build2 (MODIFY_EXPR,
					  gpy_object_type_ptr,
					  retval, fold_call),
                                  block);
      }
      break;

    default:
      error ("invalid scalar type!\n");
      break;
    }

  return retval;
}

static
tree dot_pass_genEnclosure (gpy_dot_tree_t * decl,
			    tree * block,
			    dot_contextTable_t context)
{
  tree retval = error_mark_node;
  gcc_assert (DOT_TYPE (decl) == D_T_LIST);

  gpy_dot_tree_t * node;
  size_t length = 0;
  for (node = DOT_lhs_TT (decl); node != NULL_DOT;
       node = DOT_CHAIN (node))
    length++;

  vec<tree,va_gc> * elms;
  vec_alloc (elms, 0);
  vec_safe_push (elms, build_int_cst (integer_type_node, length));
  for (node = DOT_lhs_TT (decl); node != NULL_DOT;
       node = DOT_CHAIN (node))
    {
      tree tmp = dot_pass_lowerExpr (node, context, block);
      vec_safe_push (elms, tmp);
    }
  retval = build_decl (UNKNOWN_LOCATION, VAR_DECL,
		       create_tmp_var_name ("PL"),
		       gpy_object_type_ptr);
  tree fold_call = GPY_RR_fold_encList (elms);
  append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
				    retval, fold_call),
			    block);
  return retval;
}

static
void dot_pass_genSuite (gpy_dot_tree_t * decl,
			tree * block,
			dot_contextTable_t context)
{
  gpy_hash_tab_t suite;
  gpy_dd_hash_init_table (&suite);

  gpy_vec_push (context, &suite);

  gpy_dot_tree_t * node;
  for (node = decl; node != NULL_DOT;
       node = DOT_CHAIN (node))
    {
      if (DOT_T_FIELD (node) ==  D_D_EXPR)
	{
	  dot_pass_lowerExpr (node, context, block);
	  continue;
	}
      switch (DOT_TYPE (node))
	{
	case D_PRINT_STMT:
	  dot_pass_genPrintStmt (node, block, context);
	  break;

	case D_KEY_RETURN:
	  dot_pass_genReturnStmt (node, block, context);
	  break;

	case D_STRUCT_CONDITIONAL:
	  dot_pass_genConditional (node, block, context);
	  break;

	case D_STRUCT_WHILE:
	  dot_pass_genWhile (node, block, context);
	  break;

	case D_STRUCT_FOR:
	  dot_pass_genFor (node, block, context);
	  break;

	default:
	  error ("unhandled syntax within suite");
	  break;
	}
    }

  gpy_vec_pop (context);
}

static
void dot_pass_genCBlock (gpy_dot_tree_t * decl,
			 tree * block,
			 dot_contextTable_t context,
			 tree cval, tree endif)
{
  gpy_dot_tree_t * suite = NULL_DOT;
  tree ifcval = error_mark_node;
  if (DOT_TYPE (decl) == D_STRUCT_IF
      || DOT_TYPE (decl) == D_STRUCT_ELIF)
    {
      gpy_dot_tree_t * expr = DOT_lhs_TT (decl);
      tree val = dot_pass_lowerExpr (expr, context, block);
      tree lval = val;
      if (TREE_TYPE (lval) == gpy_object_type_ptr_ptr)
	{
	  lval = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			     create_tmp_var_name ("CDRD"),
			     gpy_object_type_ptr);
	  append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					    lval, build_fold_indirect_ref (val)),
				    block);
	}
      tree fold = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			      create_tmp_var_name ("COND"),
			      boolean_type_node);
      append_to_statement_list (build2 (MODIFY_EXPR, boolean_type_node,
					fold, GPY_RR_eval_boolean (lval)),
				block);
      ifcval = fold;
      suite = DOT_rhs_TT (decl);
    }
  else
    {
      gcc_assert (DOT_TYPE (decl) == D_STRUCT_ELSE);
      suite = DOT_lhs_TT (decl);
    }

  tree label_decl = build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				create_tmp_var_name ("LBIF"),
				void_type_node);
  tree label_expr = fold_build1_loc (UNKNOWN_LOCATION, LABEL_EXPR,
				     void_type_node, label_decl);

  tree label_exit_decl =  build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				      create_tmp_var_name ("LBFI"),
				      void_type_node);
  tree label_exit_expr = fold_build1_loc (UNKNOWN_LOCATION, LABEL_EXPR,
					  void_type_node, label_exit_decl);

  DECL_CONTEXT (label_decl) = current_function_decl;
  DECL_CONTEXT (label_exit_decl) = current_function_decl;

  tree condition;
  tree evalto;
  if (ifcval != error_mark_node)
    {
      condition = ifcval;
      evalto = boolean_true_node;
    }
  else
    {
      condition = cval;
      evalto = boolean_false_node;
    }
  tree conditional = fold_build2_loc (UNKNOWN_LOCATION, EQ_EXPR,
				      boolean_type_node,
				      condition, evalto);

  tree cond = build3_loc (UNKNOWN_LOCATION, COND_EXPR, void_type_node,
			  conditional,
			  build1 (GOTO_EXPR, void_type_node, label_decl),
			  build1 (GOTO_EXPR, void_type_node, label_exit_decl));

  append_to_statement_list (cond, block);
  append_to_statement_list (label_expr, block);

  dot_pass_genSuite (suite, block, context);

  append_to_statement_list (build2 (MODIFY_EXPR, boolean_type_node,
				    cval, boolean_true_node),
			    block);
  append_to_statement_list (build1 (GOTO_EXPR, void_type_node, endif),
			    block);
  append_to_statement_list (label_exit_expr, block);
}


static
void dot_pass_genImport (gpy_dot_tree_t * decl,
			 tree * block,
			 dot_contextTable_t context)
{
  const char * import = DOT_IDENTIFIER_POINTER (DOT_lhs_TT (decl));
  tree lookup = dot_pass_lookupDecl (context, import);
  gcc_assert (lookup != error_mark_node);

  struct gpy_dataExport * exp = gpy_readExportData (import);
  if (exp == NULL)
    error ("No export data for module <%s>\n", import);
  else
    {
      if (GPY_OPT_gen_main && exp->main)
	error ("Module %s already has main!\n", import);

      // ... setup runtime to call module entry...
      tree decl = lookup;
      tree ident = get_identifier (exp->entry);

      tree fntype = build_function_type_list (void_type_node, NULL_TREE);
      tree fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
				ident, fntype);
      tree restype = TREE_TYPE (fndecl);
      tree resdecl = build_decl (BUILTINS_LOCATION, RESULT_DECL, NULL_TREE,
				 restype);
      DECL_CONTEXT (resdecl) = fndecl;
      DECL_RESULT (fndecl) = resdecl;
      DECL_EXTERNAL (fndecl) = 1;
      TREE_PUBLIC (fndecl) = 1;
      append_to_statement_list (build_call_expr (fndecl, 0), block);

      tree tstr = gpy_dot_type_const_string_tree (import);
      tree imp = GPY_RR_foldImport (decl, build_fold_addr_expr (tstr));
      append_to_statement_list (imp, block);
    }
}

static
void dot_pass_genFor (gpy_dot_tree_t * decl,
		      tree * block,
		      dot_contextTable_t context)
{
  debug ("Trying to compile the for loop!\n");

  gpy_dot_tree_t * it = DOT_FIELD (decl);
  gpy_dot_tree_t * in = DOT_lhs_TT (decl);
  gpy_dot_tree_t * suite = DOT_rhs (decl);

  fatal_error ("For loops/iterators not implemented yet!\n");

  /* Not sure how to implement this yet... */
  tree exprVal = dot_pass_lowerExpr (in, context, block);

  /**
   * Really not sure how to implement iterators at the moment
   **/

  dot_pass_genSuite (suite, block, context);

}

static
void dot_pass_genWhile (gpy_dot_tree_t * decl,
			tree * block,
			dot_contextTable_t context)
{
  tree label_decl = build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				create_tmp_var_name ("WHLIF"),
				void_type_node);
  tree label_expr = fold_build1_loc (UNKNOWN_LOCATION, LABEL_EXPR,
				     void_type_node, label_decl);

  tree label_exit_decl =  build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				      create_tmp_var_name ("WHLFI"),
				      void_type_node);
  tree label_exit_expr = fold_build1_loc (UNKNOWN_LOCATION, LABEL_EXPR,
					  void_type_node, label_exit_decl);

  DECL_CONTEXT (label_decl) = current_function_decl;
  DECL_CONTEXT (label_exit_decl) = current_function_decl;

  append_to_statement_list (label_expr, block);

  gpy_dot_tree_t * expr = DOT_lhs_TT (decl);
  tree val = dot_pass_lowerExpr (expr, context, block);
  tree lval = val;
  if (TREE_TYPE (lval) == gpy_object_type_ptr_ptr)
    {
      lval = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			 create_tmp_var_name ("CDRD"),
			 gpy_object_type_ptr);
      append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					lval, build_fold_indirect_ref (val)),
				block);
    }
  tree fold = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			  create_tmp_var_name ("COND"),
			  boolean_type_node);


  append_to_statement_list (build2 (MODIFY_EXPR, boolean_type_node,
				    fold, GPY_RR_eval_boolean (lval)),
			    block);

  gpy_dot_tree_t * suite = DOT_rhs_TT (decl);

  tree conditional = fold_build2_loc (UNKNOWN_LOCATION, EQ_EXPR,
				      boolean_type_node,
				      fold, boolean_true_node);
  tree cond = build3_loc (UNKNOWN_LOCATION, COND_EXPR, void_type_node,
			  conditional,
			  NULL_TREE,
			  build1 (GOTO_EXPR, void_type_node, label_exit_decl));

  append_to_statement_list (cond, block);
  dot_pass_genSuite (suite, block, context);
  append_to_statement_list (build1 (GOTO_EXPR, void_type_node, label_decl),
			    block);
  append_to_statement_list (label_exit_expr, block);
}

static
void dot_pass_genConditional (gpy_dot_tree_t * decl,
			      tree * block,
			      dot_contextTable_t context)
{
  tree cval = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			  create_tmp_var_name ("CELSE"),
			  boolean_type_node);
  DECL_INITIAL (cval) = boolean_false_node;
  append_to_statement_list (cval, block);

  /*
    Make each conditional block exit out of the whole thing properly
   */
  tree endif_label_decl = build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				      create_tmp_var_name ("ENDIF"),
				      void_type_node);
  tree endif_label_expr = fold_build1_loc (UNKNOWN_LOCATION, LABEL_EXPR,
					   void_type_node, endif_label_decl);
  DECL_CONTEXT (endif_label_decl) = current_function_decl;

  gpy_dot_tree_t * ifblock = DOT_FIELD (decl);
  gpy_dot_tree_t * elifchain = DOT_lhs_TT (decl);
  gpy_dot_tree_t * elseblock = DOT_rhs_TT (decl);

  dot_pass_genCBlock (ifblock, block, context, cval, endif_label_decl);

  gpy_dot_tree_t * elifnode;
  for (elifnode = elifchain; elifnode != NULL_DOT;
       elifnode = DOT_CHAIN (elifnode))
    dot_pass_genCBlock (elifnode, block, context, cval, endif_label_decl);

  if (elseblock)
    dot_pass_genCBlock (elseblock, block, context, cval, endif_label_decl);

  append_to_statement_list (endif_label_expr, block);
}

static
tree dot_pass_genModifyExpr (gpy_dot_tree_t * decl,
			     tree * block,
			     dot_contextTable_t context)
{
  tree retval = error_mark_node;
  gpy_dot_tree_t * lhs = DOT_lhs_TT (decl);
  gpy_dot_tree_t * rhs = DOT_rhs_TT (decl);

  /*
    We dont handle full target lists yet
    all targets are in the lhs tree.

    To implment a target list such as:
    x,y,z = 1

    The lhs should be a DOT_CHAIN of identifiers!
    So we just iterate over them and deal with it as such!
  */

  switch (DOT_TYPE (lhs))
    {
    case D_IDENTIFIER:
      {
	tree addr = dot_pass_lookupDecl (context,
					 DOT_IDENTIFIER_POINTER (lhs));
	/* means id isn't previously declared and we can just make it locally. */
	if (addr == error_mark_node)
	  {
	    dot_contextEntry_t current_context = DOT_CURRENT_CONTEXT (context);
	    addr = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			       get_identifier (DOT_IDENTIFIER_POINTER (lhs)),
			       gpy_object_type_ptr);
	    gcc_assert (dot_pass_pushDecl (addr, DOT_IDENTIFIER_POINTER (lhs), current_context));
	  }
	tree addr_rhs_tree = dot_pass_lowerExpr (rhs, context, block);

	switch (TREE_CODE (addr))
	  {
	  case POINTER_PLUS_EXPR:
	    {
	      tree fold = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				      create_tmp_var_name ("__MOD_DECL_ACC"),
				      gpy_object_type_ptr_ptr);
	      append_to_statement_list (build2 (MODIFY_EXPR,
						gpy_object_type_ptr_ptr,
						fold, addr),
					block);

	      tree refer = build_fold_indirect_ref (fold);
	      append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
						refer,
						addr_rhs_tree),
					block);
	      retval = refer;
	    }
	    break;

	  case PARM_DECL:
	  case COMPONENT_REF:
	  case VAR_DECL:
	    {
	      if (TREE_TYPE (addr) == gpy_object_type_ptr_ptr)
		{
		  /* *T.x = addr */
		  tree refer = build_fold_indirect_ref (addr);
		  append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
						    refer,
						    addr_rhs_tree),
					    block);
		  retval = refer;
		}
	      else
		{
		  append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
						    addr,
						    addr_rhs_tree),
					    block);
		  retval = addr;
		}
	    }
	    break;

	  default:
	    error ("unhandled shizzle!\n");
	    break;
	  }
      }
      break;

    case D_ATTRIB_REF:
      {
	tree addr_rhs_tree = dot_pass_lowerExpr (rhs, context, block);
	tree addr_lhs_tree = dot_pass_lowerExpr (lhs, context, block);

	gcc_assert (TREE_TYPE (addr_lhs_tree) == gpy_object_type_ptr_ptr);
	append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr_ptr,
					  build_fold_indirect_ref (addr_lhs_tree),
					  addr_rhs_tree),
				  block);
	retval = addr_lhs_tree;
      }
      break;

    default:
      error ("unhandled target or target list in modify expression\n");
      break;
    }

  return retval;
}

static
tree dot_pass_cleanRef (tree decl, tree * block)
{
  tree retval = decl;
  if (TREE_TYPE (decl) == gpy_object_type_ptr_ptr)
    {
      tree fold = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			      create_tmp_var_name ("CLRF"),
			      gpy_object_type_ptr);
      append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					fold, build_fold_indirect_ref (decl)),
				block);
      retval = fold;
    }
  return retval;
}

static
tree dot_pass_genBinExpr (gpy_dot_tree_t * decl, tree * block,
			  dot_contextTable_t context)
{
  tree retval = error_mark_node;
  gcc_assert (DOT_T_FIELD (decl) == D_D_EXPR);

  gpy_dot_tree_t * lhs = DOT_lhs_TT (decl);
  gpy_dot_tree_t * rhs = DOT_rhs_TT (decl);
  tree lhs_eval = dot_pass_lowerExpr (lhs, context, block);
  tree rhs_eval = dot_pass_lowerExpr (rhs, context, block);

  lhs_eval = dot_pass_cleanRef (lhs_eval, block);
  rhs_eval = dot_pass_cleanRef (rhs_eval, block);

  tree op = error_mark_node;
  switch (DOT_TYPE (decl))
    {
      // @see libgpython/runtime/gpy-module-stack.c::gpy_rr_eval_expression
    case D_ADD_EXPR:
      op = GPY_RR_eval_expression (lhs_eval, rhs_eval,
				   build_int_cst (integer_type_node, 1));
      break;

    case D_MINUS_EXPR:
      op = GPY_RR_eval_expression (lhs_eval, rhs_eval,
				   build_int_cst (integer_type_node, 2));
      break;

    case D_MULT_EXPR:
      op = GPY_RR_eval_expression (lhs_eval, rhs_eval,
				   build_int_cst (integer_type_node, 4));
      break;

    case D_LESS_EXPR:
      op = GPY_RR_eval_expression (lhs_eval, rhs_eval,
				   build_int_cst (integer_type_node, 6));
      break;

    case D_GREATER_EXPR:
      op = GPY_RR_eval_expression (lhs_eval, rhs_eval,
				   build_int_cst (integer_type_node, 8));
      break;

    case D_EQ_EQ_EXPR:
      op = GPY_RR_eval_expression (lhs_eval, rhs_eval,
				   build_int_cst (integer_type_node, 10));
      break;

    default:
      error ("unhandled binary operation type!\n");
      break;
    }
  gcc_assert (op != error_mark_node);

  tree retaddr = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			     create_tmp_var_name ("T"),
                             gpy_object_type_ptr);
  append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
				    retaddr, op),
			    block);
  retval = retaddr;
  return retval;
}

static
void dot_pass_genReturnStmt (gpy_dot_tree_t * decl, tree * block,
			     dot_contextTable_t context)
{
  /* Remember we have have return x or we can simply just have a return */
  if (DOT_lhs_TT (decl))
    {
      tree lexpr = dot_pass_lowerExpr (DOT_lhs_TT (decl), context, block);
      tree tmp = lexpr;
      if (TREE_TYPE (tmp) == gpy_object_type_ptr_ptr)
	{
	  tmp = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			    create_tmp_var_name ("ATAR"),
			    gpy_object_type_ptr);
	  append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					    tmp,
					    build_fold_indirect_ref (lexpr)),
				    block);
	}
      append_to_statement_list (GPY_RR_eval_return (tmp), block);
    }
  append_to_statement_list (fold_build1_loc (UNKNOWN_LOCATION, RETURN_EXPR,
					     void_type_node, NULL_TREE),
			    block);
}

static
void dot_pass_genPrintStmt (gpy_dot_tree_t * decl, tree * block,
			    dot_contextTable_t context)
{
  gpy_dot_tree_t * arguments = decl->opa.t;

  vec<tree,va_gc> * callvec_tmp;
  vec_alloc (callvec_tmp, 0);

  gpy_dot_tree_t * it = NULL;
  for (it = arguments; it != NULL; it = DOT_CHAIN (it))
    {
      tree lexpr = dot_pass_lowerExpr (it, context, block);
      tree tmp = lexpr;
      if (TREE_TYPE (lexpr) == gpy_object_type_ptr_ptr)
	{
	  /* lets fold */
	  tmp = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			    create_tmp_var_name ("ATAR"),
			    gpy_object_type_ptr);
	  append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					    tmp,
					    build_fold_indirect_ref (lexpr)),
				    block);
	}
      vec_safe_push (callvec_tmp, tmp);
    }
  vec<tree,va_gc> * callvec;
  vec_alloc (callvec, 0);
  vec_safe_push (callvec, build_int_cst (integer_type_node, 1));
  vec_safe_push (callvec, build_int_cst (integer_type_node, callvec_tmp->length ()));

  GPY_VEC_stmts_append (tree, callvec, callvec_tmp);
  append_to_statement_list (GPY_RR_eval_print (callvec), block);
}

static
tree dot_pass_lowerExpr (gpy_dot_tree_t * dot,
			 dot_contextTable_t context,
			 tree * block)
{
  tree retval = error_mark_node;
  switch (DOT_TYPE (dot))
    {
    case D_PRIMITIVE:
      retval = dot_pass_genScalar (dot, block);
      break;

    case D_T_LIST:
      retval = dot_pass_genEnclosure (dot, block, context);
      break;

    case D_SLICE:
      {
        tree ident = dot_pass_lowerExpr (DOT_lhs_TT (dot), context, block);
        tree slice = dot_pass_lowerExpr (DOT_rhs_TT (dot), context, block);
        retval = GPY_RR_makeSlice (ident, slice);
      }
      break;

    case D_IDENTIFIER:
      {
	tree lookup = dot_pass_lookupDecl (context,
					   DOT_IDENTIFIER_POINTER (dot));
	gcc_assert (lookup != error_mark_node);
	switch (TREE_CODE (lookup))
	  {
	  case VAR_DECL:
	  case PARM_DECL:
	  case COMPONENT_REF:
	    retval = lookup;
	    break;

	  default:
	    {
	      tree tmp = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				     create_tmp_var_name ("VA"),
				     gpy_object_type_ptr_ptr);
	      append_to_statement_list (build2 (MODIFY_EXPR,
						gpy_object_type_ptr_ptr,
						tmp, lookup),
					block);
	      tree atmp = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				      create_tmp_var_name ("VP"),
				      gpy_object_type_ptr);
	      append_to_statement_list (build2 (MODIFY_EXPR,
						gpy_object_type_ptr,
						atmp, build_fold_indirect_ref (tmp)),
					block);
	      retval = atmp;
	    }
	    break;
	  }
      }
      break;

    case D_ATTRIB_REF:
      {
	tree addr_1 = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				  create_tmp_var_name ("ATRD"),
				  gpy_object_type_ptr_ptr);
	gpy_dot_tree_t * xlhs = DOT_lhs_TT (dot);
	gpy_dot_tree_t * xrhs = DOT_rhs_TT (dot);

	gcc_assert (DOT_TYPE (xrhs) == D_IDENTIFIER);
	tree lhs_tree = dot_pass_lowerExpr (xlhs, context, block);
	const char * attrib_ident = DOT_IDENTIFIER_POINTER (xrhs);

	tree str = gpy_dot_type_const_string_tree (attrib_ident);
	tree lhs_tree_fold = lhs_tree;
	if (TREE_TYPE (lhs_tree) == gpy_object_type_ptr_ptr)
	  {
	    lhs_tree_fold = build_decl (UNKNOWN_LOCATION, VAR_DECL,
					create_tmp_var_name ("ATRDF"),
					gpy_object_type_ptr);
	    append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					      lhs_tree_fold,
					      build_fold_indirect_ref (lhs_tree)),
				      block);
	  }
	tree attrib_ref = build2 (MODIFY_EXPR, gpy_object_type_ptr_ptr, addr_1,
				  GPY_RR_fold_attrib_ref (lhs_tree_fold,
							  build_fold_addr_expr (str))
				  );
	append_to_statement_list (attrib_ref, block);
	retval = addr_1;
      }
      break;

    case D_CALL_EXPR:
      {
	gpy_dot_tree_t * callid = DOT_lhs_TT (dot);

	tree lcall_decl = dot_pass_lowerExpr (callid, context, block);
	tree call_decl = lcall_decl;
	if (TREE_TYPE (lcall_decl) == gpy_object_type_ptr_ptr)
	  {
	    /* lets fold */
	    call_decl = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				    create_tmp_var_name ("ATAR"),
				    gpy_object_type_ptr);
	    append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					      call_decl,
					      build_fold_indirect_ref (lcall_decl)),
				      block);
	  }

	gpy_dot_tree_t * argslist;
	vec<tree,va_gc> * argsvec = NULL;
	if (DOT_TYPE (callid) == D_ATTRIB_REF)
	  {
	    tree basetree = dot_pass_lowerExpr (DOT_lhs_TT (callid),
						context, block);
	    tree basetree_fold = basetree;
	    if (TREE_TYPE (basetree) == gpy_object_type_ptr_ptr)
	      {
		basetree_fold = build_decl (UNKNOWN_LOCATION, VAR_DECL,
					    create_tmp_var_name ("ATRDF"),
					    gpy_object_type_ptr);
		append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
						  basetree_fold,
						  build_fold_indirect_ref (basetree)),
					  block);
	      }
	    vec_safe_push (argsvec, basetree_fold);
	  }
	else
	  {
	    // we push in a null entry...
	    tree nullp = build_int_cst (ptr_type_node, 0);
	    vec_safe_push (argsvec, nullp);
	  }

	for (argslist = DOT_rhs_TT (dot); argslist != NULL_DOT;
	     argslist = DOT_CHAIN (argslist))
	  {
	    tree lexpr = dot_pass_lowerExpr (argslist, context, block);
	    tree argument = lexpr;
	    if (TREE_TYPE (argument) == gpy_object_type_ptr_ptr)
	      {
		argument = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				       create_tmp_var_name ("ATRDF"),
				       gpy_object_type_ptr);
		append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
						  argument,
						  build_fold_indirect_ref (lexpr)),
					  block);
	      }
	    vec_safe_push (argsvec, argument);
	  }
	vec<tree,va_gc> * args = NULL;
	vec_alloc (args, 0);
	vec_safe_push (args, call_decl);

	int tmp = 0;
	if (argsvec)
	  {
	    tmp = argsvec->length ();
	    vec_safe_push (args, build_int_cst (integer_type_node, tmp));
	    GPY_VEC_stmts_append (tree, args, argsvec);
	  }
	else
	  vec_safe_push (args, build_int_cst (integer_type_node, tmp));

	tree retaddr = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				   create_tmp_var_name ("RET"),
				   gpy_object_type_ptr);
	append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					  retaddr,
					  GPY_RR_fold_call (args)),
				  block);
	retval = retaddr;
      }
      break;

    default:
      {
	switch (DOT_TYPE (dot))
          {
	  case D_MODIFY_EXPR:
	    retval = dot_pass_genModifyExpr (dot, block, context);
	    break;

	  case D_ADD_EXPR:
	  case D_MINUS_EXPR:
	  case D_MULT_EXPR:
	  case D_DIVD_EXPR:
	  case D_LESS_EXPR:
	  case D_GREATER_EXPR:
	  case D_EQ_EQ_EXPR:
	    retval = dot_pass_genBinExpr (dot, block, context);
	    break;

	  default:
	    error ("unhandled operation type!\n");
	    break;
	  }
      }
      break;
    }
  return retval;
}

static
vec<tree,va_gc> * dot_pass_genClass (gpy_dot_tree_t * dot,
				     dot_contextTable_t context,
				     tree class_type,
				     const char * pid)
{
  vec<tree,va_gc> * lowered_decls;
  vec_alloc (lowered_decls, 0);

  tree class_type_ptr = build_pointer_type (class_type);
  tree fntype = build_function_type_list (void_type_node,
					  class_type_ptr,
					  NULL_TREE);
  tree ident = dot_pass_concat_identifier (GPY_current_module_name,
					   DOT_IDENTIFIER_POINTER (DOT_FIELD (dot)));
  ident = dot_pass_concat_identifier (IDENTIFIER_POINTER (ident),
				      "__field_init__");
  tree fndecl = dot_pass_genFndecl_Basic (ident, fntype);
  current_function_decl = fndecl;

  tree arglist = NULL_TREE;
  tree self_parm_decl = build_decl (BUILTINS_LOCATION, PARM_DECL,
                                    get_identifier ("__object_state__"),
                                    class_type_ptr);
  DECL_CONTEXT (self_parm_decl) = fndecl;
  DECL_ARG_TYPE (self_parm_decl) = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (fndecl)));
  TREE_READONLY (self_parm_decl) = 1;
  arglist = chainon (arglist, self_parm_decl);
  TREE_USED (self_parm_decl) = 1;
  DECL_ARGUMENTS (fndecl) = arglist;

  gpy_hash_tab_t field_type_namespace;
  gpy_dd_hash_init_table (&field_type_namespace);
  dot_pass_setupClassFieldContext (class_type, self_parm_decl, &field_type_namespace);

  gpy_vec_push (context, &field_type_namespace);

  tree block = alloc_stmt_list ();
  gpy_dot_tree_t * node;
  for (node = DOT_lhs_TT (dot); node != NULL_DOT;
       node = DOT_CHAIN (node))
    {
      if (DOT_T_FIELD (node) ==  D_D_EXPR)
	{
	  dot_pass_lowerExpr (node, context, &block);
	  continue;
	}
      switch (DOT_TYPE (node))
	{
	case D_PRINT_STMT:
	  dot_pass_genPrintStmt (node, &block, context);
	  break;

	case D_KEY_RETURN:
	  dot_pass_genReturnStmt (node, &block, context);
	  break;

	case D_STRUCT_WHILE:
	  dot_pass_genWhile (node, &block, context);
	  break;

	case D_STRUCT_METHOD:
	  {
	    const char * modID = DOT_IDENTIFIER_POINTER (DOT_FIELD (dot));
	    const char * npid = dot_pass_concat (pid, modID);
	    tree attrib = dot_pass_genFunction (node, context, npid);
	    vec_safe_push (lowered_decls, attrib);
	  }
	  break;

	default:
	  error ("unhandled syntax within class!\n");
	  break;
	}
    }
  tree bind = NULL_TREE;
  tree bl = build_block (DECL_RESULT (fndecl), NULL_TREE, fndecl, NULL_TREE);
  DECL_INITIAL (fndecl) = bl;
  TREE_USED (bl) = 1;

  bind = build3 (BIND_EXPR, void_type_node, BLOCK_VARS(bl),
		 NULL_TREE, bl);
  TREE_SIDE_EFFECTS (bind) = 1;
  /* Finalize the main function */
  BIND_EXPR_BODY (bind) = block;
  block = bind;
  DECL_SAVED_TREE (fndecl) = block;

  gimplify_function_tree (fndecl);
  cgraph_finalize_function (fndecl, false);

  vec_safe_push (lowered_decls, fndecl);
  gpy_vec_pop (context);
  pop_cfun ();

  return lowered_decls;
}

static
tree dot_pass_genFunction (gpy_dot_tree_t * dot,
			   dot_contextTable_t context,
			   const char * parentID)
{
  /* setup next context */
  gpy_hash_tab_t ctx;
  gpy_dd_hash_init_table (&ctx);
  gpy_vec_push (context, &ctx);

  gpy_dot_tree_t * pnode;
  tree params = NULL_TREE;
  for (pnode = DOT_lhs_TT (dot); pnode != NULL_DOT;
       pnode = DOT_CHAIN (pnode))
    chainon (params, tree_cons (NULL_TREE, gpy_object_type_ptr, NULL_TREE));
  chainon (params, tree_cons (NULL_TREE, void_type_node, NULL_TREE));

  tree fntype = build_function_type_list (void_type_node, params);
  tree ident =  dot_pass_concat_identifier (parentID,
					    DOT_IDENTIFIER_POINTER (DOT_FIELD (dot)));
  tree fndecl = dot_pass_genFndecl_Basic (ident, fntype);
  current_function_decl = fndecl;

  tree arglist = NULL_TREE;
  for (pnode = DOT_lhs_TT (dot); pnode != NULL_DOT;
       pnode = DOT_CHAIN (pnode))
    {
      const char * parmid = DOT_IDENTIFIER_POINTER (pnode);
      tree parm_decl = build_decl (BUILTINS_LOCATION, PARM_DECL,
				   get_identifier (parmid),
				   gpy_object_type_ptr);
      DECL_CONTEXT (parm_decl) = fndecl;
      DECL_ARG_TYPE (parm_decl) = gpy_object_type_ptr;
      TREE_READONLY (parm_decl) = 1;
      arglist = chainon (arglist, parm_decl);
      TREE_USED (parm_decl) = 1;

      gcc_assert (dot_pass_pushDecl (parm_decl, parmid, &ctx));
    }
  DECL_ARGUMENTS (fndecl) = arglist;

  tree block = alloc_stmt_list ();
  gpy_dot_tree_t * node;
  for (node = DOT_rhs_TT (dot); node != NULL_DOT;
       node = DOT_CHAIN (node))
    {
      if (DOT_T_FIELD (node) ==  D_D_EXPR)
	{
	  dot_pass_lowerExpr (node, context, &block);
	  continue;
	}
      switch (DOT_TYPE (node))
	{
	case D_PRINT_STMT:
	  dot_pass_genPrintStmt (node, &block, context);
	  break;

	case D_KEY_RETURN:
	  dot_pass_genReturnStmt (node, &block, context);
	  break;

	case D_STRUCT_CONDITIONAL:
	  dot_pass_genConditional (node, &block, context);
	  break;

	case D_STRUCT_WHILE:
	  dot_pass_genWhile (node, &block, context);
	  break;

	default:
	  error ("unhandled syntax within toplevel function!\n");
	  break;
	}
    }
  tree bind = NULL_TREE;
  tree bl = build_block (DECL_RESULT (fndecl), NULL_TREE, fndecl, NULL_TREE);
  DECL_INITIAL (fndecl) = bl;
  TREE_USED (bl) = 1;

  bind = build3 (BIND_EXPR, void_type_node, BLOCK_VARS(bl),
		 NULL_TREE, bl);
  TREE_SIDE_EFFECTS (bind) = 1;
  /* Finalize the main function */
  BIND_EXPR_BODY (bind) = block;
  block = bind;
  DECL_SAVED_TREE (fndecl) = block;

  gimplify_function_tree (fndecl);
  cgraph_finalize_function (fndecl, false);
  pop_cfun ();

  gpy_vec_pop (context);
  return fndecl;
}

static
void dot_pass_mainInitCheck (tree * block)
{
   // if we have already initilized we should just return...
  tree label_decl = build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				create_tmp_var_name ("LBIF"),
				void_type_node);
  tree label_expr = fold_build1_loc (UNKNOWN_LOCATION, LABEL_EXPR,
				     void_type_node, label_decl);

  tree endif_label_decl = build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				      create_tmp_var_name ("ENDIF"),
				      void_type_node);
  tree endif_label_expr = fold_build1_loc (UNKNOWN_LOCATION, LABEL_EXPR,
					   void_type_node, endif_label_decl);
  DECL_CONTEXT (endif_label_decl) = current_function_decl;
  DECL_CONTEXT (label_decl) = current_function_decl;

  tree conditional = fold_build2_loc (UNKNOWN_LOCATION, EQ_EXPR,
				      boolean_type_node,
				      moduleInit,
				      boolean_true_node);
  tree cond = build3_loc (UNKNOWN_LOCATION, COND_EXPR, void_type_node,
			  conditional,
			  build1 (GOTO_EXPR, void_type_node, label_decl),
			  build1 (GOTO_EXPR, void_type_node, endif_label_decl));

  append_to_statement_list (cond, block);
  append_to_statement_list (label_expr, block);
  append_to_statement_list (fold_build1_loc (UNKNOWN_LOCATION, RETURN_EXPR,
					     void_type_node, NULL_TREE),
			    block);
  append_to_statement_list (endif_label_expr, block);
  append_to_statement_list (build2 (MODIFY_EXPR, boolean_type_node,
				    moduleInit, boolean_true_node),
			    block);
}

static
void dot_pass_generic_TU (gpy_hash_tab_t * types,
			  vec<gpydot,va_gc> * decls,
			  vec<tree,va_gc> * generic)
{
  gpy_hash_tab_t toplvl, topnxt;
  gpy_dd_hash_init_table (&toplvl);
  gpy_dd_hash_init_table (&topnxt);

  gpy_vector_t context;
  memset (&context, 0, sizeof (gpy_vector_t));

  gpy_vec_push (&context, &toplvl);
  gpy_vec_push (&context, &topnxt);

  tree block = alloc_stmt_list ();
  tree module = dot_pass_getModuleType (GPY_current_module_name, types);
  dot_pass_setupContext (module, &context, generic, &block);

  tree fntype = build_function_type_list (void_type_node, NULL_TREE);
  tree ident =  dot_pass_concat_identifier (GPY_current_module_name,
					    "__main_start__");
  tree fndecl = dot_pass_genFndecl_Basic (ident, fntype);
  current_function_decl = fndecl;
  dot_pass_mainInitCheck (&block);
 
  int i;
  gpy_dot_tree_t * dot = NULL_DOT;
  for (i = 0; decls->iterate (i, &dot); ++i)
    {
      if (DOT_T_FIELD (dot) ==  D_D_EXPR)
	{
	  dot_pass_lowerExpr (dot, &context, &block);
	  continue;
	}

      switch (DOT_TYPE (dot))
        {
	case D_PRINT_STMT:
	  dot_pass_genPrintStmt (dot, &block, &context);
	  break;

	case D_STRUCT_CONDITIONAL:
	  dot_pass_genConditional (dot, &block, &context);
	  break;

	case D_STRUCT_WHILE:
	  dot_pass_genWhile (dot, &block, &context);
	  break;

	case D_STRUCT_FOR:
	  dot_pass_genFor (dot, &block, &context);
	  break;

	case D_KEY_RETURN:
	  error ("Return in toplevel context is invalid!\n");
	  break;

	case D_KEY_IMPORT:
	  dot_pass_genImport (dot, &block, &context);
	  break;

        case D_STRUCT_METHOD:
	  {
	    tree func = dot_pass_genFunction (dot, &context, GPY_current_module_name);
	    /* assign the function to the decl */
	    const char * funcid = DOT_IDENTIFIER_POINTER (DOT_FIELD (dot));
	    tree funcdecl = dot_pass_lookupDecl (&context, funcid);
	    int n = 0;
	    gpy_dot_tree_t * pnode;
	    for (pnode = DOT_lhs_TT (dot); pnode != NULL_DOT;
		 pnode = DOT_CHAIN (pnode))
	      n++;
	    tree nargs = build_int_cst (integer_type_node, n);
	    tree str = gpy_dot_type_const_string_tree (funcid);
	    tree fold_functor = GPY_RR_fold_func_decl (build_fold_addr_expr (str),
						       func, nargs);
	    tree decl_func = build_decl (UNKNOWN_LOCATION, VAR_DECL,
					 create_tmp_var_name ("FN"),
					 gpy_object_type_ptr);
	    append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					      decl_func, fold_functor),
				      &block);
	    append_to_statement_list (build2 (MODIFY_EXPR, gpy_object_type_ptr,
					      build_fold_indirect_ref (funcdecl),
					      decl_func),
				      &block);
	    vec_safe_push (generic, func);
	  }
	  break;

	case D_STRUCT_CLASS:
	  {
	    char * classID =  dot_pass_concat (GPY_current_module_name,
					       DOT_IDENTIFIER_POINTER (DOT_FIELD (dot)));
	    tree classType = dot_pass_getModuleType (classID, types);
	    vec<tree,va_gc> * cdecls = dot_pass_genClass (dot, &context, classType,
							  GPY_current_module_name);
	    GPY_VEC_stmts_append (tree, generic, cdecls);
	    tree class_decl_ptr = dot_pass_lookupDecl (&context,
						       DOT_IDENTIFIER_POINTER (DOT_FIELD (dot)));
	    dot_pass_genWalkClass (&block, classType, class_decl_ptr, cdecls);
	  }
	  break;

	default:
	  fatal_error ("unhandled tree!\n");
	  break;
	}
    }

  tree entry_decl = NULL_TREE;
  if (GPY_OPT_gen_main)
    {
      tree entry_fntype = gpy_unsigned_char_ptr;
      entry_decl = build_decl (BUILTINS_LOCATION, VAR_DECL,
			       get_identifier (GPY_RR_entry),
			       entry_fntype);
      TREE_STATIC (entry_decl) = 1;
      TREE_PUBLIC (entry_decl) = 1;
      TREE_USED (entry_decl) = 1;
      DECL_ARTIFICIAL (entry_decl) = 1;
      DECL_EXTERNAL (entry_decl) = 0;
      DECL_INITIAL (entry_decl) = build_fold_addr_expr (fndecl);
      rest_of_decl_compilation (entry_decl, 1, 0);
    }

  tree bind = NULL_TREE;
  tree bl = build_block (DECL_RESULT (fndecl), NULL_TREE, fndecl, NULL_TREE);
  DECL_INITIAL (fndecl) = bl;
  TREE_USED (bl) = 1;

  bind = build3 (BIND_EXPR, void_type_node, BLOCK_VARS(bl),
		 NULL_TREE, bl);
  TREE_SIDE_EFFECTS (bind) = 1;
  /* Finalize the main function */
  BIND_EXPR_BODY (bind) = block;
  block = bind;
  DECL_SAVED_TREE (fndecl) = block;

  gimplify_function_tree (fndecl);
  cgraph_finalize_function (fndecl, false);

  pop_cfun ();

  /* Here we need to write out export data to object */
  char * exportFile = (char *) alloca (512);
  const char * main_FNDECL = IDENTIFIER_POINTER (DECL_NAME (fndecl));
  snprintf (exportFile, 511, "%s.export.gpyx", GPY_current_module_name);
  gpy_writeExport (exportFile, (bool) GPY_OPT_gen_main,
		   GPY_current_module_name, main_FNDECL);

  vec_safe_push (generic, fndecl);
  if (GPY_OPT_gen_main)
    vec_safe_push (generic, entry_decl);
}

vec<tree,va_gc> * dot_pass_genericify (vec<tree,va_gc> * modules,
				       vec<gpydot,va_gc> * decls)
{
  vec<tree,va_gc> * retval;
  vec_alloc (retval, 0);

  gpy_hash_tab_t types;
  gpy_dd_hash_init_table (&types);

  int i;
  tree type = NULL_TREE;
  for (i = 0; modules->iterate (i, &type); ++i)
    {
      gpy_hashval_t h = gpy_dd_hash_string (IDENTIFIER_POINTER (TYPE_NAME(type)));
      void ** e = gpy_dd_hash_insert (h, type, &types);
      if (e)
        fatal_error ("module <%s> is already defined!\n",
		     IDENTIFIER_POINTER (DECL_NAME (type)));
    }
  dot_pass_generic_TU (&types, decls, retval);

  if (types.array)
    free (types.array);
  return retval;
}
