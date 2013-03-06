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

#ifndef __GCC_PY_TREE_H__
#define __GCC_PY_TREE_H__

extern VEC(tree,gc) * gpy_builtin_types_vec;

#define gpy_object_type_ptr        VEC_index (tree, gpy_builtin_types_vec, 0)
#define gpy_object_type_ptr_ptr    VEC_index (tree, gpy_builtin_types_vec, 1)
#define gpy_const_char_ptr         VEC_index (tree, gpy_builtin_types_vec, 2)
#define gpy_vector_type            VEC_index (tree, gpy_builtin_types_vec, 3)
#define gpy_vector_type_ptr        VEC_index (tree, gpy_builtin_types_vec, 4)
#define gpy_vector_type_ptr_ptr    VEC_index (tree, gpy_builtin_types_vec, 5)
#define gpy_attrib_type            VEC_index (tree, gpy_builtin_types_vec, 6)
#define gpy_attrib_type_ptr        VEC_index (tree, gpy_builtin_types_vec, 7)
#define gpy_attrib_type_ptr_ptr    VEC_index (tree, gpy_builtin_types_vec, 8)
#define gpy_unsigned_char_ptr      build_pointer_type (unsigned_char_type_node)
#define gpy_unsigned_char_ptr_ptr  build_pointer_type (gpy_unsigned_char_ptr)

extern char * dot_pass_concat (const char *, const char *);
#define dot_pass_concat_identifier(X_, Y_)	\
  get_identifier (dot_pass_concat (X_, Y_))

/* Appends vector y on x */
#define GPY_VEC_stmts_append(T,x,y)			\
  do {							\
    int x_; T t_ = NULL_TREE;				\
    for (x_ = 0; VEC_iterate (T,y,x_,t_); ++x_)		\
      {							\
        VEC_safe_push (T, gc, x, t_);			\
      }							\
  } while (0);

/* Passes */
extern VEC(gpydot,gc) * dot_pass_check1 (VEC(gpydot,gc) *);
extern VEC(gpydot,gc) * dot_pass_const_fold (VEC(gpydot,gc) *);
extern VEC(gpydot,gc) * dot_pass_translate (VEC(gpydot,gc) *);
extern VEC(gpydot,gc) * dot_pass_PrettyPrint (VEC(gpydot,gc) *);
extern VEC(tree,gc) * dot_pass_GenTypes (VEC(gpydot,gc) *);
extern VEC(tree,gc) * dot_pass_genericify (VEC(tree,gc) *, VEC(gpydot,gc) *);

extern void dot_pass_gdotPrettyPrint (VEC(gpydot,gc) * decls);
extern void dot_pass_pretty_PrintTypes (VEC(tree,gc) *);
extern void dot_pass_manager_WriteGlobals (void);
extern void dot_pass_manager_ProcessDecl (gpy_dot_tree_t * const);
extern void gpy_dot_types_init (void);

// gpy-data-export.c
extern void gpy_write_export_data (const char *, unsigned int);

#endif //__PYGCC_PY_TREE_H__
