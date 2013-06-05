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

#ifndef __GCC_DOT_TREE_H__
#define __GCC_DOT_TREE_H__

extern vec<tree,va_gc> * gpy_builtin_types_vec;

#define gpy_object_type_ptr        (*gpy_builtin_types_vec) [0]
#define gpy_object_type_ptr_ptr    (*gpy_builtin_types_vec) [1]
#define gpy_const_char_ptr         (*gpy_builtin_types_vec) [2]
#define gpy_vector_type            (*gpy_builtin_types_vec) [3]
#define gpy_vector_type_ptr        (*gpy_builtin_types_vec) [4]
#define gpy_vector_type_ptr_ptr    (*gpy_builtin_types_vec) [5]
#define gpy_attrib_type            (*gpy_builtin_types_vec) [6]
#define gpy_attrib_type_ptr        (*gpy_builtin_types_vec) [7]
#define gpy_attrib_type_ptr_ptr    (*gpy_builtin_types_vec) [8]
#define gpy_unsigned_char_ptr      build_pointer_type (unsigned_char_type_node)
#define gpy_unsigned_char_ptr_ptr  build_pointer_type (gpy_unsigned_char_ptr)

extern char * dot_pass_concat (const char *, const char *);
#define dot_pass_concat_identifier(X_, Y_)	\
  get_identifier (dot_pass_concat (X_, Y_))

/* Appends vector y on x */
#define GPY_VEC_stmts_append(T,x,y)			\
  do {							\
    int x_; T t_ = NULL_TREE;				\
    for (x_ = 0; y->iterate (x_, &t_); ++x_)		\
      vec_safe_push (x, t_);				\
  } while (0);

/* Passes */
typedef vec<gpydot,va_gc> * dot_table;
typedef vec<tree,va_gc> * tree_table;

extern vec<gpydot,va_gc> * dot_pass_check1 (vec<gpydot,va_gc> *);
extern vec<gpydot,va_gc> * dot_pass_const_fold (vec<gpydot,va_gc> *);
extern vec<gpydot,va_gc> * dot_pass_translate (vec<gpydot,va_gc> *);
extern vec<gpydot,va_gc> * dot_pass_PrettyPrint (vec<gpydot,va_gc> *);
extern vec<tree,va_gc> * dot_pass_GenTypes (vec<gpydot,va_gc> *);
extern vec<tree,va_gc> * dot_pass_genericify (vec<tree,va_gc> *, vec<gpydot,va_gc> *);

extern void dot_pass_gdotPrettyPrint (vec<gpydot,va_gc> * decls);
extern void dot_pass_pretty_PrintTypes (vec<tree,va_gc> *);
extern void dot_pass_manager_WriteGlobals (void);
extern void dot_pass_manager_ProcessDecl (gpy_dot_tree_t * const);
extern void gpy_dot_types_init (void);

// gpy-data-export.c
struct gpy_dataExport {
  bool main;
  char * entry;
  char * module;
} ;
extern void gpy_import_read (const char *);
extern gpy_dataExport * gpy_readExportData (const char *);
extern void gpy_pushExportData (struct gpy_dataExport *);
extern void gpy_writeExport (const char *, bool, const char *, const char *);

#endif //__GCC_DOT_TREE_H__
