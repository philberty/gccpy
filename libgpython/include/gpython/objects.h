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

#ifndef __GCC_OBJECTS_H__
#define __GCC_OBJECTS_H__

enum GPY_LIT_T {
  TYPE_INTEGER,
  TYPE_STRING,
  TYPE_BOOLEAN,
  TYPE_FLOAT,
  TYPE_ADDR,
  TYPE_ATTRIB_L,
  TYPE_VEC,
  TYPE_STR_ARRY,
  TYPE_NONE,
};

enum GPY_OBJECT_T {
  TYPE_OBJECT_STATE,
  TYPE_OBJECT_DECL,
  TYPE_OBJECT_LIT,
  TYPE_NULL,
};

typedef struct gpy_rr_literal_t {
  enum GPY_LIT_T type;
  union {
    int integer;
    float decimal;
    char * string;
    bool boolean;
    unsigned char * addr;
    char ** sarray;
    struct gpy_object_attrib_t ** attribs;
    struct gpy_object_t ** vec;
  } literal ;
} gpy_literal_t ;

typedef struct gpy_rr_object_state_t {
  char * identifier;
  int ref_count;
  void * state;
  struct gpy_typedef_t * definition;
} gpy_object_state_t ;

typedef struct gpy_object_t {
  enum GPY_OBJECT_T T;
  union {
    gpy_object_state_t object_state;
    gpy_literal_t literal;
  } o ;
} gpy_object_t ;

typedef void (*staticmethod_fndecl)(gpy_object_t **);
typedef void (*classmethod_fndecl) (gpy_object_t *, gpy_object_t **);
typedef gpy_object_t * (*binary_op)(gpy_object_t *, gpy_object_t *);
typedef struct gpy_number_prot_t {
  binary_op n_add; // x + y
  binary_op n_sub; // x - y
  binary_op n_div; // x / y
  binary_op n_mul; // x * y
  binary_op n_pow; // x ^ y
  binary_op n_let; // x < y
  binary_op n_lee; // x <= y
  binary_op n_get; // x > y
  binary_op n_gee; // x >= y
  binary_op n_eee; // x == y
  binary_op n_nee; // x != y
  binary_op n_orr; // x || y
  binary_op n_and; // x && y
} gpy_num_prot_t ;

enum GPY_ATTRT {
  GPY_GCCPY,
  GPY_CATTR,
  GPY_MOD,
  GPY_EMPTY,
};

typedef struct gpy_object_attrib_t {
  const char * identifier;
  enum GPY_ATTRT T;
  unsigned int offset;
  gpy_object_t * addr;
} gpy_object_attrib_t;

typedef gpy_object_t * (*GPY_CFUNC)(gpy_object_t *, gpy_object_t **);
typedef struct gpy_builtinAttribs_t {
  const char * identifier;
  int nargs;
  GPY_CFUNC addr;
} gpy_builtinAttribs_t ;

typedef struct gpy_typedef_t {
  const char * identifier;
  size_t state_size;
  gpy_object_t * (*tp_new)(struct gpy_typedef_t *, gpy_object_t *);
  void (*tp_dealloc)(gpy_object_t *);
  void (*tp_print)(gpy_object_t * , FILE *, bool);
  gpy_object_t * (*tp_call) (gpy_object_t *, gpy_object_t **);
  int (*tp_nparms) (gpy_object_t *);
  bool (*tp_eval_boolean) (gpy_object_t *);
  struct gpy_number_prot_t * binary_protocol;
  struct gpy_object_attrib_t ** members_defintion;
  struct gpy_builtinAttribs_t * builtins;
  gpy_object_t * (*tp_slice)(gpy_object_t *, gpy_object_t *);
  gpy_object_t ** (*tp_ref_slice)(gpy_object_t *, gpy_object_t *);
} gpy_typedef_t ;

#define NULL_OBJ_STATE (gpy_object_state_t *) NULL
#define NULL_OBJECT (gpy_object_t *) NULL

extern gpy_object_t NULL_OBJECT_REF;
#define OBJECT_STATE(x_)       x_->o.object_state
#define OBJECT_DEFINITION(x_)  OBJECT_STATE(x_).definition

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

#endif //__GCC_OBJECTS_H__
