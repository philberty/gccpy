
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


#ifndef __GCC_GPY_RUNTIME_H__
#define __GCC_GPY_RUNTIME_H__

// identifiers for all major runtime memory components
#define GPY_RR_stack_ptr           "__GPY_RR_STACK_PTR"
#define GPY_RR_module_stack        "__GPY_GLOBL_MOD_STACK"
#define GPY_RR_entry               "__GPY_entry"

/* return a const string tree */
extern tree gpy_dot_type_const_string_tree (const char *);

/* Extends shizzle */
extern tree GPY_RR_initRRStack (tree, tree, tree);

/* Fold attribute info into an attribute type */
extern tree GPY_RR_fold_attrib (tree, tree, tree, tree);

/* Requires the first tree in the arguments to be an integer_type_node of the number of arguments */
extern tree GPY_RR_fold_attrib_list (vec<tree,va_gc> *);

/* Fold class data into class object args = <attrib list><size><identifier> */
extern tree GPY_RR_fold_class_decl (tree, tree, tree);

/* Fold func into decl <identifier><fndcel><nargs> */
extern tree GPY_RR_fold_func_decl (tree, tree, tree);

/* Fold func into decl <identifier><fndcel><nargs> */
extern tree GPY_RR_fold_classmethod_decl (tree, tree, tree);

/* Fold integer into Int object  via Int (x) */
extern tree GPY_RR_fold_integer (tree);

/* Fold enclosure list via List (1,2,3,...) */
extern tree GPY_RR_fold_encList (vec<tree,va_gc> *);

/* incr the refrence count on the object  */
extern tree GPY_RR_incr_ref_count (tree);

/* decr the refrence count on the object  */
extern tree GPY_RR_decr_ref_count (tree);

/* first index is the fd (1/0) 2nd idx is number of elements and finaly va_list of args */
extern tree GPY_RR_eval_print (vec<tree,va_gc> *);

/* Evaluate the operation op of the 2 objects x and y and return result */
extern tree GPY_RR_eval_expression (tree, tree, tree);

/* Eval base.attrib */
extern tree GPY_RR_fold_attrib_ref (tree, tree);

/* Eval call */
extern tree GPY_RR_fold_call (vec<tree,va_gc> *);

/* Eval arg */
extern tree GPY_RR_fold_argument (tree, tree);

/* Eval result */
extern tree GPY_RR_eval_boolean (tree);

#endif //__GCC_GPY_RUNTIME_H__


