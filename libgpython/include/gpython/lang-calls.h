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

#ifndef __GCC_GPY_LANG_CALLS_H__
#define __GCC_GPY_LANG_CALLS_H__

extern gpy_object_t NULL_OBJECT_REF;

typedef void (*gpy_ffiCall0)(void);
typedef void (*gpy_ffiCall1)(gpy_object_t *);
typedef void (*gpy_ffiCall2)(gpy_object_t *, gpy_object_t *);
typedef void (*gpy_ffiCall3)(gpy_object_t *, gpy_object_t *, gpy_object_t *);
typedef void (*gpy_ffiCall4)(gpy_object_t *, gpy_object_t *, gpy_object_t *, gpy_object_t *);
typedef void (*gpy_ffiCall5)(gpy_object_t *, gpy_object_t *, gpy_object_t *, gpy_object_t *, gpy_object_t *);

extern void gpy_rr_init_runtime (void);
extern gpy_object_t * gpy_rr_fold_integer (int);

extern bool gpy_args_check_fmt (gpy_object_t *, const char *);

extern char ** gpy_args_lit_parse_sarray (gpy_object_t *);
extern int gpy_args_lit_parse_int (gpy_object_t *);
extern char * gpy_args_lit_parse_string (gpy_object_t * );
extern unsigned char * gpy_args_lit_parse_pointer (gpy_object_t *);
extern gpy_object_attrib_t ** gpy_args_lit_parse_attrib_table (gpy_object_t *);
extern gpy_object_t ** gpy_args_lit_parse_vec (gpy_object_t *);

extern gpy_object_t * gpy_create_object_state (gpy_typedef_t *, void *);
extern gpy_object_t * gpy_create_object_decl (gpy_typedef_t *, void *);
extern unsigned char * gpy_object_staticmethod_getaddr (gpy_object_t *);
extern unsigned char * gpy_object_classmethod_getaddr (gpy_object_t *);

extern void gpy_wrap_builtins (gpy_typedef_t * const, size_t);

extern int gpy_obj_integer_getInt (gpy_object_t *);

extern void gpy_obj_integer_mod_init (gpy_vector_t * const);
extern void gpy_obj_staticmethod_mod_init (gpy_vector_t * const);
extern void gpy_obj_func_mod_init (gpy_vector_t * const);
extern void gpy_obj_list_mod_init (gpy_vector_t * const);
extern void gpy_obj_module_mod_init (gpy_vector_t * const);
extern void gpy_obj_dict_mod_init (gpy_vector_t * const);
extern void gpy_obj_class_mod_init (gpy_vector_t * const);
extern void gpy_obj_classmethod_mod_init (gpy_vector_t * const);
extern void gpy_obj_string_mod_init (gpy_vector_t * const);

/* builtins... */
extern void gpy_builtin_sys_init (void);

extern gpy_object_t * gpy_rr_fold_staticmethod_decl (const char *, unsigned char *, int);
extern gpy_object_t * gpy_rr_fold_classmethod_decl (const char *, unsigned char *, int);
extern gpy_object_t ** gpy_rr_eval_attrib_reference (gpy_object_t *, const char *);

extern void gpy_rr_eval_print (int, int, ...);
extern void gpy_rr_foldImport (gpy_object_t **, const char *);
extern gpy_object_t * gpy_rr_fold_string (const char *);

extern void gpy_rr_cleanup_final (void);
extern int gpy_rr_extendRRStack (int, const char *, char **);

#endif // __GCC_GPY_LANG_CALLS_H__
