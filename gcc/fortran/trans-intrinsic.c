/* Intrinsic translation
   Copyright (C) 2002-2013 Free Software Foundation, Inc.
   Contributed by Paul Brook <paul@nowt.org>
   and Steven Bosscher <s.bosscher@student.tudelft.nl>

This file is part of GCC.

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

/* trans-intrinsic.c-- generate GENERIC trees for calls to intrinsics.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"		/* For UNITS_PER_WORD.  */
#include "tree.h"
#include "ggc.h"
#include "diagnostic-core.h"	/* For internal_error.  */
#include "toplev.h"	/* For rest_of_decl_compilation.  */
#include "flags.h"
#include "gfortran.h"
#include "arith.h"
#include "intrinsic.h"
#include "trans.h"
#include "trans-const.h"
#include "trans-types.h"
#include "trans-array.h"
/* Only for gfc_trans_assign and gfc_trans_pointer_assign.  */
#include "trans-stmt.h"

/* This maps Fortran intrinsic math functions to external library or GCC
   builtin functions.  */
typedef struct GTY(()) gfc_intrinsic_map_t {
  /* The explicit enum is required to work around inadequacies in the
     garbage collection/gengtype parsing mechanism.  */
  enum gfc_isym_id id;

  /* Enum value from the "language-independent", aka C-centric, part
     of gcc, or END_BUILTINS of no such value set.  */
  enum built_in_function float_built_in;
  enum built_in_function double_built_in;
  enum built_in_function long_double_built_in;
  enum built_in_function complex_float_built_in;
  enum built_in_function complex_double_built_in;
  enum built_in_function complex_long_double_built_in;

  /* True if the naming pattern is to prepend "c" for complex and
     append "f" for kind=4.  False if the naming pattern is to
     prepend "_gfortran_" and append "[rc](4|8|10|16)".  */
  bool libm_name;

  /* True if a complex version of the function exists.  */
  bool complex_available;

  /* True if the function should be marked const.  */
  bool is_constant;

  /* The base library name of this function.  */
  const char *name;

  /* Cache decls created for the various operand types.  */
  tree real4_decl;
  tree real8_decl;
  tree real10_decl;
  tree real16_decl;
  tree complex4_decl;
  tree complex8_decl;
  tree complex10_decl;
  tree complex16_decl;
}
gfc_intrinsic_map_t;

/* ??? The NARGS==1 hack here is based on the fact that (c99 at least)
   defines complex variants of all of the entries in mathbuiltins.def
   except for atan2.  */
#define DEFINE_MATH_BUILTIN(ID, NAME, ARGTYPE) \
  { GFC_ISYM_ ## ID, BUILT_IN_ ## ID ## F, BUILT_IN_ ## ID, \
    BUILT_IN_ ## ID ## L, END_BUILTINS, END_BUILTINS, END_BUILTINS, \
    true, false, true, NAME, NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE, \
    NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE},

#define DEFINE_MATH_BUILTIN_C(ID, NAME, ARGTYPE) \
  { GFC_ISYM_ ## ID, BUILT_IN_ ## ID ## F, BUILT_IN_ ## ID, \
    BUILT_IN_ ## ID ## L, BUILT_IN_C ## ID ## F, BUILT_IN_C ## ID, \
    BUILT_IN_C ## ID ## L, true, true, true, NAME, NULL_TREE, NULL_TREE, \
    NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE},

#define LIB_FUNCTION(ID, NAME, HAVE_COMPLEX) \
  { GFC_ISYM_ ## ID, END_BUILTINS, END_BUILTINS, END_BUILTINS, \
    END_BUILTINS, END_BUILTINS, END_BUILTINS, \
    false, HAVE_COMPLEX, true, NAME, NULL_TREE, NULL_TREE, NULL_TREE, \
    NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE }

#define OTHER_BUILTIN(ID, NAME, TYPE, CONST) \
  { GFC_ISYM_NONE, BUILT_IN_ ## ID ## F, BUILT_IN_ ## ID, \
    BUILT_IN_ ## ID ## L, END_BUILTINS, END_BUILTINS, END_BUILTINS, \
    true, false, CONST, NAME, NULL_TREE, NULL_TREE, \
    NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE},

static GTY(()) gfc_intrinsic_map_t gfc_intrinsic_map[] =
{
  /* Functions built into gcc itself (DEFINE_MATH_BUILTIN and
     DEFINE_MATH_BUILTIN_C), then the built-ins that don't correspond
     to any GFC_ISYM id directly, which use the OTHER_BUILTIN macro.  */
#include "mathbuiltins.def"

  /* Functions in libgfortran.  */
  LIB_FUNCTION (ERFC_SCALED, "erfc_scaled", false),

  /* End the list.  */
  LIB_FUNCTION (NONE, NULL, false)

};
#undef OTHER_BUILTIN
#undef LIB_FUNCTION
#undef DEFINE_MATH_BUILTIN
#undef DEFINE_MATH_BUILTIN_C


enum rounding_mode { RND_ROUND, RND_TRUNC, RND_CEIL, RND_FLOOR };


/* Find the correct variant of a given builtin from its argument.  */
static tree
builtin_decl_for_precision (enum built_in_function base_built_in,
			    int precision)
{
  enum built_in_function i = END_BUILTINS;

  gfc_intrinsic_map_t *m;
  for (m = gfc_intrinsic_map; m->double_built_in != base_built_in ; m++)
    ;

  if (precision == TYPE_PRECISION (float_type_node))
    i = m->float_built_in;
  else if (precision == TYPE_PRECISION (double_type_node))
    i = m->double_built_in;
  else if (precision == TYPE_PRECISION (long_double_type_node))
    i = m->long_double_built_in;
  else if (precision == TYPE_PRECISION (float128_type_node))
    {
      /* Special treatment, because it is not exactly a built-in, but
	 a library function.  */
      return m->real16_decl;
    }

  return (i == END_BUILTINS ? NULL_TREE : builtin_decl_explicit (i));
}


tree
gfc_builtin_decl_for_float_kind (enum built_in_function double_built_in,
				 int kind)
{
  int i = gfc_validate_kind (BT_REAL, kind, false);

  if (gfc_real_kinds[i].c_float128)
    {
      /* For __float128, the story is a bit different, because we return
	 a decl to a library function rather than a built-in.  */
      gfc_intrinsic_map_t *m; 
      for (m = gfc_intrinsic_map; m->double_built_in != double_built_in ; m++)
	;

      return m->real16_decl;
    }

  return builtin_decl_for_precision (double_built_in,
				     gfc_real_kinds[i].mode_precision);
}


/* Evaluate the arguments to an intrinsic function.  The value
   of NARGS may be less than the actual number of arguments in EXPR
   to allow optional "KIND" arguments that are not included in the
   generated code to be ignored.  */

static void
gfc_conv_intrinsic_function_args (gfc_se *se, gfc_expr *expr,
				  tree *argarray, int nargs)
{
  gfc_actual_arglist *actual;
  gfc_expr *e;
  gfc_intrinsic_arg  *formal;
  gfc_se argse;
  int curr_arg;

  formal = expr->value.function.isym->formal;
  actual = expr->value.function.actual;

   for (curr_arg = 0; curr_arg < nargs; curr_arg++,
	actual = actual->next,
	formal = formal ? formal->next : NULL)
    {
      gcc_assert (actual);
      e = actual->expr;
      /* Skip omitted optional arguments.  */
      if (!e)
	{
	  --curr_arg;
	  continue;
	}

      /* Evaluate the parameter.  This will substitute scalarized
         references automatically.  */
      gfc_init_se (&argse, se);

      if (e->ts.type == BT_CHARACTER)
	{
	  gfc_conv_expr (&argse, e);
	  gfc_conv_string_parameter (&argse);
          argarray[curr_arg++] = argse.string_length;
	  gcc_assert (curr_arg < nargs);
	}
      else
        gfc_conv_expr_val (&argse, e);

      /* If an optional argument is itself an optional dummy argument,
	 check its presence and substitute a null if absent.  */
      if (e->expr_type == EXPR_VARIABLE
	    && e->symtree->n.sym->attr.optional
	    && formal
	    && formal->optional)
	gfc_conv_missing_dummy (&argse, e, formal->ts, 0);

      gfc_add_block_to_block (&se->pre, &argse.pre);
      gfc_add_block_to_block (&se->post, &argse.post);
      argarray[curr_arg] = argse.expr;
    }
}

/* Count the number of actual arguments to the intrinsic function EXPR
   including any "hidden" string length arguments.  */

static unsigned int
gfc_intrinsic_argument_list_length (gfc_expr *expr)
{
  int n = 0;
  gfc_actual_arglist *actual;

  for (actual = expr->value.function.actual; actual; actual = actual->next)
    {
      if (!actual->expr)
	continue;

      if (actual->expr->ts.type == BT_CHARACTER)
	n += 2;
      else
	n++;
    }

  return n;
}


/* Conversions between different types are output by the frontend as
   intrinsic functions.  We implement these directly with inline code.  */

static void
gfc_conv_intrinsic_conversion (gfc_se * se, gfc_expr * expr)
{
  tree type;
  tree *args;
  int nargs;

  nargs = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, nargs);

  /* Evaluate all the arguments passed. Whilst we're only interested in the 
     first one here, there are other parts of the front-end that assume this 
     and will trigger an ICE if it's not the case.  */
  type = gfc_typenode_for_spec (&expr->ts);
  gcc_assert (expr->value.function.actual->expr);
  gfc_conv_intrinsic_function_args (se, expr, args, nargs);

  /* Conversion between character kinds involves a call to a library
     function.  */
  if (expr->ts.type == BT_CHARACTER)
    {
      tree fndecl, var, addr, tmp;

      if (expr->ts.kind == 1
	  && expr->value.function.actual->expr->ts.kind == 4)
	fndecl = gfor_fndecl_convert_char4_to_char1;
      else if (expr->ts.kind == 4
	       && expr->value.function.actual->expr->ts.kind == 1)
	fndecl = gfor_fndecl_convert_char1_to_char4;
      else
	gcc_unreachable ();

      /* Create the variable storing the converted value.  */
      type = gfc_get_pchar_type (expr->ts.kind);
      var = gfc_create_var (type, "str");
      addr = gfc_build_addr_expr (build_pointer_type (type), var);

      /* Call the library function that will perform the conversion.  */
      gcc_assert (nargs >= 2);
      tmp = build_call_expr_loc (input_location,
			     fndecl, 3, addr, args[0], args[1]);
      gfc_add_expr_to_block (&se->pre, tmp);

      /* Free the temporary afterwards.  */
      tmp = gfc_call_free (var);
      gfc_add_expr_to_block (&se->post, tmp);

      se->expr = var;
      se->string_length = args[0];

      return;
    }

  /* Conversion from complex to non-complex involves taking the real
     component of the value.  */
  if (TREE_CODE (TREE_TYPE (args[0])) == COMPLEX_TYPE
      && expr->ts.type != BT_COMPLEX)
    {
      tree artype;

      artype = TREE_TYPE (TREE_TYPE (args[0]));
      args[0] = fold_build1_loc (input_location, REALPART_EXPR, artype,
				 args[0]);
    }

  se->expr = convert (type, args[0]);
}

/* This is needed because the gcc backend only implements
   FIX_TRUNC_EXPR, which is the same as INT() in Fortran.
   FLOOR(x) = INT(x) <= x ? INT(x) : INT(x) - 1
   Similarly for CEILING.  */

static tree
build_fixbound_expr (stmtblock_t * pblock, tree arg, tree type, int up)
{
  tree tmp;
  tree cond;
  tree argtype;
  tree intval;

  argtype = TREE_TYPE (arg);
  arg = gfc_evaluate_now (arg, pblock);

  intval = convert (type, arg);
  intval = gfc_evaluate_now (intval, pblock);

  tmp = convert (argtype, intval);
  cond = fold_build2_loc (input_location, up ? GE_EXPR : LE_EXPR,
			  boolean_type_node, tmp, arg);

  tmp = fold_build2_loc (input_location, up ? PLUS_EXPR : MINUS_EXPR, type,
			 intval, build_int_cst (type, 1));
  tmp = fold_build3_loc (input_location, COND_EXPR, type, cond, intval, tmp);
  return tmp;
}


/* Round to nearest integer, away from zero.  */

static tree
build_round_expr (tree arg, tree restype)
{
  tree argtype;
  tree fn;
  int argprec, resprec;

  argtype = TREE_TYPE (arg);
  argprec = TYPE_PRECISION (argtype);
  resprec = TYPE_PRECISION (restype);

  /* Depending on the type of the result, choose the int intrinsic
     (iround, available only as a builtin, therefore cannot use it for
     __float128), long int intrinsic (lround family) or long long
     intrinsic (llround).  We might also need to convert the result
     afterwards.  */
  if (resprec <= INT_TYPE_SIZE && argprec <= LONG_DOUBLE_TYPE_SIZE)
    fn = builtin_decl_for_precision (BUILT_IN_IROUND, argprec);
  else if (resprec <= LONG_TYPE_SIZE)
    fn = builtin_decl_for_precision (BUILT_IN_LROUND, argprec);
  else if (resprec <= LONG_LONG_TYPE_SIZE)
    fn = builtin_decl_for_precision (BUILT_IN_LLROUND, argprec);
  else
    gcc_unreachable ();

  return fold_convert (restype, build_call_expr_loc (input_location,
						 fn, 1, arg));
}


/* Convert a real to an integer using a specific rounding mode.
   Ideally we would just build the corresponding GENERIC node,
   however the RTL expander only actually supports FIX_TRUNC_EXPR.  */

static tree
build_fix_expr (stmtblock_t * pblock, tree arg, tree type,
               enum rounding_mode op)
{
  switch (op)
    {
    case RND_FLOOR:
      return build_fixbound_expr (pblock, arg, type, 0);
      break;

    case RND_CEIL:
      return build_fixbound_expr (pblock, arg, type, 1);
      break;

    case RND_ROUND:
      return build_round_expr (arg, type);
      break;

    case RND_TRUNC:
      return fold_build1_loc (input_location, FIX_TRUNC_EXPR, type, arg);
      break;

    default:
      gcc_unreachable ();
    }
}


/* Round a real value using the specified rounding mode.
   We use a temporary integer of that same kind size as the result.
   Values larger than those that can be represented by this kind are
   unchanged, as they will not be accurate enough to represent the
   rounding.
    huge = HUGE (KIND (a))
    aint (a) = ((a > huge) || (a < -huge)) ? a : (real)(int)a
   */

static void
gfc_conv_intrinsic_aint (gfc_se * se, gfc_expr * expr, enum rounding_mode op)
{
  tree type;
  tree itype;
  tree arg[2];
  tree tmp;
  tree cond;
  tree decl;
  mpfr_t huge;
  int n, nargs;
  int kind;

  kind = expr->ts.kind;
  nargs = gfc_intrinsic_argument_list_length (expr);

  decl = NULL_TREE;
  /* We have builtin functions for some cases.  */
  switch (op)
    {
    case RND_ROUND:
      decl = gfc_builtin_decl_for_float_kind (BUILT_IN_ROUND, kind);
      break;

    case RND_TRUNC:
      decl = gfc_builtin_decl_for_float_kind (BUILT_IN_TRUNC, kind);
      break;

    default:
      gcc_unreachable ();
    }

  /* Evaluate the argument.  */
  gcc_assert (expr->value.function.actual->expr);
  gfc_conv_intrinsic_function_args (se, expr, arg, nargs);

  /* Use a builtin function if one exists.  */
  if (decl != NULL_TREE)
    {
      se->expr = build_call_expr_loc (input_location, decl, 1, arg[0]);
      return;
    }

  /* This code is probably redundant, but we'll keep it lying around just
     in case.  */
  type = gfc_typenode_for_spec (&expr->ts);
  arg[0] = gfc_evaluate_now (arg[0], &se->pre);

  /* Test if the value is too large to handle sensibly.  */
  gfc_set_model_kind (kind);
  mpfr_init (huge);
  n = gfc_validate_kind (BT_INTEGER, kind, false);
  mpfr_set_z (huge, gfc_integer_kinds[n].huge, GFC_RND_MODE);
  tmp = gfc_conv_mpfr_to_tree (huge, kind, 0);
  cond = fold_build2_loc (input_location, LT_EXPR, boolean_type_node, arg[0],
			  tmp);

  mpfr_neg (huge, huge, GFC_RND_MODE);
  tmp = gfc_conv_mpfr_to_tree (huge, kind, 0);
  tmp = fold_build2_loc (input_location, GT_EXPR, boolean_type_node, arg[0],
			 tmp);
  cond = fold_build2_loc (input_location, TRUTH_AND_EXPR, boolean_type_node,
			  cond, tmp);
  itype = gfc_get_int_type (kind);

  tmp = build_fix_expr (&se->pre, arg[0], itype, op);
  tmp = convert (type, tmp);
  se->expr = fold_build3_loc (input_location, COND_EXPR, type, cond, tmp,
			      arg[0]);
  mpfr_clear (huge);
}


/* Convert to an integer using the specified rounding mode.  */

static void
gfc_conv_intrinsic_int (gfc_se * se, gfc_expr * expr, enum rounding_mode op)
{
  tree type;
  tree *args;
  int nargs;

  nargs = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, nargs);

  /* Evaluate the argument, we process all arguments even though we only 
     use the first one for code generation purposes.  */
  type = gfc_typenode_for_spec (&expr->ts);
  gcc_assert (expr->value.function.actual->expr);
  gfc_conv_intrinsic_function_args (se, expr, args, nargs);

  if (TREE_CODE (TREE_TYPE (args[0])) == INTEGER_TYPE)
    {
      /* Conversion to a different integer kind.  */
      se->expr = convert (type, args[0]);
    }
  else
    {
      /* Conversion from complex to non-complex involves taking the real
         component of the value.  */
      if (TREE_CODE (TREE_TYPE (args[0])) == COMPLEX_TYPE
	  && expr->ts.type != BT_COMPLEX)
	{
	  tree artype;

	  artype = TREE_TYPE (TREE_TYPE (args[0]));
	  args[0] = fold_build1_loc (input_location, REALPART_EXPR, artype,
				     args[0]);
	}

      se->expr = build_fix_expr (&se->pre, args[0], type, op);
    }
}


/* Get the imaginary component of a value.  */

static void
gfc_conv_intrinsic_imagpart (gfc_se * se, gfc_expr * expr)
{
  tree arg;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  se->expr = fold_build1_loc (input_location, IMAGPART_EXPR,
			      TREE_TYPE (TREE_TYPE (arg)), arg);
}


/* Get the complex conjugate of a value.  */

static void
gfc_conv_intrinsic_conjg (gfc_se * se, gfc_expr * expr)
{
  tree arg;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  se->expr = fold_build1_loc (input_location, CONJ_EXPR, TREE_TYPE (arg), arg);
}



static tree
define_quad_builtin (const char *name, tree type, bool is_const)
{
  tree fndecl;
  fndecl = build_decl (input_location, FUNCTION_DECL, get_identifier (name),
		       type);

  /* Mark the decl as external.  */
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  /* Mark it __attribute__((const)).  */
  TREE_READONLY (fndecl) = is_const;

  rest_of_decl_compilation (fndecl, 1, 0);

  return fndecl;
}



/* Initialize function decls for library functions.  The external functions
   are created as required.  Builtin functions are added here.  */

void
gfc_build_intrinsic_lib_fndecls (void)
{
  gfc_intrinsic_map_t *m;
  tree quad_decls[END_BUILTINS + 1];

  if (gfc_real16_is_float128)
  {
    /* If we have soft-float types, we create the decls for their
       C99-like library functions.  For now, we only handle __float128
       q-suffixed functions.  */

    tree type, complex_type, func_1, func_2, func_cabs, func_frexp;
    tree func_iround, func_lround, func_llround, func_scalbn, func_cpow;

    memset (quad_decls, 0, sizeof(tree) * (END_BUILTINS + 1));

    type = float128_type_node;
    complex_type = complex_float128_type_node;
    /* type (*) (type) */
    func_1 = build_function_type_list (type, type, NULL_TREE);
    /* int (*) (type) */
    func_iround = build_function_type_list (integer_type_node,
					    type, NULL_TREE);
    /* long (*) (type) */
    func_lround = build_function_type_list (long_integer_type_node,
					    type, NULL_TREE);
    /* long long (*) (type) */
    func_llround = build_function_type_list (long_long_integer_type_node,
					     type, NULL_TREE);
    /* type (*) (type, type) */
    func_2 = build_function_type_list (type, type, type, NULL_TREE);
    /* type (*) (type, &int) */
    func_frexp
      = build_function_type_list (type,
				  type,
				  build_pointer_type (integer_type_node),
				  NULL_TREE);
    /* type (*) (type, int) */
    func_scalbn = build_function_type_list (type,
					    type, integer_type_node, NULL_TREE);
    /* type (*) (complex type) */
    func_cabs = build_function_type_list (type, complex_type, NULL_TREE);
    /* complex type (*) (complex type, complex type) */
    func_cpow
      = build_function_type_list (complex_type,
				  complex_type, complex_type, NULL_TREE);

#define DEFINE_MATH_BUILTIN(ID, NAME, ARGTYPE)
#define DEFINE_MATH_BUILTIN_C(ID, NAME, ARGTYPE)
#define LIB_FUNCTION(ID, NAME, HAVE_COMPLEX)

    /* Only these built-ins are actually needed here. These are used directly
       from the code, when calling builtin_decl_for_precision() or
       builtin_decl_for_float_type(). The others are all constructed by
       gfc_get_intrinsic_lib_fndecl().  */
#define OTHER_BUILTIN(ID, NAME, TYPE, CONST) \
  quad_decls[BUILT_IN_ ## ID] = define_quad_builtin (NAME "q", func_ ## TYPE, CONST);

#include "mathbuiltins.def"

#undef OTHER_BUILTIN
#undef LIB_FUNCTION
#undef DEFINE_MATH_BUILTIN
#undef DEFINE_MATH_BUILTIN_C

  }

  /* Add GCC builtin functions.  */
  for (m = gfc_intrinsic_map;
       m->id != GFC_ISYM_NONE || m->double_built_in != END_BUILTINS; m++)
    {
      if (m->float_built_in != END_BUILTINS)
	m->real4_decl = builtin_decl_explicit (m->float_built_in);
      if (m->complex_float_built_in != END_BUILTINS)
	m->complex4_decl = builtin_decl_explicit (m->complex_float_built_in);
      if (m->double_built_in != END_BUILTINS)
	m->real8_decl = builtin_decl_explicit (m->double_built_in);
      if (m->complex_double_built_in != END_BUILTINS)
	m->complex8_decl = builtin_decl_explicit (m->complex_double_built_in);

      /* If real(kind=10) exists, it is always long double.  */
      if (m->long_double_built_in != END_BUILTINS)
	m->real10_decl = builtin_decl_explicit (m->long_double_built_in);
      if (m->complex_long_double_built_in != END_BUILTINS)
	m->complex10_decl
	  = builtin_decl_explicit (m->complex_long_double_built_in);

      if (!gfc_real16_is_float128)
	{
	  if (m->long_double_built_in != END_BUILTINS)
	    m->real16_decl = builtin_decl_explicit (m->long_double_built_in);
	  if (m->complex_long_double_built_in != END_BUILTINS)
	    m->complex16_decl
	      = builtin_decl_explicit (m->complex_long_double_built_in);
	}
      else if (quad_decls[m->double_built_in] != NULL_TREE)
        {
	  /* Quad-precision function calls are constructed when first
	     needed by builtin_decl_for_precision(), except for those
	     that will be used directly (define by OTHER_BUILTIN).  */
	  m->real16_decl = quad_decls[m->double_built_in];
	}
      else if (quad_decls[m->complex_double_built_in] != NULL_TREE)
        {
	  /* Same thing for the complex ones.  */
	  m->complex16_decl = quad_decls[m->double_built_in];
	}
    }
}


/* Create a fndecl for a simple intrinsic library function.  */

static tree
gfc_get_intrinsic_lib_fndecl (gfc_intrinsic_map_t * m, gfc_expr * expr)
{
  tree type;
  vec<tree, va_gc> *argtypes;
  tree fndecl;
  gfc_actual_arglist *actual;
  tree *pdecl;
  gfc_typespec *ts;
  char name[GFC_MAX_SYMBOL_LEN + 3];

  ts = &expr->ts;
  if (ts->type == BT_REAL)
    {
      switch (ts->kind)
	{
	case 4:
	  pdecl = &m->real4_decl;
	  break;
	case 8:
	  pdecl = &m->real8_decl;
	  break;
	case 10:
	  pdecl = &m->real10_decl;
	  break;
	case 16:
	  pdecl = &m->real16_decl;
	  break;
	default:
	  gcc_unreachable ();
	}
    }
  else if (ts->type == BT_COMPLEX)
    {
      gcc_assert (m->complex_available);

      switch (ts->kind)
	{
	case 4:
	  pdecl = &m->complex4_decl;
	  break;
	case 8:
	  pdecl = &m->complex8_decl;
	  break;
	case 10:
	  pdecl = &m->complex10_decl;
	  break;
	case 16:
	  pdecl = &m->complex16_decl;
	  break;
	default:
	  gcc_unreachable ();
	}
    }
  else
    gcc_unreachable ();

  if (*pdecl)
    return *pdecl;

  if (m->libm_name)
    {
      int n = gfc_validate_kind (BT_REAL, ts->kind, false);
      if (gfc_real_kinds[n].c_float)
	snprintf (name, sizeof (name), "%s%s%s",
		  ts->type == BT_COMPLEX ? "c" : "", m->name, "f");
      else if (gfc_real_kinds[n].c_double)
	snprintf (name, sizeof (name), "%s%s",
		  ts->type == BT_COMPLEX ? "c" : "", m->name);
      else if (gfc_real_kinds[n].c_long_double)
	snprintf (name, sizeof (name), "%s%s%s",
		  ts->type == BT_COMPLEX ? "c" : "", m->name, "l");
      else if (gfc_real_kinds[n].c_float128)
	snprintf (name, sizeof (name), "%s%s%s",
		  ts->type == BT_COMPLEX ? "c" : "", m->name, "q");
      else
	gcc_unreachable ();
    }
  else
    {
      snprintf (name, sizeof (name), PREFIX ("%s_%c%d"), m->name,
		ts->type == BT_COMPLEX ? 'c' : 'r',
		ts->kind);
    }

  argtypes = NULL;
  for (actual = expr->value.function.actual; actual; actual = actual->next)
    {
      type = gfc_typenode_for_spec (&actual->expr->ts);
      vec_safe_push (argtypes, type);
    }
  type = build_function_type_vec (gfc_typenode_for_spec (ts), argtypes);
  fndecl = build_decl (input_location,
		       FUNCTION_DECL, get_identifier (name), type);

  /* Mark the decl as external.  */
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 1;

  /* Mark it __attribute__((const)), if possible.  */
  TREE_READONLY (fndecl) = m->is_constant;

  rest_of_decl_compilation (fndecl, 1, 0);

  (*pdecl) = fndecl;
  return fndecl;
}


/* Convert an intrinsic function into an external or builtin call.  */

static void
gfc_conv_intrinsic_lib_function (gfc_se * se, gfc_expr * expr)
{
  gfc_intrinsic_map_t *m;
  tree fndecl;
  tree rettype;
  tree *args;
  unsigned int num_args;
  gfc_isym_id id;

  id = expr->value.function.isym->id;
  /* Find the entry for this function.  */
  for (m = gfc_intrinsic_map;
       m->id != GFC_ISYM_NONE || m->double_built_in != END_BUILTINS; m++)
    {
      if (id == m->id)
	break;
    }

  if (m->id == GFC_ISYM_NONE)
    {
      internal_error ("Intrinsic function %s(%d) not recognized",
		      expr->value.function.name, id);
    }

  /* Get the decl and generate the call.  */
  num_args = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, num_args);

  gfc_conv_intrinsic_function_args (se, expr, args, num_args);
  fndecl = gfc_get_intrinsic_lib_fndecl (m, expr);
  rettype = TREE_TYPE (TREE_TYPE (fndecl));

  fndecl = build_addr (fndecl, current_function_decl);
  se->expr = build_call_array_loc (input_location, rettype, fndecl, num_args, args);
}


/* If bounds-checking is enabled, create code to verify at runtime that the
   string lengths for both expressions are the same (needed for e.g. MERGE).
   If bounds-checking is not enabled, does nothing.  */

void
gfc_trans_same_strlen_check (const char* intr_name, locus* where,
			     tree a, tree b, stmtblock_t* target)
{
  tree cond;
  tree name;

  /* If bounds-checking is disabled, do nothing.  */
  if (!(gfc_option.rtcheck & GFC_RTCHECK_BOUNDS))
    return;

  /* Compare the two string lengths.  */
  cond = fold_build2_loc (input_location, NE_EXPR, boolean_type_node, a, b);

  /* Output the runtime-check.  */
  name = gfc_build_cstring_const (intr_name);
  name = gfc_build_addr_expr (pchar_type_node, name);
  gfc_trans_runtime_check (true, false, cond, target, where,
			   "Unequal character lengths (%ld/%ld) in %s",
			   fold_convert (long_integer_type_node, a),
			   fold_convert (long_integer_type_node, b), name);
}


/* The EXPONENT(s) intrinsic function is translated into
       int ret;
       frexp (s, &ret);
       return ret;
 */

static void
gfc_conv_intrinsic_exponent (gfc_se *se, gfc_expr *expr)
{
  tree arg, type, res, tmp, frexp;

  frexp = gfc_builtin_decl_for_float_kind (BUILT_IN_FREXP,
				       expr->value.function.actual->expr->ts.kind);

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);

  res = gfc_create_var (integer_type_node, NULL);
  tmp = build_call_expr_loc (input_location, frexp, 2, arg,
			     gfc_build_addr_expr (NULL_TREE, res));
  gfc_add_expr_to_block (&se->pre, tmp);

  type = gfc_typenode_for_spec (&expr->ts);
  se->expr = fold_convert (type, res);
}


static void
trans_this_image (gfc_se * se, gfc_expr *expr)
{
  stmtblock_t loop;
  tree type, desc, dim_arg, cond, tmp, m, loop_var, exit_label, min_var,
       lbound, ubound, extent, ml;
  gfc_se argse;
  int rank, corank;

  /* The case -fcoarray=single is handled elsewhere.  */
  gcc_assert (gfc_option.coarray != GFC_FCOARRAY_SINGLE);

  gfc_init_coarray_decl (false);

  /* Argument-free version: THIS_IMAGE().  */
  if (expr->value.function.actual->expr == NULL)
    {
      se->expr = fold_convert (gfc_get_int_type (gfc_default_integer_kind),
			       gfort_gvar_caf_this_image);
      return;
    }

  /* Coarray-argument version: THIS_IMAGE(coarray [, dim]).  */

  type = gfc_get_int_type (gfc_default_integer_kind);
  corank = gfc_get_corank (expr->value.function.actual->expr);
  rank = expr->value.function.actual->expr->rank;

  /* Obtain the descriptor of the COARRAY.  */
  gfc_init_se (&argse, NULL);
  argse.want_coarray = 1;
  gfc_conv_expr_descriptor (&argse, expr->value.function.actual->expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);
  desc = argse.expr;

  if (se->ss)
    {
      /* Create an implicit second parameter from the loop variable.  */
      gcc_assert (!expr->value.function.actual->next->expr);
      gcc_assert (corank > 0);
      gcc_assert (se->loop->dimen == 1);
      gcc_assert (se->ss->info->expr == expr);

      dim_arg = se->loop->loopvar[0];
      dim_arg = fold_build2_loc (input_location, PLUS_EXPR,
				 gfc_array_index_type, dim_arg,
				 build_int_cst (TREE_TYPE (dim_arg), 1));
      gfc_advance_se_ss_chain (se);
    }
  else
    {
      /* Use the passed DIM= argument.  */
      gcc_assert (expr->value.function.actual->next->expr);
      gfc_init_se (&argse, NULL);
      gfc_conv_expr_type (&argse, expr->value.function.actual->next->expr,
			  gfc_array_index_type);
      gfc_add_block_to_block (&se->pre, &argse.pre);
      dim_arg = argse.expr;

      if (INTEGER_CST_P (dim_arg))
	{
	  int hi, co_dim;

	  hi = TREE_INT_CST_HIGH (dim_arg);
	  co_dim = TREE_INT_CST_LOW (dim_arg);
	  if (hi || co_dim < 1
	      || co_dim > GFC_TYPE_ARRAY_CORANK (TREE_TYPE (desc)))
	    gfc_error ("'dim' argument of %s intrinsic at %L is not a valid "
		       "dimension index", expr->value.function.isym->name,
		       &expr->where);
	}
     else if (gfc_option.rtcheck & GFC_RTCHECK_BOUNDS)
	{
	  dim_arg = gfc_evaluate_now (dim_arg, &se->pre);
	  cond = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				  dim_arg,
				  build_int_cst (TREE_TYPE (dim_arg), 1));
	  tmp = gfc_rank_cst[GFC_TYPE_ARRAY_CORANK (TREE_TYPE (desc))];
	  tmp = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
				 dim_arg, tmp);
	  cond = fold_build2_loc (input_location, TRUTH_ORIF_EXPR,
				  boolean_type_node, cond, tmp);
	  gfc_trans_runtime_check (true, false, cond, &se->pre, &expr->where,
			           gfc_msg_fault);
	}
    }

  /* Used algorithm; cf. Fortran 2008, C.10. Note, due to the scalarizer,
     one always has a dim_arg argument.

     m = this_image() - 1
     if (corank == 1)
       {
	 sub(1) = m + lcobound(corank)
	 return;
       }
     i = rank
     min_var = min (rank + corank - 2, rank + dim_arg - 1)
     for (;;)
       {
	 extent = gfc_extent(i)
	 ml = m
	 m  = m/extent
	 if (i >= min_var) 
	   goto exit_label
	 i++
       }
     exit_label:
     sub(dim_arg) = (dim_arg < corank) ? ml - m*extent + lcobound(dim_arg)
				       : m + lcobound(corank)
  */

  /* this_image () - 1.  */
  tmp = fold_convert (type, gfort_gvar_caf_this_image);
  tmp = fold_build2_loc (input_location, MINUS_EXPR, type, tmp,
		       build_int_cst (type, 1));
  if (corank == 1)
    {
      /* sub(1) = m + lcobound(corank).  */
      lbound = gfc_conv_descriptor_lbound_get (desc,
			build_int_cst (TREE_TYPE (gfc_array_index_type),
				       corank+rank-1));
      lbound = fold_convert (type, lbound);
      tmp = fold_build2_loc (input_location, PLUS_EXPR, type, tmp, lbound);

      se->expr = tmp;
      return;
    }

  m = gfc_create_var (type, NULL); 
  ml = gfc_create_var (type, NULL); 
  loop_var = gfc_create_var (integer_type_node, NULL); 
  min_var = gfc_create_var (integer_type_node, NULL); 

  /* m = this_image () - 1.  */
  gfc_add_modify (&se->pre, m, tmp);

  /* min_var = min (rank + corank-2, rank + dim_arg - 1).  */
  tmp = fold_build2_loc (input_location, PLUS_EXPR, integer_type_node,
			 fold_convert (integer_type_node, dim_arg),
			 build_int_cst (integer_type_node, rank - 1));
  tmp = fold_build2_loc (input_location, MIN_EXPR, integer_type_node,
			 build_int_cst (integer_type_node, rank + corank - 2),
			 tmp);
  gfc_add_modify (&se->pre, min_var, tmp);

  /* i = rank.  */
  tmp = build_int_cst (integer_type_node, rank);
  gfc_add_modify (&se->pre, loop_var, tmp);

  exit_label = gfc_build_label_decl (NULL_TREE);
  TREE_USED (exit_label) = 1;

  /* Loop body.  */
  gfc_init_block (&loop);

  /* ml = m.  */
  gfc_add_modify (&loop, ml, m);

  /* extent = ...  */
  lbound = gfc_conv_descriptor_lbound_get (desc, loop_var);
  ubound = gfc_conv_descriptor_ubound_get (desc, loop_var);
  extent = gfc_conv_array_extent_dim (lbound, ubound, NULL);
  extent = fold_convert (type, extent);

  /* m = m/extent.  */
  gfc_add_modify (&loop, m, 
		  fold_build2_loc (input_location, TRUNC_DIV_EXPR, type,
			  m, extent));

  /* Exit condition:  if (i >= min_var) goto exit_label.  */
  cond = fold_build2_loc (input_location, GE_EXPR, boolean_type_node, loop_var,
		  min_var);
  tmp = build1_v (GOTO_EXPR, exit_label);
  tmp = fold_build3_loc (input_location, COND_EXPR, void_type_node, cond, tmp,
                         build_empty_stmt (input_location));
  gfc_add_expr_to_block (&loop, tmp);

  /* Increment loop variable: i++.  */
  gfc_add_modify (&loop, loop_var,
                  fold_build2_loc (input_location, PLUS_EXPR, integer_type_node,
				   loop_var,
				   build_int_cst (integer_type_node, 1)));

  /* Making the loop... actually loop!  */
  tmp = gfc_finish_block (&loop);
  tmp = build1_v (LOOP_EXPR, tmp);
  gfc_add_expr_to_block (&se->pre, tmp);

  /* The exit label.  */
  tmp = build1_v (LABEL_EXPR, exit_label);
  gfc_add_expr_to_block (&se->pre, tmp);

  /*  sub(co_dim) = (co_dim < corank) ? ml - m*extent + lcobound(dim_arg)
				      : m + lcobound(corank) */

  cond = fold_build2_loc (input_location, LT_EXPR, boolean_type_node, dim_arg,
			  build_int_cst (TREE_TYPE (dim_arg), corank));

  lbound = gfc_conv_descriptor_lbound_get (desc,
		fold_build2_loc (input_location, PLUS_EXPR,
				 gfc_array_index_type, dim_arg,
				 build_int_cst (TREE_TYPE (dim_arg), rank-1)));
  lbound = fold_convert (type, lbound);

  tmp = fold_build2_loc (input_location, MINUS_EXPR, type, ml,
			 fold_build2_loc (input_location, MULT_EXPR, type,
					  m, extent));
  tmp = fold_build2_loc (input_location, PLUS_EXPR, type, tmp, lbound);

  se->expr = fold_build3_loc (input_location, COND_EXPR, type, cond, tmp,
			      fold_build2_loc (input_location, PLUS_EXPR, type,
					       m, lbound));
}


static void
trans_image_index (gfc_se * se, gfc_expr *expr)
{
  tree num_images, cond, coindex, type, lbound, ubound, desc, subdesc,
       tmp, invalid_bound;
  gfc_se argse, subse;
  int rank, corank, codim;

  type = gfc_get_int_type (gfc_default_integer_kind);
  corank = gfc_get_corank (expr->value.function.actual->expr);
  rank = expr->value.function.actual->expr->rank;

  /* Obtain the descriptor of the COARRAY.  */
  gfc_init_se (&argse, NULL);
  argse.want_coarray = 1;
  gfc_conv_expr_descriptor (&argse, expr->value.function.actual->expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);
  desc = argse.expr;

  /* Obtain a handle to the SUB argument.  */
  gfc_init_se (&subse, NULL);
  gfc_conv_expr_descriptor (&subse, expr->value.function.actual->next->expr);
  gfc_add_block_to_block (&se->pre, &subse.pre);
  gfc_add_block_to_block (&se->post, &subse.post);
  subdesc = build_fold_indirect_ref_loc (input_location,
			gfc_conv_descriptor_data_get (subse.expr));

  /* Fortran 2008 does not require that the values remain in the cobounds,
     thus we need explicitly check this - and return 0 if they are exceeded.  */

  lbound = gfc_conv_descriptor_lbound_get (desc, gfc_rank_cst[rank+corank-1]);
  tmp = gfc_build_array_ref (subdesc, gfc_rank_cst[corank-1], NULL);
  invalid_bound = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				 fold_convert (gfc_array_index_type, tmp),
				 lbound);

  for (codim = corank + rank - 2; codim >= rank; codim--)
    {
      lbound = gfc_conv_descriptor_lbound_get (desc, gfc_rank_cst[codim]);
      ubound = gfc_conv_descriptor_ubound_get (desc, gfc_rank_cst[codim]);
      tmp = gfc_build_array_ref (subdesc, gfc_rank_cst[codim-rank], NULL);
      cond = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
			      fold_convert (gfc_array_index_type, tmp),
			      lbound);
      invalid_bound = fold_build2_loc (input_location, TRUTH_OR_EXPR,
				       boolean_type_node, invalid_bound, cond);
      cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			      fold_convert (gfc_array_index_type, tmp),
			      ubound);
      invalid_bound = fold_build2_loc (input_location, TRUTH_OR_EXPR,
				       boolean_type_node, invalid_bound, cond);
    }

  invalid_bound = gfc_unlikely (invalid_bound);


  /* See Fortran 2008, C.10 for the following algorithm.  */

  /* coindex = sub(corank) - lcobound(n).  */
  coindex = fold_convert (gfc_array_index_type,
			  gfc_build_array_ref (subdesc, gfc_rank_cst[corank-1],
					       NULL));
  lbound = gfc_conv_descriptor_lbound_get (desc, gfc_rank_cst[rank+corank-1]);
  coindex = fold_build2_loc (input_location, MINUS_EXPR, gfc_array_index_type,
			     fold_convert (gfc_array_index_type, coindex),
			     lbound);

  for (codim = corank + rank - 2; codim >= rank; codim--)
    {
      tree extent, ubound;

      /* coindex = coindex*extent(codim) + sub(codim) - lcobound(codim).  */
      lbound = gfc_conv_descriptor_lbound_get (desc, gfc_rank_cst[codim]);
      ubound = gfc_conv_descriptor_ubound_get (desc, gfc_rank_cst[codim]);
      extent = gfc_conv_array_extent_dim (lbound, ubound, NULL);

      /* coindex *= extent.  */
      coindex = fold_build2_loc (input_location, MULT_EXPR,
				 gfc_array_index_type, coindex, extent);

      /* coindex += sub(codim).  */
      tmp = gfc_build_array_ref (subdesc, gfc_rank_cst[codim-rank], NULL);
      coindex = fold_build2_loc (input_location, PLUS_EXPR,
				 gfc_array_index_type, coindex,
				 fold_convert (gfc_array_index_type, tmp));

      /* coindex -= lbound(codim).  */
      lbound = gfc_conv_descriptor_lbound_get (desc, gfc_rank_cst[codim]);
      coindex = fold_build2_loc (input_location, MINUS_EXPR,
				 gfc_array_index_type, coindex, lbound);
    }

  coindex = fold_build2_loc (input_location, PLUS_EXPR, type,
			     fold_convert(type, coindex),
			     build_int_cst (type, 1));

  /* Return 0 if "coindex" exceeds num_images().  */

  if (gfc_option.coarray == GFC_FCOARRAY_SINGLE)
    num_images = build_int_cst (type, 1);
  else
    {
      gfc_init_coarray_decl (false);
      num_images = fold_convert (type, gfort_gvar_caf_num_images);
    }

  tmp = gfc_create_var (type, NULL);
  gfc_add_modify (&se->pre, tmp, coindex);

  cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node, tmp,
			  num_images);
  cond = fold_build2_loc (input_location, TRUTH_OR_EXPR, boolean_type_node,
			  cond,
			  fold_convert (boolean_type_node, invalid_bound));
  se->expr = fold_build3_loc (input_location, COND_EXPR, type, cond,
			      build_int_cst (type, 0), tmp);
}


static void
trans_num_images (gfc_se * se)
{
  gfc_init_coarray_decl (false);
  se->expr = fold_convert (gfc_get_int_type (gfc_default_integer_kind),
			   gfort_gvar_caf_num_images);
}


static void
gfc_conv_intrinsic_rank (gfc_se *se, gfc_expr *expr)
{
  gfc_se argse;

  gfc_init_se (&argse, NULL);
  argse.data_not_needed = 1;
  argse.descriptor_only = 1;

  gfc_conv_expr_descriptor (&argse, expr->value.function.actual->expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);

  se->expr = gfc_conv_descriptor_rank (argse.expr);
}


/* Evaluate a single upper or lower bound.  */
/* TODO: bound intrinsic generates way too much unnecessary code.  */

static void
gfc_conv_intrinsic_bound (gfc_se * se, gfc_expr * expr, int upper)
{
  gfc_actual_arglist *arg;
  gfc_actual_arglist *arg2;
  tree desc;
  tree type;
  tree bound;
  tree tmp;
  tree cond, cond1, cond3, cond4, size;
  tree ubound;
  tree lbound;
  gfc_se argse;
  gfc_array_spec * as;
  bool assumed_rank_lb_one;

  arg = expr->value.function.actual;
  arg2 = arg->next;

  if (se->ss)
    {
      /* Create an implicit second parameter from the loop variable.  */
      gcc_assert (!arg2->expr);
      gcc_assert (se->loop->dimen == 1);
      gcc_assert (se->ss->info->expr == expr);
      gfc_advance_se_ss_chain (se);
      bound = se->loop->loopvar[0];
      bound = fold_build2_loc (input_location, MINUS_EXPR,
			       gfc_array_index_type, bound,
			       se->loop->from[0]);
    }
  else
    {
      /* use the passed argument.  */
      gcc_assert (arg2->expr);
      gfc_init_se (&argse, NULL);
      gfc_conv_expr_type (&argse, arg2->expr, gfc_array_index_type);
      gfc_add_block_to_block (&se->pre, &argse.pre);
      bound = argse.expr;
      /* Convert from one based to zero based.  */
      bound = fold_build2_loc (input_location, MINUS_EXPR,
			       gfc_array_index_type, bound,
			       gfc_index_one_node);
    }

  /* TODO: don't re-evaluate the descriptor on each iteration.  */
  /* Get a descriptor for the first parameter.  */
  gfc_init_se (&argse, NULL);
  gfc_conv_expr_descriptor (&argse, arg->expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);

  desc = argse.expr;

  as = gfc_get_full_arrayspec_from_expr (arg->expr);

  if (INTEGER_CST_P (bound))
    {
      int hi, low;

      hi = TREE_INT_CST_HIGH (bound);
      low = TREE_INT_CST_LOW (bound);
      if (hi || low < 0
	  || ((!as || as->type != AS_ASSUMED_RANK)
	      && low >= GFC_TYPE_ARRAY_RANK (TREE_TYPE (desc)))
	  || low > GFC_MAX_DIMENSIONS)
	gfc_error ("'dim' argument of %s intrinsic at %L is not a valid "
		   "dimension index", upper ? "UBOUND" : "LBOUND",
		   &expr->where);
    }

  if (!INTEGER_CST_P (bound) || (as && as->type == AS_ASSUMED_RANK))
    {
      if (gfc_option.rtcheck & GFC_RTCHECK_BOUNDS)
        {
          bound = gfc_evaluate_now (bound, &se->pre);
          cond = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				  bound, build_int_cst (TREE_TYPE (bound), 0));
	  if (as && as->type == AS_ASSUMED_RANK)
	    tmp = gfc_conv_descriptor_rank (desc);
	  else
	    tmp = gfc_rank_cst[GFC_TYPE_ARRAY_RANK (TREE_TYPE (desc))];
          tmp = fold_build2_loc (input_location, GE_EXPR, boolean_type_node,
				 bound, fold_convert(TREE_TYPE (bound), tmp));
          cond = fold_build2_loc (input_location, TRUTH_ORIF_EXPR,
				  boolean_type_node, cond, tmp);
          gfc_trans_runtime_check (true, false, cond, &se->pre, &expr->where,
				   gfc_msg_fault);
        }
    }

  /* Take care of the lbound shift for assumed-rank arrays, which are
     nonallocatable and nonpointers. Those has a lbound of 1.  */
  assumed_rank_lb_one = as && as->type == AS_ASSUMED_RANK
			&& ((arg->expr->ts.type != BT_CLASS
			     && !arg->expr->symtree->n.sym->attr.allocatable
			     && !arg->expr->symtree->n.sym->attr.pointer)
			    || (arg->expr->ts.type == BT_CLASS
			     && !CLASS_DATA (arg->expr)->attr.allocatable
			     && !CLASS_DATA (arg->expr)->attr.class_pointer));

  ubound = gfc_conv_descriptor_ubound_get (desc, bound);
  lbound = gfc_conv_descriptor_lbound_get (desc, bound);
  
  /* 13.14.53: Result value for LBOUND

     Case (i): For an array section or for an array expression other than a
               whole array or array structure component, LBOUND(ARRAY, DIM)
               has the value 1.  For a whole array or array structure
               component, LBOUND(ARRAY, DIM) has the value:
                 (a) equal to the lower bound for subscript DIM of ARRAY if
                     dimension DIM of ARRAY does not have extent zero
                     or if ARRAY is an assumed-size array of rank DIM,
              or (b) 1 otherwise.

     13.14.113: Result value for UBOUND

     Case (i): For an array section or for an array expression other than a
               whole array or array structure component, UBOUND(ARRAY, DIM)
               has the value equal to the number of elements in the given
               dimension; otherwise, it has a value equal to the upper bound
               for subscript DIM of ARRAY if dimension DIM of ARRAY does
               not have size zero and has value zero if dimension DIM has
               size zero.  */

  if (!upper && assumed_rank_lb_one)
    se->expr = gfc_index_one_node;
  else if (as)
    {
      tree stride = gfc_conv_descriptor_stride_get (desc, bound);

      cond1 = fold_build2_loc (input_location, GE_EXPR, boolean_type_node,
			       ubound, lbound);
      cond3 = fold_build2_loc (input_location, GE_EXPR, boolean_type_node,
			       stride, gfc_index_zero_node);
      cond3 = fold_build2_loc (input_location, TRUTH_AND_EXPR,
			       boolean_type_node, cond3, cond1);
      cond4 = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
			       stride, gfc_index_zero_node);

      if (upper)
	{
	  tree cond5;
	  cond = fold_build2_loc (input_location, TRUTH_OR_EXPR,
				  boolean_type_node, cond3, cond4);
	  cond5 = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
				   gfc_index_one_node, lbound);
	  cond5 = fold_build2_loc (input_location, TRUTH_AND_EXPR,
				   boolean_type_node, cond4, cond5);

	  cond = fold_build2_loc (input_location, TRUTH_OR_EXPR,
				  boolean_type_node, cond, cond5);

	  if (assumed_rank_lb_one)
	    {
	      tmp = fold_build2_loc (input_location, MINUS_EXPR,
			       gfc_array_index_type, ubound, lbound);
	      tmp = fold_build2_loc (input_location, PLUS_EXPR,
			       gfc_array_index_type, tmp, gfc_index_one_node);
	    }
          else
            tmp = ubound;

	  se->expr = fold_build3_loc (input_location, COND_EXPR,
				      gfc_array_index_type, cond,
				      tmp, gfc_index_zero_node);
	}
      else
	{
	  if (as->type == AS_ASSUMED_SIZE)
	    cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
				    bound, build_int_cst (TREE_TYPE (bound),
							  arg->expr->rank - 1));
	  else
	    cond = boolean_false_node;

	  cond1 = fold_build2_loc (input_location, TRUTH_OR_EXPR,
				   boolean_type_node, cond3, cond4);
	  cond = fold_build2_loc (input_location, TRUTH_OR_EXPR,
				  boolean_type_node, cond, cond1);

	  se->expr = fold_build3_loc (input_location, COND_EXPR,
				      gfc_array_index_type, cond,
				      lbound, gfc_index_one_node);
	}
    }
  else
    {
      if (upper)
        {
	  size = fold_build2_loc (input_location, MINUS_EXPR,
				  gfc_array_index_type, ubound, lbound);
	  se->expr = fold_build2_loc (input_location, PLUS_EXPR,
				      gfc_array_index_type, size,
				  gfc_index_one_node);
	  se->expr = fold_build2_loc (input_location, MAX_EXPR,
				      gfc_array_index_type, se->expr,
				      gfc_index_zero_node);
	}
      else
	se->expr = gfc_index_one_node;
    }

  type = gfc_typenode_for_spec (&expr->ts);
  se->expr = convert (type, se->expr);
}


static void
conv_intrinsic_cobound (gfc_se * se, gfc_expr * expr)
{
  gfc_actual_arglist *arg;
  gfc_actual_arglist *arg2;
  gfc_se argse;
  tree bound, resbound, resbound2, desc, cond, tmp;
  tree type;
  int corank;

  gcc_assert (expr->value.function.isym->id == GFC_ISYM_LCOBOUND
	      || expr->value.function.isym->id == GFC_ISYM_UCOBOUND
	      || expr->value.function.isym->id == GFC_ISYM_THIS_IMAGE);

  arg = expr->value.function.actual;
  arg2 = arg->next;

  gcc_assert (arg->expr->expr_type == EXPR_VARIABLE);
  corank = gfc_get_corank (arg->expr);

  gfc_init_se (&argse, NULL);
  argse.want_coarray = 1;

  gfc_conv_expr_descriptor (&argse, arg->expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);
  desc = argse.expr;

  if (se->ss)
    {
      /* Create an implicit second parameter from the loop variable.  */
      gcc_assert (!arg2->expr);
      gcc_assert (corank > 0);
      gcc_assert (se->loop->dimen == 1);
      gcc_assert (se->ss->info->expr == expr);

      bound = se->loop->loopvar[0];
      bound = fold_build2_loc (input_location, PLUS_EXPR, gfc_array_index_type,
			       bound, gfc_rank_cst[arg->expr->rank]);
      gfc_advance_se_ss_chain (se);
    }
  else
    {
      /* use the passed argument.  */
      gcc_assert (arg2->expr);
      gfc_init_se (&argse, NULL);
      gfc_conv_expr_type (&argse, arg2->expr, gfc_array_index_type);
      gfc_add_block_to_block (&se->pre, &argse.pre);
      bound = argse.expr;

      if (INTEGER_CST_P (bound))
	{
	  int hi, low;

	  hi = TREE_INT_CST_HIGH (bound);
	  low = TREE_INT_CST_LOW (bound);
	  if (hi || low < 1 || low > GFC_TYPE_ARRAY_CORANK (TREE_TYPE (desc)))
	    gfc_error ("'dim' argument of %s intrinsic at %L is not a valid "
		       "dimension index", expr->value.function.isym->name,
		       &expr->where);
	}
      else if (gfc_option.rtcheck & GFC_RTCHECK_BOUNDS)
        {
	  bound = gfc_evaluate_now (bound, &se->pre);
	  cond = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				  bound, build_int_cst (TREE_TYPE (bound), 1));
	  tmp = gfc_rank_cst[GFC_TYPE_ARRAY_CORANK (TREE_TYPE (desc))];
	  tmp = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
				 bound, tmp);
	  cond = fold_build2_loc (input_location, TRUTH_ORIF_EXPR,
				  boolean_type_node, cond, tmp);
	  gfc_trans_runtime_check (true, false, cond, &se->pre, &expr->where,
				   gfc_msg_fault);
	}


      /* Subtract 1 to get to zero based and add dimensions.  */
      switch (arg->expr->rank)
	{
	case 0:
	  bound = fold_build2_loc (input_location, MINUS_EXPR,
				   gfc_array_index_type, bound,
				   gfc_index_one_node);
	case 1:
	  break;
	default:
	  bound = fold_build2_loc (input_location, PLUS_EXPR,
				   gfc_array_index_type, bound,
				   gfc_rank_cst[arg->expr->rank - 1]);
	}
    }

  resbound = gfc_conv_descriptor_lbound_get (desc, bound);

  /* Handle UCOBOUND with special handling of the last codimension.  */
  if (expr->value.function.isym->id == GFC_ISYM_UCOBOUND)
    {
      /* Last codimension: For -fcoarray=single just return
	 the lcobound - otherwise add
	   ceiling (real (num_images ()) / real (size)) - 1
	 = (num_images () + size - 1) / size - 1
	 = (num_images - 1) / size(),
         where size is the product of the extent of all but the last
	 codimension.  */

      if (gfc_option.coarray != GFC_FCOARRAY_SINGLE && corank > 1)
	{
          tree cosize;

	  gfc_init_coarray_decl (false);
	  cosize = gfc_conv_descriptor_cosize (desc, arg->expr->rank, corank);

	  tmp = fold_build2_loc (input_location, MINUS_EXPR,
				 gfc_array_index_type,
				 fold_convert (gfc_array_index_type,
					       gfort_gvar_caf_num_images),
				 build_int_cst (gfc_array_index_type, 1));
	  tmp = fold_build2_loc (input_location, TRUNC_DIV_EXPR,
				 gfc_array_index_type, tmp,
				 fold_convert (gfc_array_index_type, cosize));
	  resbound = fold_build2_loc (input_location, PLUS_EXPR,
				      gfc_array_index_type, resbound, tmp);
	}
      else if (gfc_option.coarray != GFC_FCOARRAY_SINGLE)
	{
	  /* ubound = lbound + num_images() - 1.  */
	  gfc_init_coarray_decl (false);
	  tmp = fold_build2_loc (input_location, MINUS_EXPR,
				 gfc_array_index_type,
				 fold_convert (gfc_array_index_type,
					       gfort_gvar_caf_num_images),
				 build_int_cst (gfc_array_index_type, 1));
	  resbound = fold_build2_loc (input_location, PLUS_EXPR,
				      gfc_array_index_type, resbound, tmp);
	}

      if (corank > 1)
	{
	  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
				  bound,
				  build_int_cst (TREE_TYPE (bound),
						 arg->expr->rank + corank - 1));

	  resbound2 = gfc_conv_descriptor_ubound_get (desc, bound);
	  se->expr = fold_build3_loc (input_location, COND_EXPR,
				      gfc_array_index_type, cond,
				      resbound, resbound2);
	}
      else
	se->expr = resbound;
    }
  else
    se->expr = resbound;

  type = gfc_typenode_for_spec (&expr->ts);
  se->expr = convert (type, se->expr);
}


static void
conv_intrinsic_stride (gfc_se * se, gfc_expr * expr)
{
  gfc_actual_arglist *array_arg;
  gfc_actual_arglist *dim_arg;
  gfc_se argse;
  tree desc, tmp;

  array_arg = expr->value.function.actual;
  dim_arg = array_arg->next;

  gcc_assert (array_arg->expr->expr_type == EXPR_VARIABLE);

  gfc_init_se (&argse, NULL);
  gfc_conv_expr_descriptor (&argse, array_arg->expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);
  desc = argse.expr;

  gcc_assert (dim_arg->expr);
  gfc_init_se (&argse, NULL);
  gfc_conv_expr_type (&argse, dim_arg->expr, gfc_array_index_type);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  tmp = fold_build2_loc (input_location, MINUS_EXPR, gfc_array_index_type,
			 argse.expr, gfc_index_one_node);
  se->expr = gfc_conv_descriptor_stride_get (desc, tmp);
}


static void
gfc_conv_intrinsic_abs (gfc_se * se, gfc_expr * expr)
{
  tree arg, cabs;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);

  switch (expr->value.function.actual->expr->ts.type)
    {
    case BT_INTEGER:
    case BT_REAL:
      se->expr = fold_build1_loc (input_location, ABS_EXPR, TREE_TYPE (arg),
				  arg);
      break;

    case BT_COMPLEX:
      cabs = gfc_builtin_decl_for_float_kind (BUILT_IN_CABS, expr->ts.kind);
      se->expr = build_call_expr_loc (input_location, cabs, 1, arg);
      break;

    default:
      gcc_unreachable ();
    }
}


/* Create a complex value from one or two real components.  */

static void
gfc_conv_intrinsic_cmplx (gfc_se * se, gfc_expr * expr, int both)
{
  tree real;
  tree imag;
  tree type;
  tree *args;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, num_args);

  type = gfc_typenode_for_spec (&expr->ts);
  gfc_conv_intrinsic_function_args (se, expr, args, num_args);
  real = convert (TREE_TYPE (type), args[0]);
  if (both)
    imag = convert (TREE_TYPE (type), args[1]);
  else if (TREE_CODE (TREE_TYPE (args[0])) == COMPLEX_TYPE)
    {
      imag = fold_build1_loc (input_location, IMAGPART_EXPR,
			      TREE_TYPE (TREE_TYPE (args[0])), args[0]);
      imag = convert (TREE_TYPE (type), imag);
    }
  else
    imag = build_real_from_int_cst (TREE_TYPE (type), integer_zero_node);

  se->expr = fold_build2_loc (input_location, COMPLEX_EXPR, type, real, imag);
}


/* Remainder function MOD(A, P) = A - INT(A / P) * P
                      MODULO(A, P) = A - FLOOR (A / P) * P  

   The obvious algorithms above are numerically instable for large
   arguments, hence these intrinsics are instead implemented via calls
   to the fmod family of functions.  It is the responsibility of the
   user to ensure that the second argument is non-zero.  */

static void
gfc_conv_intrinsic_mod (gfc_se * se, gfc_expr * expr, int modulo)
{
  tree type;
  tree tmp;
  tree test;
  tree test2;
  tree fmod;
  tree zero;
  tree args[2];

  gfc_conv_intrinsic_function_args (se, expr, args, 2);

  switch (expr->ts.type)
    {
    case BT_INTEGER:
      /* Integer case is easy, we've got a builtin op.  */
      type = TREE_TYPE (args[0]);

      if (modulo)
       se->expr = fold_build2_loc (input_location, FLOOR_MOD_EXPR, type,
				   args[0], args[1]);
      else
       se->expr = fold_build2_loc (input_location, TRUNC_MOD_EXPR, type,
				   args[0], args[1]);
      break;

    case BT_REAL:
      fmod = NULL_TREE;
      /* Check if we have a builtin fmod.  */
      fmod = gfc_builtin_decl_for_float_kind (BUILT_IN_FMOD, expr->ts.kind);

      /* The builtin should always be available.  */
      gcc_assert (fmod != NULL_TREE);

      tmp = build_addr (fmod, current_function_decl);
      se->expr = build_call_array_loc (input_location,
				       TREE_TYPE (TREE_TYPE (fmod)),
                                       tmp, 2, args);
      if (modulo == 0)
	return;

      type = TREE_TYPE (args[0]);

      args[0] = gfc_evaluate_now (args[0], &se->pre);
      args[1] = gfc_evaluate_now (args[1], &se->pre);

      /* Definition:
	 modulo = arg - floor (arg/arg2) * arg2

	 In order to calculate the result accurately, we use the fmod
	 function as follows.
	 
	 res = fmod (arg, arg2);
	 if (res)
	   {
	     if ((arg < 0) xor (arg2 < 0))
	       res += arg2;
	   }
	 else
	   res = copysign (0., arg2);

	 => As two nested ternary exprs:

	 res = res ? (((arg < 0) xor (arg2 < 0)) ? res + arg2 : res) 
	       : copysign (0., arg2);

      */

      zero = gfc_build_const (type, integer_zero_node);
      tmp = gfc_evaluate_now (se->expr, &se->pre);
      if (!flag_signed_zeros)
	{
	  test = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				  args[0], zero);
	  test2 = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				   args[1], zero);
	  test2 = fold_build2_loc (input_location, TRUTH_XOR_EXPR,
				   boolean_type_node, test, test2);
	  test = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
				  tmp, zero);
	  test = fold_build2_loc (input_location, TRUTH_AND_EXPR,
				  boolean_type_node, test, test2);
	  test = gfc_evaluate_now (test, &se->pre);
	  se->expr = fold_build3_loc (input_location, COND_EXPR, type, test,
				      fold_build2_loc (input_location, 
						       PLUS_EXPR,
						       type, tmp, args[1]), 
				      tmp);
	}
      else
	{
	  tree expr1, copysign, cscall;
	  copysign = gfc_builtin_decl_for_float_kind (BUILT_IN_COPYSIGN, 
						      expr->ts.kind);
	  test = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				  args[0], zero);
	  test2 = fold_build2_loc (input_location, LT_EXPR, boolean_type_node,
				   args[1], zero);
	  test2 = fold_build2_loc (input_location, TRUTH_XOR_EXPR,
				   boolean_type_node, test, test2);
	  expr1 = fold_build3_loc (input_location, COND_EXPR, type, test2,
				   fold_build2_loc (input_location, 
						    PLUS_EXPR,
						    type, tmp, args[1]), 
				   tmp);
	  test = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
				  tmp, zero);
	  cscall = build_call_expr_loc (input_location, copysign, 2, zero, 
					args[1]);
	  se->expr = fold_build3_loc (input_location, COND_EXPR, type, test,
				      expr1, cscall);
	}
      return;

    default:
      gcc_unreachable ();
    }
}

/* DSHIFTL(I,J,S) = (I << S) | (J >> (BITSIZE(J) - S))
   DSHIFTR(I,J,S) = (I << (BITSIZE(I) - S)) | (J >> S)
   where the right shifts are logical (i.e. 0's are shifted in).
   Because SHIFT_EXPR's want shifts strictly smaller than the integral
   type width, we have to special-case both S == 0 and S == BITSIZE(J):
     DSHIFTL(I,J,0) = I
     DSHIFTL(I,J,BITSIZE) = J
     DSHIFTR(I,J,0) = J
     DSHIFTR(I,J,BITSIZE) = I.  */

static void
gfc_conv_intrinsic_dshift (gfc_se * se, gfc_expr * expr, bool dshiftl)
{
  tree type, utype, stype, arg1, arg2, shift, res, left, right;
  tree args[3], cond, tmp;
  int bitsize;

  gfc_conv_intrinsic_function_args (se, expr, args, 3);

  gcc_assert (TREE_TYPE (args[0]) == TREE_TYPE (args[1]));
  type = TREE_TYPE (args[0]);
  bitsize = TYPE_PRECISION (type);
  utype = unsigned_type_for (type);
  stype = TREE_TYPE (args[2]);

  arg1 = gfc_evaluate_now (args[0], &se->pre);
  arg2 = gfc_evaluate_now (args[1], &se->pre);
  shift = gfc_evaluate_now (args[2], &se->pre);

  /* The generic case.  */
  tmp = fold_build2_loc (input_location, MINUS_EXPR, stype,
			 build_int_cst (stype, bitsize), shift);
  left = fold_build2_loc (input_location, LSHIFT_EXPR, type,
			  arg1, dshiftl ? shift : tmp);

  right = fold_build2_loc (input_location, RSHIFT_EXPR, utype,
			   fold_convert (utype, arg2), dshiftl ? tmp : shift);
  right = fold_convert (type, right);

  res = fold_build2_loc (input_location, BIT_IOR_EXPR, type, left, right);

  /* Special cases.  */
  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, shift,
			  build_int_cst (stype, 0));
  res = fold_build3_loc (input_location, COND_EXPR, type, cond,
			 dshiftl ? arg1 : arg2, res);

  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, shift,
			  build_int_cst (stype, bitsize));
  res = fold_build3_loc (input_location, COND_EXPR, type, cond,
			 dshiftl ? arg2 : arg1, res);

  se->expr = res;
}


/* Positive difference DIM (x, y) = ((x - y) < 0) ? 0 : x - y.  */

static void
gfc_conv_intrinsic_dim (gfc_se * se, gfc_expr * expr)
{
  tree val;
  tree tmp;
  tree type;
  tree zero;
  tree args[2];

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  type = TREE_TYPE (args[0]);

  val = fold_build2_loc (input_location, MINUS_EXPR, type, args[0], args[1]);
  val = gfc_evaluate_now (val, &se->pre);

  zero = gfc_build_const (type, integer_zero_node);
  tmp = fold_build2_loc (input_location, LE_EXPR, boolean_type_node, val, zero);
  se->expr = fold_build3_loc (input_location, COND_EXPR, type, tmp, zero, val);
}


/* SIGN(A, B) is absolute value of A times sign of B.
   The real value versions use library functions to ensure the correct
   handling of negative zero.  Integer case implemented as:
   SIGN(A, B) = { tmp = (A ^ B) >> C; (A + tmp) ^ tmp }
  */

static void
gfc_conv_intrinsic_sign (gfc_se * se, gfc_expr * expr)
{
  tree tmp;
  tree type;
  tree args[2];

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  if (expr->ts.type == BT_REAL)
    {
      tree abs;

      tmp = gfc_builtin_decl_for_float_kind (BUILT_IN_COPYSIGN, expr->ts.kind);
      abs = gfc_builtin_decl_for_float_kind (BUILT_IN_FABS, expr->ts.kind);

      /* We explicitly have to ignore the minus sign. We do so by using
	 result = (arg1 == 0) ? abs(arg0) : copysign(arg0, arg1).  */
      if (!gfc_option.flag_sign_zero
	  && MODE_HAS_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (args[1]))))
	{
	  tree cond, zero;
	  zero = build_real_from_int_cst (TREE_TYPE (args[1]), integer_zero_node);
	  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
				  args[1], zero);
	  se->expr = fold_build3_loc (input_location, COND_EXPR,
				  TREE_TYPE (args[0]), cond,
				  build_call_expr_loc (input_location, abs, 1,
						       args[0]),
				  build_call_expr_loc (input_location, tmp, 2,
						       args[0], args[1]));
	}
      else
        se->expr = build_call_expr_loc (input_location, tmp, 2,
					args[0], args[1]);
      return;
    }

  /* Having excluded floating point types, we know we are now dealing
     with signed integer types.  */
  type = TREE_TYPE (args[0]);

  /* Args[0] is used multiple times below.  */
  args[0] = gfc_evaluate_now (args[0], &se->pre);

  /* Construct (A ^ B) >> 31, which generates a bit mask of all zeros if
     the signs of A and B are the same, and of all ones if they differ.  */
  tmp = fold_build2_loc (input_location, BIT_XOR_EXPR, type, args[0], args[1]);
  tmp = fold_build2_loc (input_location, RSHIFT_EXPR, type, tmp,
			 build_int_cst (type, TYPE_PRECISION (type) - 1));
  tmp = gfc_evaluate_now (tmp, &se->pre);

  /* Construct (A + tmp) ^ tmp, which is A if tmp is zero, and -A if tmp]
     is all ones (i.e. -1).  */
  se->expr = fold_build2_loc (input_location, BIT_XOR_EXPR, type,
			      fold_build2_loc (input_location, PLUS_EXPR,
					       type, args[0], tmp), tmp);
}


/* Test for the presence of an optional argument.  */

static void
gfc_conv_intrinsic_present (gfc_se * se, gfc_expr * expr)
{
  gfc_expr *arg;

  arg = expr->value.function.actual->expr;
  gcc_assert (arg->expr_type == EXPR_VARIABLE);
  se->expr = gfc_conv_expr_present (arg->symtree->n.sym);
  se->expr = convert (gfc_typenode_for_spec (&expr->ts), se->expr);
}


/* Calculate the double precision product of two single precision values.  */

static void
gfc_conv_intrinsic_dprod (gfc_se * se, gfc_expr * expr)
{
  tree type;
  tree args[2];

  gfc_conv_intrinsic_function_args (se, expr, args, 2);

  /* Convert the args to double precision before multiplying.  */
  type = gfc_typenode_for_spec (&expr->ts);
  args[0] = convert (type, args[0]);
  args[1] = convert (type, args[1]);
  se->expr = fold_build2_loc (input_location, MULT_EXPR, type, args[0],
			      args[1]);
}


/* Return a length one character string containing an ascii character.  */

static void
gfc_conv_intrinsic_char (gfc_se * se, gfc_expr * expr)
{
  tree arg[2];
  tree var;
  tree type;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr);
  gfc_conv_intrinsic_function_args (se, expr, arg, num_args);

  type = gfc_get_char_type (expr->ts.kind);
  var = gfc_create_var (type, "char");

  arg[0] = fold_build1_loc (input_location, NOP_EXPR, type, arg[0]);
  gfc_add_modify (&se->pre, var, arg[0]);
  se->expr = gfc_build_addr_expr (build_pointer_type (type), var);
  se->string_length = build_int_cst (gfc_charlen_type_node, 1);
}


static void
gfc_conv_intrinsic_ctime (gfc_se * se, gfc_expr * expr)
{
  tree var;
  tree len;
  tree tmp;
  tree cond;
  tree fndecl;
  tree *args;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr) + 2;
  args = XALLOCAVEC (tree, num_args);

  var = gfc_create_var (pchar_type_node, "pstr");
  len = gfc_create_var (gfc_charlen_type_node, "len");

  gfc_conv_intrinsic_function_args (se, expr, &args[2], num_args - 2);
  args[0] = gfc_build_addr_expr (NULL_TREE, var);
  args[1] = gfc_build_addr_expr (NULL_TREE, len);

  fndecl = build_addr (gfor_fndecl_ctime, current_function_decl);
  tmp = build_call_array_loc (input_location,
			  TREE_TYPE (TREE_TYPE (gfor_fndecl_ctime)),
			  fndecl, num_args, args);
  gfc_add_expr_to_block (&se->pre, tmp);

  /* Free the temporary afterwards, if necessary.  */
  cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			  len, build_int_cst (TREE_TYPE (len), 0));
  tmp = gfc_call_free (var);
  tmp = build3_v (COND_EXPR, cond, tmp, build_empty_stmt (input_location));
  gfc_add_expr_to_block (&se->post, tmp);

  se->expr = var;
  se->string_length = len;
}


static void
gfc_conv_intrinsic_fdate (gfc_se * se, gfc_expr * expr)
{
  tree var;
  tree len;
  tree tmp;
  tree cond;
  tree fndecl;
  tree *args;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr) + 2;
  args = XALLOCAVEC (tree, num_args);

  var = gfc_create_var (pchar_type_node, "pstr");
  len = gfc_create_var (gfc_charlen_type_node, "len");

  gfc_conv_intrinsic_function_args (se, expr, &args[2], num_args - 2);
  args[0] = gfc_build_addr_expr (NULL_TREE, var);
  args[1] = gfc_build_addr_expr (NULL_TREE, len);

  fndecl = build_addr (gfor_fndecl_fdate, current_function_decl);
  tmp = build_call_array_loc (input_location,
			  TREE_TYPE (TREE_TYPE (gfor_fndecl_fdate)),
			  fndecl, num_args, args);
  gfc_add_expr_to_block (&se->pre, tmp);

  /* Free the temporary afterwards, if necessary.  */
  cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			  len, build_int_cst (TREE_TYPE (len), 0));
  tmp = gfc_call_free (var);
  tmp = build3_v (COND_EXPR, cond, tmp, build_empty_stmt (input_location));
  gfc_add_expr_to_block (&se->post, tmp);

  se->expr = var;
  se->string_length = len;
}


/* Return a character string containing the tty name.  */

static void
gfc_conv_intrinsic_ttynam (gfc_se * se, gfc_expr * expr)
{
  tree var;
  tree len;
  tree tmp;
  tree cond;
  tree fndecl;
  tree *args;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr) + 2;
  args = XALLOCAVEC (tree, num_args);

  var = gfc_create_var (pchar_type_node, "pstr");
  len = gfc_create_var (gfc_charlen_type_node, "len");

  gfc_conv_intrinsic_function_args (se, expr, &args[2], num_args - 2);
  args[0] = gfc_build_addr_expr (NULL_TREE, var);
  args[1] = gfc_build_addr_expr (NULL_TREE, len);

  fndecl = build_addr (gfor_fndecl_ttynam, current_function_decl);
  tmp = build_call_array_loc (input_location,
			  TREE_TYPE (TREE_TYPE (gfor_fndecl_ttynam)),
			  fndecl, num_args, args);
  gfc_add_expr_to_block (&se->pre, tmp);

  /* Free the temporary afterwards, if necessary.  */
  cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			  len, build_int_cst (TREE_TYPE (len), 0));
  tmp = gfc_call_free (var);
  tmp = build3_v (COND_EXPR, cond, tmp, build_empty_stmt (input_location));
  gfc_add_expr_to_block (&se->post, tmp);

  se->expr = var;
  se->string_length = len;
}


/* Get the minimum/maximum value of all the parameters.
    minmax (a1, a2, a3, ...)
    {
      mvar = a1;
      if (a2 .op. mvar || isnan(mvar))
        mvar = a2;
      if (a3 .op. mvar || isnan(mvar))
        mvar = a3;
      ...
      return mvar
    }
 */

/* TODO: Mismatching types can occur when specific names are used.
   These should be handled during resolution.  */
static void
gfc_conv_intrinsic_minmax (gfc_se * se, gfc_expr * expr, enum tree_code op)
{
  tree tmp;
  tree mvar;
  tree val;
  tree thencase;
  tree *args;
  tree type;
  gfc_actual_arglist *argexpr;
  unsigned int i, nargs;

  nargs = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, nargs);

  gfc_conv_intrinsic_function_args (se, expr, args, nargs);
  type = gfc_typenode_for_spec (&expr->ts);

  argexpr = expr->value.function.actual;
  if (TREE_TYPE (args[0]) != type)
    args[0] = convert (type, args[0]);
  /* Only evaluate the argument once.  */
  if (TREE_CODE (args[0]) != VAR_DECL && !TREE_CONSTANT (args[0]))
    args[0] = gfc_evaluate_now (args[0], &se->pre);

  mvar = gfc_create_var (type, "M");
  gfc_add_modify (&se->pre, mvar, args[0]);
  for (i = 1, argexpr = argexpr->next; i < nargs; i++)
    {
      tree cond, isnan;

      val = args[i]; 

      /* Handle absent optional arguments by ignoring the comparison.  */
      if (argexpr->expr->expr_type == EXPR_VARIABLE
	  && argexpr->expr->symtree->n.sym->attr.optional
	  && TREE_CODE (val) == INDIRECT_REF)
	cond = fold_build2_loc (input_location,
				NE_EXPR, boolean_type_node,
				TREE_OPERAND (val, 0), 
			build_int_cst (TREE_TYPE (TREE_OPERAND (val, 0)), 0));
      else
      {
	cond = NULL_TREE;

	/* Only evaluate the argument once.  */
	if (TREE_CODE (val) != VAR_DECL && !TREE_CONSTANT (val))
	  val = gfc_evaluate_now (val, &se->pre);
      }

      thencase = build2_v (MODIFY_EXPR, mvar, convert (type, val));

      tmp = fold_build2_loc (input_location, op, boolean_type_node,
			     convert (type, val), mvar);

      /* FIXME: When the IEEE_ARITHMETIC module is implemented, the call to
	 __builtin_isnan might be made dependent on that module being loaded,
	 to help performance of programs that don't rely on IEEE semantics.  */
      if (FLOAT_TYPE_P (TREE_TYPE (mvar)))
	{
	  isnan = build_call_expr_loc (input_location,
				       builtin_decl_explicit (BUILT_IN_ISNAN),
				       1, mvar);
	  tmp = fold_build2_loc (input_location, TRUTH_OR_EXPR,
				 boolean_type_node, tmp,
				 fold_convert (boolean_type_node, isnan));
	}
      tmp = build3_v (COND_EXPR, tmp, thencase,
		      build_empty_stmt (input_location));

      if (cond != NULL_TREE)
	tmp = build3_v (COND_EXPR, cond, tmp,
			build_empty_stmt (input_location));

      gfc_add_expr_to_block (&se->pre, tmp);
      argexpr = argexpr->next;
    }
  se->expr = mvar;
}


/* Generate library calls for MIN and MAX intrinsics for character
   variables.  */
static void
gfc_conv_intrinsic_minmax_char (gfc_se * se, gfc_expr * expr, int op)
{
  tree *args;
  tree var, len, fndecl, tmp, cond, function;
  unsigned int nargs;

  nargs = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, nargs + 4);
  gfc_conv_intrinsic_function_args (se, expr, &args[4], nargs);

  /* Create the result variables.  */
  len = gfc_create_var (gfc_charlen_type_node, "len");
  args[0] = gfc_build_addr_expr (NULL_TREE, len);
  var = gfc_create_var (gfc_get_pchar_type (expr->ts.kind), "pstr");
  args[1] = gfc_build_addr_expr (ppvoid_type_node, var);
  args[2] = build_int_cst (integer_type_node, op);
  args[3] = build_int_cst (integer_type_node, nargs / 2);

  if (expr->ts.kind == 1)
    function = gfor_fndecl_string_minmax;
  else if (expr->ts.kind == 4)
    function = gfor_fndecl_string_minmax_char4;
  else
    gcc_unreachable ();

  /* Make the function call.  */
  fndecl = build_addr (function, current_function_decl);
  tmp = build_call_array_loc (input_location,
			  TREE_TYPE (TREE_TYPE (function)), fndecl,
			  nargs + 4, args);
  gfc_add_expr_to_block (&se->pre, tmp);

  /* Free the temporary afterwards, if necessary.  */
  cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			  len, build_int_cst (TREE_TYPE (len), 0));
  tmp = gfc_call_free (var);
  tmp = build3_v (COND_EXPR, cond, tmp, build_empty_stmt (input_location));
  gfc_add_expr_to_block (&se->post, tmp);

  se->expr = var;
  se->string_length = len;
}


/* Create a symbol node for this intrinsic.  The symbol from the frontend
   has the generic name.  */

static gfc_symbol *
gfc_get_symbol_for_expr (gfc_expr * expr)
{
  gfc_symbol *sym;

  /* TODO: Add symbols for intrinsic function to the global namespace.  */
  gcc_assert (strlen (expr->value.function.name) <= GFC_MAX_SYMBOL_LEN - 5);
  sym = gfc_new_symbol (expr->value.function.name, NULL);

  sym->ts = expr->ts;
  sym->attr.external = 1;
  sym->attr.function = 1;
  sym->attr.always_explicit = 1;
  sym->attr.proc = PROC_INTRINSIC;
  sym->attr.flavor = FL_PROCEDURE;
  sym->result = sym;
  if (expr->rank > 0)
    {
      sym->attr.dimension = 1;
      sym->as = gfc_get_array_spec ();
      sym->as->type = AS_ASSUMED_SHAPE;
      sym->as->rank = expr->rank;
    }

  gfc_copy_formal_args_intr (sym, expr->value.function.isym);

  return sym;
}

/* Generate a call to an external intrinsic function.  */
static void
gfc_conv_intrinsic_funcall (gfc_se * se, gfc_expr * expr)
{
  gfc_symbol *sym;
  vec<tree, va_gc> *append_args;

  gcc_assert (!se->ss || se->ss->info->expr == expr);

  if (se->ss)
    gcc_assert (expr->rank > 0);
  else
    gcc_assert (expr->rank == 0);

  sym = gfc_get_symbol_for_expr (expr);

  /* Calls to libgfortran_matmul need to be appended special arguments,
     to be able to call the BLAS ?gemm functions if required and possible.  */
  append_args = NULL;
  if (expr->value.function.isym->id == GFC_ISYM_MATMUL
      && sym->ts.type != BT_LOGICAL)
    {
      tree cint = gfc_get_int_type (gfc_c_int_kind);

      if (gfc_option.flag_external_blas
	  && (sym->ts.type == BT_REAL || sym->ts.type == BT_COMPLEX)
	  && (sym->ts.kind == 4 || sym->ts.kind == 8))
	{
	  tree gemm_fndecl;

	  if (sym->ts.type == BT_REAL)
	    {
	      if (sym->ts.kind == 4)
		gemm_fndecl = gfor_fndecl_sgemm;
	      else
		gemm_fndecl = gfor_fndecl_dgemm;
	    }
	  else
	    {
	      if (sym->ts.kind == 4)
		gemm_fndecl = gfor_fndecl_cgemm;
	      else
		gemm_fndecl = gfor_fndecl_zgemm;
	    }

	  vec_alloc (append_args, 3);
	  append_args->quick_push (build_int_cst (cint, 1));
	  append_args->quick_push (build_int_cst (cint,
		                                 gfc_option.blas_matmul_limit));
	  append_args->quick_push (gfc_build_addr_expr (NULL_TREE,
							gemm_fndecl));
	}
      else
	{
	  vec_alloc (append_args, 3);
	  append_args->quick_push (build_int_cst (cint, 0));
	  append_args->quick_push (build_int_cst (cint, 0));
	  append_args->quick_push (null_pointer_node);
	}
    }

  gfc_conv_procedure_call (se, sym, expr->value.function.actual, expr,
			  append_args);
  gfc_free_symbol (sym);
}

/* ANY and ALL intrinsics. ANY->op == NE_EXPR, ALL->op == EQ_EXPR.
   Implemented as
    any(a)
    {
      forall (i=...)
        if (a[i] != 0)
          return 1
      end forall
      return 0
    }
    all(a)
    {
      forall (i=...)
        if (a[i] == 0)
          return 0
      end forall
      return 1
    }
 */
static void
gfc_conv_intrinsic_anyall (gfc_se * se, gfc_expr * expr, enum tree_code op)
{
  tree resvar;
  stmtblock_t block;
  stmtblock_t body;
  tree type;
  tree tmp;
  tree found;
  gfc_loopinfo loop;
  gfc_actual_arglist *actual;
  gfc_ss *arrayss;
  gfc_se arrayse;
  tree exit_label;

  if (se->ss)
    {
      gfc_conv_intrinsic_funcall (se, expr);
      return;
    }

  actual = expr->value.function.actual;
  type = gfc_typenode_for_spec (&expr->ts);
  /* Initialize the result.  */
  resvar = gfc_create_var (type, "test");
  if (op == EQ_EXPR)
    tmp = convert (type, boolean_true_node);
  else
    tmp = convert (type, boolean_false_node);
  gfc_add_modify (&se->pre, resvar, tmp);

  /* Walk the arguments.  */
  arrayss = gfc_walk_expr (actual->expr);
  gcc_assert (arrayss != gfc_ss_terminator);

  /* Initialize the scalarizer.  */
  gfc_init_loopinfo (&loop);
  exit_label = gfc_build_label_decl (NULL_TREE);
  TREE_USED (exit_label) = 1;
  gfc_add_ss_to_loop (&loop, arrayss);

  /* Initialize the loop.  */
  gfc_conv_ss_startstride (&loop);
  gfc_conv_loop_setup (&loop, &expr->where);

  gfc_mark_ss_chain_used (arrayss, 1);
  /* Generate the loop body.  */
  gfc_start_scalarized_body (&loop, &body);

  /* If the condition matches then set the return value.  */
  gfc_start_block (&block);
  if (op == EQ_EXPR)
    tmp = convert (type, boolean_false_node);
  else
    tmp = convert (type, boolean_true_node);
  gfc_add_modify (&block, resvar, tmp);

  /* And break out of the loop.  */
  tmp = build1_v (GOTO_EXPR, exit_label);
  gfc_add_expr_to_block (&block, tmp);

  found = gfc_finish_block (&block);

  /* Check this element.  */
  gfc_init_se (&arrayse, NULL);
  gfc_copy_loopinfo_to_se (&arrayse, &loop);
  arrayse.ss = arrayss;
  gfc_conv_expr_val (&arrayse, actual->expr);

  gfc_add_block_to_block (&body, &arrayse.pre);
  tmp = fold_build2_loc (input_location, op, boolean_type_node, arrayse.expr,
			 build_int_cst (TREE_TYPE (arrayse.expr), 0));
  tmp = build3_v (COND_EXPR, tmp, found, build_empty_stmt (input_location));
  gfc_add_expr_to_block (&body, tmp);
  gfc_add_block_to_block (&body, &arrayse.post);

  gfc_trans_scalarizing_loops (&loop, &body);

  /* Add the exit label.  */
  tmp = build1_v (LABEL_EXPR, exit_label);
  gfc_add_expr_to_block (&loop.pre, tmp);

  gfc_add_block_to_block (&se->pre, &loop.pre);
  gfc_add_block_to_block (&se->pre, &loop.post);
  gfc_cleanup_loop (&loop);

  se->expr = resvar;
}

/* COUNT(A) = Number of true elements in A.  */
static void
gfc_conv_intrinsic_count (gfc_se * se, gfc_expr * expr)
{
  tree resvar;
  tree type;
  stmtblock_t body;
  tree tmp;
  gfc_loopinfo loop;
  gfc_actual_arglist *actual;
  gfc_ss *arrayss;
  gfc_se arrayse;

  if (se->ss)
    {
      gfc_conv_intrinsic_funcall (se, expr);
      return;
    }

  actual = expr->value.function.actual;

  type = gfc_typenode_for_spec (&expr->ts);
  /* Initialize the result.  */
  resvar = gfc_create_var (type, "count");
  gfc_add_modify (&se->pre, resvar, build_int_cst (type, 0));

  /* Walk the arguments.  */
  arrayss = gfc_walk_expr (actual->expr);
  gcc_assert (arrayss != gfc_ss_terminator);

  /* Initialize the scalarizer.  */
  gfc_init_loopinfo (&loop);
  gfc_add_ss_to_loop (&loop, arrayss);

  /* Initialize the loop.  */
  gfc_conv_ss_startstride (&loop);
  gfc_conv_loop_setup (&loop, &expr->where);

  gfc_mark_ss_chain_used (arrayss, 1);
  /* Generate the loop body.  */
  gfc_start_scalarized_body (&loop, &body);

  tmp = fold_build2_loc (input_location, PLUS_EXPR, TREE_TYPE (resvar),
			 resvar, build_int_cst (TREE_TYPE (resvar), 1));
  tmp = build2_v (MODIFY_EXPR, resvar, tmp);

  gfc_init_se (&arrayse, NULL);
  gfc_copy_loopinfo_to_se (&arrayse, &loop);
  arrayse.ss = arrayss;
  gfc_conv_expr_val (&arrayse, actual->expr);
  tmp = build3_v (COND_EXPR, arrayse.expr, tmp,
		  build_empty_stmt (input_location));

  gfc_add_block_to_block (&body, &arrayse.pre);
  gfc_add_expr_to_block (&body, tmp);
  gfc_add_block_to_block (&body, &arrayse.post);

  gfc_trans_scalarizing_loops (&loop, &body);

  gfc_add_block_to_block (&se->pre, &loop.pre);
  gfc_add_block_to_block (&se->pre, &loop.post);
  gfc_cleanup_loop (&loop);

  se->expr = resvar;
}


/* Update given gfc_se to have ss component pointing to the nested gfc_ss
   struct and return the corresponding loopinfo.  */

static gfc_loopinfo *
enter_nested_loop (gfc_se *se)
{
  se->ss = se->ss->nested_ss;
  gcc_assert (se->ss == se->ss->loop->ss);

  return se->ss->loop;
}


/* Inline implementation of the sum and product intrinsics.  */
static void
gfc_conv_intrinsic_arith (gfc_se * se, gfc_expr * expr, enum tree_code op,
			  bool norm2)
{
  tree resvar;
  tree scale = NULL_TREE;
  tree type;
  stmtblock_t body;
  stmtblock_t block;
  tree tmp;
  gfc_loopinfo loop, *ploop;
  gfc_actual_arglist *arg_array, *arg_mask;
  gfc_ss *arrayss = NULL;
  gfc_ss *maskss = NULL;
  gfc_se arrayse;
  gfc_se maskse;
  gfc_se *parent_se;
  gfc_expr *arrayexpr;
  gfc_expr *maskexpr;

  if (expr->rank > 0)
    {
      gcc_assert (gfc_inline_intrinsic_function_p (expr));
      parent_se = se;
    }
  else
    parent_se = NULL;

  type = gfc_typenode_for_spec (&expr->ts);
  /* Initialize the result.  */
  resvar = gfc_create_var (type, "val");
  if (norm2)
    {
      /* result = 0.0;
	 scale = 1.0.  */
      scale = gfc_create_var (type, "scale");
      gfc_add_modify (&se->pre, scale,
		      gfc_build_const (type, integer_one_node));
      tmp = gfc_build_const (type, integer_zero_node);
    }
  else if (op == PLUS_EXPR || op == BIT_IOR_EXPR || op == BIT_XOR_EXPR)
    tmp = gfc_build_const (type, integer_zero_node);
  else if (op == NE_EXPR)
    /* PARITY.  */
    tmp = convert (type, boolean_false_node);
  else if (op == BIT_AND_EXPR)
    tmp = gfc_build_const (type, fold_build1_loc (input_location, NEGATE_EXPR,
						  type, integer_one_node));
  else
    tmp = gfc_build_const (type, integer_one_node);

  gfc_add_modify (&se->pre, resvar, tmp);

  arg_array = expr->value.function.actual;

  arrayexpr = arg_array->expr;

  if (op == NE_EXPR || norm2)
    /* PARITY and NORM2.  */
    maskexpr = NULL;
  else
    {
      arg_mask  = arg_array->next->next;
      gcc_assert (arg_mask != NULL);
      maskexpr = arg_mask->expr;
    }

  if (expr->rank == 0)
    {
      /* Walk the arguments.  */
      arrayss = gfc_walk_expr (arrayexpr);
      gcc_assert (arrayss != gfc_ss_terminator);

      if (maskexpr && maskexpr->rank > 0)
	{
	  maskss = gfc_walk_expr (maskexpr);
	  gcc_assert (maskss != gfc_ss_terminator);
	}
      else
	maskss = NULL;

      /* Initialize the scalarizer.  */
      gfc_init_loopinfo (&loop);
      gfc_add_ss_to_loop (&loop, arrayss);
      if (maskexpr && maskexpr->rank > 0)
	gfc_add_ss_to_loop (&loop, maskss);

      /* Initialize the loop.  */
      gfc_conv_ss_startstride (&loop);
      gfc_conv_loop_setup (&loop, &expr->where);

      gfc_mark_ss_chain_used (arrayss, 1);
      if (maskexpr && maskexpr->rank > 0)
	gfc_mark_ss_chain_used (maskss, 1);

      ploop = &loop;
    }
  else
    /* All the work has been done in the parent loops.  */
    ploop = enter_nested_loop (se);

  gcc_assert (ploop);

  /* Generate the loop body.  */
  gfc_start_scalarized_body (ploop, &body);

  /* If we have a mask, only add this element if the mask is set.  */
  if (maskexpr && maskexpr->rank > 0)
    {
      gfc_init_se (&maskse, parent_se);
      gfc_copy_loopinfo_to_se (&maskse, ploop);
      if (expr->rank == 0)
	maskse.ss = maskss;
      gfc_conv_expr_val (&maskse, maskexpr);
      gfc_add_block_to_block (&body, &maskse.pre);

      gfc_start_block (&block);
    }
  else
    gfc_init_block (&block);

  /* Do the actual summation/product.  */
  gfc_init_se (&arrayse, parent_se);
  gfc_copy_loopinfo_to_se (&arrayse, ploop);
  if (expr->rank == 0)
    arrayse.ss = arrayss;
  gfc_conv_expr_val (&arrayse, arrayexpr);
  gfc_add_block_to_block (&block, &arrayse.pre);

  if (norm2)
    {
      /* if (x(i) != 0.0)
	   {
	     absX = abs(x(i))
	     if (absX > scale)
	       {
                 val = scale/absX;
		 result = 1.0 + result * val * val;
		 scale = absX;
	       }
	     else
	       {
                 val = absX/scale;
	         result += val * val;
	       }
	   }  */
      tree res1, res2, cond, absX, val;
      stmtblock_t ifblock1, ifblock2, ifblock3;

      gfc_init_block (&ifblock1);

      absX = gfc_create_var (type, "absX");
      gfc_add_modify (&ifblock1, absX,
		      fold_build1_loc (input_location, ABS_EXPR, type,
				       arrayse.expr));
      val = gfc_create_var (type, "val");
      gfc_add_expr_to_block (&ifblock1, val);

      gfc_init_block (&ifblock2);
      gfc_add_modify (&ifblock2, val,
		      fold_build2_loc (input_location, RDIV_EXPR, type, scale,
				       absX));
      res1 = fold_build2_loc (input_location, MULT_EXPR, type, val, val); 
      res1 = fold_build2_loc (input_location, MULT_EXPR, type, resvar, res1);
      res1 = fold_build2_loc (input_location, PLUS_EXPR, type, res1,
			      gfc_build_const (type, integer_one_node));
      gfc_add_modify (&ifblock2, resvar, res1);
      gfc_add_modify (&ifblock2, scale, absX);
      res1 = gfc_finish_block (&ifblock2); 

      gfc_init_block (&ifblock3);
      gfc_add_modify (&ifblock3, val,
		      fold_build2_loc (input_location, RDIV_EXPR, type, absX,
				       scale));
      res2 = fold_build2_loc (input_location, MULT_EXPR, type, val, val); 
      res2 = fold_build2_loc (input_location, PLUS_EXPR, type, resvar, res2);
      gfc_add_modify (&ifblock3, resvar, res2);
      res2 = gfc_finish_block (&ifblock3);

      cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			      absX, scale);
      tmp = build3_v (COND_EXPR, cond, res1, res2);
      gfc_add_expr_to_block (&ifblock1, tmp);  
      tmp = gfc_finish_block (&ifblock1);

      cond = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
			      arrayse.expr,
			      gfc_build_const (type, integer_zero_node));

      tmp = build3_v (COND_EXPR, cond, tmp, build_empty_stmt (input_location));
      gfc_add_expr_to_block (&block, tmp);  
    }
  else
    {
      tmp = fold_build2_loc (input_location, op, type, resvar, arrayse.expr);
      gfc_add_modify (&block, resvar, tmp);
    }

  gfc_add_block_to_block (&block, &arrayse.post);

  if (maskexpr && maskexpr->rank > 0)
    {
      /* We enclose the above in if (mask) {...} .  */

      tmp = gfc_finish_block (&block);
      tmp = build3_v (COND_EXPR, maskse.expr, tmp,
		      build_empty_stmt (input_location));
    }
  else
    tmp = gfc_finish_block (&block);
  gfc_add_expr_to_block (&body, tmp);

  gfc_trans_scalarizing_loops (ploop, &body);

  /* For a scalar mask, enclose the loop in an if statement.  */
  if (maskexpr && maskexpr->rank == 0)
    {
      gfc_init_block (&block);
      gfc_add_block_to_block (&block, &ploop->pre);
      gfc_add_block_to_block (&block, &ploop->post);
      tmp = gfc_finish_block (&block);

      if (expr->rank > 0)
	{
	  tmp = build3_v (COND_EXPR, se->ss->info->data.scalar.value, tmp,
			  build_empty_stmt (input_location));
	  gfc_advance_se_ss_chain (se);
	}
      else
	{
	  gcc_assert (expr->rank == 0);
	  gfc_init_se (&maskse, NULL);
	  gfc_conv_expr_val (&maskse, maskexpr);
	  tmp = build3_v (COND_EXPR, maskse.expr, tmp,
			  build_empty_stmt (input_location));
	}

      gfc_add_expr_to_block (&block, tmp);
      gfc_add_block_to_block (&se->pre, &block);
      gcc_assert (se->post.head == NULL);
    }
  else
    {
      gfc_add_block_to_block (&se->pre, &ploop->pre);
      gfc_add_block_to_block (&se->pre, &ploop->post);
    }

  if (expr->rank == 0)
    gfc_cleanup_loop (ploop);

  if (norm2)
    {
      /* result = scale * sqrt(result).  */
      tree sqrt;
      sqrt = gfc_builtin_decl_for_float_kind (BUILT_IN_SQRT, expr->ts.kind);
      resvar = build_call_expr_loc (input_location,
				    sqrt, 1, resvar);
      resvar = fold_build2_loc (input_location, MULT_EXPR, type, scale, resvar);
    }

  se->expr = resvar;
}


/* Inline implementation of the dot_product intrinsic. This function
   is based on gfc_conv_intrinsic_arith (the previous function).  */
static void
gfc_conv_intrinsic_dot_product (gfc_se * se, gfc_expr * expr)
{
  tree resvar;
  tree type;
  stmtblock_t body;
  stmtblock_t block;
  tree tmp;
  gfc_loopinfo loop;
  gfc_actual_arglist *actual;
  gfc_ss *arrayss1, *arrayss2;
  gfc_se arrayse1, arrayse2;
  gfc_expr *arrayexpr1, *arrayexpr2;

  type = gfc_typenode_for_spec (&expr->ts);

  /* Initialize the result.  */
  resvar = gfc_create_var (type, "val");
  if (expr->ts.type == BT_LOGICAL)
    tmp = build_int_cst (type, 0);
  else
    tmp = gfc_build_const (type, integer_zero_node);

  gfc_add_modify (&se->pre, resvar, tmp);

  /* Walk argument #1.  */
  actual = expr->value.function.actual;
  arrayexpr1 = actual->expr;
  arrayss1 = gfc_walk_expr (arrayexpr1);
  gcc_assert (arrayss1 != gfc_ss_terminator);

  /* Walk argument #2.  */
  actual = actual->next;
  arrayexpr2 = actual->expr;
  arrayss2 = gfc_walk_expr (arrayexpr2);
  gcc_assert (arrayss2 != gfc_ss_terminator);

  /* Initialize the scalarizer.  */
  gfc_init_loopinfo (&loop);
  gfc_add_ss_to_loop (&loop, arrayss1);
  gfc_add_ss_to_loop (&loop, arrayss2);

  /* Initialize the loop.  */
  gfc_conv_ss_startstride (&loop);
  gfc_conv_loop_setup (&loop, &expr->where);

  gfc_mark_ss_chain_used (arrayss1, 1);
  gfc_mark_ss_chain_used (arrayss2, 1);

  /* Generate the loop body.  */
  gfc_start_scalarized_body (&loop, &body);
  gfc_init_block (&block);

  /* Make the tree expression for [conjg(]array1[)].  */
  gfc_init_se (&arrayse1, NULL);
  gfc_copy_loopinfo_to_se (&arrayse1, &loop);
  arrayse1.ss = arrayss1;
  gfc_conv_expr_val (&arrayse1, arrayexpr1);
  if (expr->ts.type == BT_COMPLEX)
    arrayse1.expr = fold_build1_loc (input_location, CONJ_EXPR, type,
				     arrayse1.expr);
  gfc_add_block_to_block (&block, &arrayse1.pre);

  /* Make the tree expression for array2.  */
  gfc_init_se (&arrayse2, NULL);
  gfc_copy_loopinfo_to_se (&arrayse2, &loop);
  arrayse2.ss = arrayss2;
  gfc_conv_expr_val (&arrayse2, arrayexpr2);
  gfc_add_block_to_block (&block, &arrayse2.pre);

  /* Do the actual product and sum.  */
  if (expr->ts.type == BT_LOGICAL)
    {
      tmp = fold_build2_loc (input_location, TRUTH_AND_EXPR, type,
			     arrayse1.expr, arrayse2.expr);
      tmp = fold_build2_loc (input_location, TRUTH_OR_EXPR, type, resvar, tmp);
    }
  else
    {
      tmp = fold_build2_loc (input_location, MULT_EXPR, type, arrayse1.expr,
			     arrayse2.expr);
      tmp = fold_build2_loc (input_location, PLUS_EXPR, type, resvar, tmp);
    }
  gfc_add_modify (&block, resvar, tmp);

  /* Finish up the loop block and the loop.  */
  tmp = gfc_finish_block (&block);
  gfc_add_expr_to_block (&body, tmp);

  gfc_trans_scalarizing_loops (&loop, &body);
  gfc_add_block_to_block (&se->pre, &loop.pre);
  gfc_add_block_to_block (&se->pre, &loop.post);
  gfc_cleanup_loop (&loop);

  se->expr = resvar;
}


/* Emit code for minloc or maxloc intrinsic.  There are many different cases
   we need to handle.  For performance reasons we sometimes create two
   loops instead of one, where the second one is much simpler.
   Examples for minloc intrinsic:
   1) Result is an array, a call is generated
   2) Array mask is used and NaNs need to be supported:
      limit = Infinity;
      pos = 0;
      S = from;
      while (S <= to) {
	if (mask[S]) {
	  if (pos == 0) pos = S + (1 - from);
	  if (a[S] <= limit) { limit = a[S]; pos = S + (1 - from); goto lab1; }
	}
	S++;
      }
      goto lab2;
      lab1:;
      while (S <= to) {
	if (mask[S]) if (a[S] < limit) { limit = a[S]; pos = S + (1 - from); }
	S++;
      }
      lab2:;
   3) NaNs need to be supported, but it is known at compile time or cheaply
      at runtime whether array is nonempty or not:
      limit = Infinity;
      pos = 0;
      S = from;
      while (S <= to) {
	if (a[S] <= limit) { limit = a[S]; pos = S + (1 - from); goto lab1; }
	S++;
      }
      if (from <= to) pos = 1;
      goto lab2;
      lab1:;
      while (S <= to) {
	if (a[S] < limit) { limit = a[S]; pos = S + (1 - from); }
	S++;
      }
      lab2:;
   4) NaNs aren't supported, array mask is used:
      limit = infinities_supported ? Infinity : huge (limit);
      pos = 0;
      S = from;
      while (S <= to) {
	if (mask[S]) { limit = a[S]; pos = S + (1 - from); goto lab1; }
	S++;
      }
      goto lab2;
      lab1:;
      while (S <= to) {
	if (mask[S]) if (a[S] < limit) { limit = a[S]; pos = S + (1 - from); }
	S++;
      }
      lab2:;
   5) Same without array mask:
      limit = infinities_supported ? Infinity : huge (limit);
      pos = (from <= to) ? 1 : 0;
      S = from;
      while (S <= to) {
	if (a[S] < limit) { limit = a[S]; pos = S + (1 - from); }
	S++;
      }
   For 3) and 5), if mask is scalar, this all goes into a conditional,
   setting pos = 0; in the else branch.  */

static void
gfc_conv_intrinsic_minmaxloc (gfc_se * se, gfc_expr * expr, enum tree_code op)
{
  stmtblock_t body;
  stmtblock_t block;
  stmtblock_t ifblock;
  stmtblock_t elseblock;
  tree limit;
  tree type;
  tree tmp;
  tree cond;
  tree elsetmp;
  tree ifbody;
  tree offset;
  tree nonempty;
  tree lab1, lab2;
  gfc_loopinfo loop;
  gfc_actual_arglist *actual;
  gfc_ss *arrayss;
  gfc_ss *maskss;
  gfc_se arrayse;
  gfc_se maskse;
  gfc_expr *arrayexpr;
  gfc_expr *maskexpr;
  tree pos;
  int n;

  if (se->ss)
    {
      gfc_conv_intrinsic_funcall (se, expr);
      return;
    }

  /* Initialize the result.  */
  pos = gfc_create_var (gfc_array_index_type, "pos");
  offset = gfc_create_var (gfc_array_index_type, "offset");
  type = gfc_typenode_for_spec (&expr->ts);

  /* Walk the arguments.  */
  actual = expr->value.function.actual;
  arrayexpr = actual->expr;
  arrayss = gfc_walk_expr (arrayexpr);
  gcc_assert (arrayss != gfc_ss_terminator);

  actual = actual->next->next;
  gcc_assert (actual);
  maskexpr = actual->expr;
  nonempty = NULL;
  if (maskexpr && maskexpr->rank != 0)
    {
      maskss = gfc_walk_expr (maskexpr);
      gcc_assert (maskss != gfc_ss_terminator);
    }
  else
    {
      mpz_t asize;
      if (gfc_array_size (arrayexpr, &asize) == SUCCESS)
	{
	  nonempty = gfc_conv_mpz_to_tree (asize, gfc_index_integer_kind);
	  mpz_clear (asize);
	  nonempty = fold_build2_loc (input_location, GT_EXPR,
				      boolean_type_node, nonempty,
				      gfc_index_zero_node);
	}
      maskss = NULL;
    }

  limit = gfc_create_var (gfc_typenode_for_spec (&arrayexpr->ts), "limit");
  switch (arrayexpr->ts.type)
    {
    case BT_REAL:
      tmp = gfc_build_inf_or_huge (TREE_TYPE (limit), arrayexpr->ts.kind);
      break;

    case BT_INTEGER:
      n = gfc_validate_kind (arrayexpr->ts.type, arrayexpr->ts.kind, false);
      tmp = gfc_conv_mpz_to_tree (gfc_integer_kinds[n].huge,
				  arrayexpr->ts.kind);
      break;

    default:
      gcc_unreachable ();
    }

  /* We start with the most negative possible value for MAXLOC, and the most
     positive possible value for MINLOC. The most negative possible value is
     -HUGE for BT_REAL and (-HUGE - 1) for BT_INTEGER; the most positive
     possible value is HUGE in both cases.  */
  if (op == GT_EXPR)
    tmp = fold_build1_loc (input_location, NEGATE_EXPR, TREE_TYPE (tmp), tmp);
  if (op == GT_EXPR && expr->ts.type == BT_INTEGER)
    tmp = fold_build2_loc (input_location, MINUS_EXPR, TREE_TYPE (tmp), tmp,
			   build_int_cst (type, 1));

  gfc_add_modify (&se->pre, limit, tmp);

  /* Initialize the scalarizer.  */
  gfc_init_loopinfo (&loop);
  gfc_add_ss_to_loop (&loop, arrayss);
  if (maskss)
    gfc_add_ss_to_loop (&loop, maskss);

  /* Initialize the loop.  */
  gfc_conv_ss_startstride (&loop);

  /* The code generated can have more than one loop in sequence (see the
     comment at the function header).  This doesn't work well with the
     scalarizer, which changes arrays' offset when the scalarization loops
     are generated (see gfc_trans_preloop_setup).  Fortunately, {min,max}loc
     are  currently inlined in the scalar case only (for which loop is of rank
     one).  As there is no dependency to care about in that case, there is no
     temporary, so that we can use the scalarizer temporary code to handle
     multiple loops.  Thus, we set temp_dim here, we call gfc_mark_ss_chain_used
     with flag=3 later, and we use gfc_trans_scalarized_loop_boundary even later
     to restore offset.
     TODO: this prevents inlining of rank > 0 minmaxloc calls, so this
     should eventually go away.  We could either create two loops properly,
     or find another way to save/restore the array offsets between the two
     loops (without conflicting with temporary management), or use a single
     loop minmaxloc implementation.  See PR 31067.  */
  loop.temp_dim = loop.dimen;
  gfc_conv_loop_setup (&loop, &expr->where);

  gcc_assert (loop.dimen == 1);
  if (nonempty == NULL && maskss == NULL && loop.from[0] && loop.to[0])
    nonempty = fold_build2_loc (input_location, LE_EXPR, boolean_type_node,
				loop.from[0], loop.to[0]);

  lab1 = NULL;
  lab2 = NULL;
  /* Initialize the position to zero, following Fortran 2003.  We are free
     to do this because Fortran 95 allows the result of an entirely false
     mask to be processor dependent.  If we know at compile time the array
     is non-empty and no MASK is used, we can initialize to 1 to simplify
     the inner loop.  */
  if (nonempty != NULL && !HONOR_NANS (DECL_MODE (limit)))
    gfc_add_modify (&loop.pre, pos,
		    fold_build3_loc (input_location, COND_EXPR,
				     gfc_array_index_type,
				     nonempty, gfc_index_one_node,
				     gfc_index_zero_node));
  else
    {
      gfc_add_modify (&loop.pre, pos, gfc_index_zero_node);
      lab1 = gfc_build_label_decl (NULL_TREE);
      TREE_USED (lab1) = 1;
      lab2 = gfc_build_label_decl (NULL_TREE);
      TREE_USED (lab2) = 1;
    }

  /* An offset must be added to the loop
     counter to obtain the required position.  */
  gcc_assert (loop.from[0]);

  tmp = fold_build2_loc (input_location, MINUS_EXPR, gfc_array_index_type,
			 gfc_index_one_node, loop.from[0]);
  gfc_add_modify (&loop.pre, offset, tmp);

  gfc_mark_ss_chain_used (arrayss, lab1 ? 3 : 1);
  if (maskss)
    gfc_mark_ss_chain_used (maskss, lab1 ? 3 : 1);
  /* Generate the loop body.  */
  gfc_start_scalarized_body (&loop, &body);

  /* If we have a mask, only check this element if the mask is set.  */
  if (maskss)
    {
      gfc_init_se (&maskse, NULL);
      gfc_copy_loopinfo_to_se (&maskse, &loop);
      maskse.ss = maskss;
      gfc_conv_expr_val (&maskse, maskexpr);
      gfc_add_block_to_block (&body, &maskse.pre);

      gfc_start_block (&block);
    }
  else
    gfc_init_block (&block);

  /* Compare with the current limit.  */
  gfc_init_se (&arrayse, NULL);
  gfc_copy_loopinfo_to_se (&arrayse, &loop);
  arrayse.ss = arrayss;
  gfc_conv_expr_val (&arrayse, arrayexpr);
  gfc_add_block_to_block (&block, &arrayse.pre);

  /* We do the following if this is a more extreme value.  */
  gfc_start_block (&ifblock);

  /* Assign the value to the limit...  */
  gfc_add_modify (&ifblock, limit, arrayse.expr);

  if (nonempty == NULL && HONOR_NANS (DECL_MODE (limit)))
    {
      stmtblock_t ifblock2;
      tree ifbody2;

      gfc_start_block (&ifblock2);
      tmp = fold_build2_loc (input_location, PLUS_EXPR, TREE_TYPE (pos),
			     loop.loopvar[0], offset);
      gfc_add_modify (&ifblock2, pos, tmp);
      ifbody2 = gfc_finish_block (&ifblock2);
      cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, pos,
			      gfc_index_zero_node);
      tmp = build3_v (COND_EXPR, cond, ifbody2,
		      build_empty_stmt (input_location));
      gfc_add_expr_to_block (&block, tmp);
    }

  tmp = fold_build2_loc (input_location, PLUS_EXPR, TREE_TYPE (pos),
			 loop.loopvar[0], offset);
  gfc_add_modify (&ifblock, pos, tmp);

  if (lab1)
    gfc_add_expr_to_block (&ifblock, build1_v (GOTO_EXPR, lab1));

  ifbody = gfc_finish_block (&ifblock);

  if (!lab1 || HONOR_NANS (DECL_MODE (limit)))
    {
      if (lab1)
	cond = fold_build2_loc (input_location,
				op == GT_EXPR ? GE_EXPR : LE_EXPR,
				boolean_type_node, arrayse.expr, limit);
      else
	cond = fold_build2_loc (input_location, op, boolean_type_node,
				arrayse.expr, limit);

      ifbody = build3_v (COND_EXPR, cond, ifbody,
			 build_empty_stmt (input_location));
    }
  gfc_add_expr_to_block (&block, ifbody);

  if (maskss)
    {
      /* We enclose the above in if (mask) {...}.  */
      tmp = gfc_finish_block (&block);

      tmp = build3_v (COND_EXPR, maskse.expr, tmp,
		      build_empty_stmt (input_location));
    }
  else
    tmp = gfc_finish_block (&block);
  gfc_add_expr_to_block (&body, tmp);

  if (lab1)
    {
      gfc_trans_scalarized_loop_boundary (&loop, &body);

      if (HONOR_NANS (DECL_MODE (limit)))
	{
	  if (nonempty != NULL)
	    {
	      ifbody = build2_v (MODIFY_EXPR, pos, gfc_index_one_node);
	      tmp = build3_v (COND_EXPR, nonempty, ifbody,
			      build_empty_stmt (input_location));
	      gfc_add_expr_to_block (&loop.code[0], tmp);
	    }
	}

      gfc_add_expr_to_block (&loop.code[0], build1_v (GOTO_EXPR, lab2));
      gfc_add_expr_to_block (&loop.code[0], build1_v (LABEL_EXPR, lab1));

      /* If we have a mask, only check this element if the mask is set.  */
      if (maskss)
	{
	  gfc_init_se (&maskse, NULL);
	  gfc_copy_loopinfo_to_se (&maskse, &loop);
	  maskse.ss = maskss;
	  gfc_conv_expr_val (&maskse, maskexpr);
	  gfc_add_block_to_block (&body, &maskse.pre);

	  gfc_start_block (&block);
	}
      else
	gfc_init_block (&block);

      /* Compare with the current limit.  */
      gfc_init_se (&arrayse, NULL);
      gfc_copy_loopinfo_to_se (&arrayse, &loop);
      arrayse.ss = arrayss;
      gfc_conv_expr_val (&arrayse, arrayexpr);
      gfc_add_block_to_block (&block, &arrayse.pre);

      /* We do the following if this is a more extreme value.  */
      gfc_start_block (&ifblock);

      /* Assign the value to the limit...  */
      gfc_add_modify (&ifblock, limit, arrayse.expr);

      tmp = fold_build2_loc (input_location, PLUS_EXPR, TREE_TYPE (pos),
			     loop.loopvar[0], offset);
      gfc_add_modify (&ifblock, pos, tmp);

      ifbody = gfc_finish_block (&ifblock);

      cond = fold_build2_loc (input_location, op, boolean_type_node,
			      arrayse.expr, limit);

      tmp = build3_v (COND_EXPR, cond, ifbody,
		      build_empty_stmt (input_location));
      gfc_add_expr_to_block (&block, tmp);

      if (maskss)
	{
	  /* We enclose the above in if (mask) {...}.  */
	  tmp = gfc_finish_block (&block);

	  tmp = build3_v (COND_EXPR, maskse.expr, tmp,
			  build_empty_stmt (input_location));
	}
      else
	tmp = gfc_finish_block (&block);
      gfc_add_expr_to_block (&body, tmp);
      /* Avoid initializing loopvar[0] again, it should be left where
	 it finished by the first loop.  */
      loop.from[0] = loop.loopvar[0];
    }

  gfc_trans_scalarizing_loops (&loop, &body);

  if (lab2)
    gfc_add_expr_to_block (&loop.pre, build1_v (LABEL_EXPR, lab2));

  /* For a scalar mask, enclose the loop in an if statement.  */
  if (maskexpr && maskss == NULL)
    {
      gfc_init_se (&maskse, NULL);
      gfc_conv_expr_val (&maskse, maskexpr);
      gfc_init_block (&block);
      gfc_add_block_to_block (&block, &loop.pre);
      gfc_add_block_to_block (&block, &loop.post);
      tmp = gfc_finish_block (&block);

      /* For the else part of the scalar mask, just initialize
	 the pos variable the same way as above.  */

      gfc_init_block (&elseblock);
      gfc_add_modify (&elseblock, pos, gfc_index_zero_node);
      elsetmp = gfc_finish_block (&elseblock);

      tmp = build3_v (COND_EXPR, maskse.expr, tmp, elsetmp);
      gfc_add_expr_to_block (&block, tmp);
      gfc_add_block_to_block (&se->pre, &block);
    }
  else
    {
      gfc_add_block_to_block (&se->pre, &loop.pre);
      gfc_add_block_to_block (&se->pre, &loop.post);
    }
  gfc_cleanup_loop (&loop);

  se->expr = convert (type, pos);
}

/* Emit code for minval or maxval intrinsic.  There are many different cases
   we need to handle.  For performance reasons we sometimes create two
   loops instead of one, where the second one is much simpler.
   Examples for minval intrinsic:
   1) Result is an array, a call is generated
   2) Array mask is used and NaNs need to be supported, rank 1:
      limit = Infinity;
      nonempty = false;
      S = from;
      while (S <= to) {
	if (mask[S]) { nonempty = true; if (a[S] <= limit) goto lab; }
	S++;
      }
      limit = nonempty ? NaN : huge (limit);
      lab:
      while (S <= to) { if(mask[S]) limit = min (a[S], limit); S++; }
   3) NaNs need to be supported, but it is known at compile time or cheaply
      at runtime whether array is nonempty or not, rank 1:
      limit = Infinity;
      S = from;
      while (S <= to) { if (a[S] <= limit) goto lab; S++; }
      limit = (from <= to) ? NaN : huge (limit);
      lab:
      while (S <= to) { limit = min (a[S], limit); S++; }
   4) Array mask is used and NaNs need to be supported, rank > 1:
      limit = Infinity;
      nonempty = false;
      fast = false;
      S1 = from1;
      while (S1 <= to1) {
	S2 = from2;
	while (S2 <= to2) {
	  if (mask[S1][S2]) {
	    if (fast) limit = min (a[S1][S2], limit);
	    else {
	      nonempty = true;
	      if (a[S1][S2] <= limit) {
		limit = a[S1][S2];
		fast = true;
	      }
	    }
	  }
	  S2++;
	}
	S1++;
      }
      if (!fast)
	limit = nonempty ? NaN : huge (limit);
   5) NaNs need to be supported, but it is known at compile time or cheaply
      at runtime whether array is nonempty or not, rank > 1:
      limit = Infinity;
      fast = false;
      S1 = from1;
      while (S1 <= to1) {
	S2 = from2;
	while (S2 <= to2) {
	  if (fast) limit = min (a[S1][S2], limit);
	  else {
	    if (a[S1][S2] <= limit) {
	      limit = a[S1][S2];
	      fast = true;
	    }
	  }
	  S2++;
	}
	S1++;
      }
      if (!fast)
	limit = (nonempty_array) ? NaN : huge (limit);
   6) NaNs aren't supported, but infinities are.  Array mask is used:
      limit = Infinity;
      nonempty = false;
      S = from;
      while (S <= to) {
	if (mask[S]) { nonempty = true; limit = min (a[S], limit); }
	S++;
      }
      limit = nonempty ? limit : huge (limit);
   7) Same without array mask:
      limit = Infinity;
      S = from;
      while (S <= to) { limit = min (a[S], limit); S++; }
      limit = (from <= to) ? limit : huge (limit);
   8) Neither NaNs nor infinities are supported (-ffast-math or BT_INTEGER):
      limit = huge (limit);
      S = from;
      while (S <= to) { limit = min (a[S], limit); S++); }
      (or
      while (S <= to) { if (mask[S]) limit = min (a[S], limit); S++; }
      with array mask instead).
   For 3), 5), 7) and 8), if mask is scalar, this all goes into a conditional,
   setting limit = huge (limit); in the else branch.  */

static void
gfc_conv_intrinsic_minmaxval (gfc_se * se, gfc_expr * expr, enum tree_code op)
{
  tree limit;
  tree type;
  tree tmp;
  tree ifbody;
  tree nonempty;
  tree nonempty_var;
  tree lab;
  tree fast;
  tree huge_cst = NULL, nan_cst = NULL;
  stmtblock_t body;
  stmtblock_t block, block2;
  gfc_loopinfo loop;
  gfc_actual_arglist *actual;
  gfc_ss *arrayss;
  gfc_ss *maskss;
  gfc_se arrayse;
  gfc_se maskse;
  gfc_expr *arrayexpr;
  gfc_expr *maskexpr;
  int n;

  if (se->ss)
    {
      gfc_conv_intrinsic_funcall (se, expr);
      return;
    }

  type = gfc_typenode_for_spec (&expr->ts);
  /* Initialize the result.  */
  limit = gfc_create_var (type, "limit");
  n = gfc_validate_kind (expr->ts.type, expr->ts.kind, false);
  switch (expr->ts.type)
    {
    case BT_REAL:
      huge_cst = gfc_conv_mpfr_to_tree (gfc_real_kinds[n].huge,
					expr->ts.kind, 0);
      if (HONOR_INFINITIES (DECL_MODE (limit)))
	{
	  REAL_VALUE_TYPE real;
	  real_inf (&real);
	  tmp = build_real (type, real);
	}
      else
	tmp = huge_cst;
      if (HONOR_NANS (DECL_MODE (limit)))
	{
	  REAL_VALUE_TYPE real;
	  real_nan (&real, "", 1, DECL_MODE (limit));
	  nan_cst = build_real (type, real);
	}
      break;

    case BT_INTEGER:
      tmp = gfc_conv_mpz_to_tree (gfc_integer_kinds[n].huge, expr->ts.kind);
      break;

    default:
      gcc_unreachable ();
    }

  /* We start with the most negative possible value for MAXVAL, and the most
     positive possible value for MINVAL. The most negative possible value is
     -HUGE for BT_REAL and (-HUGE - 1) for BT_INTEGER; the most positive
     possible value is HUGE in both cases.  */
  if (op == GT_EXPR)
    {
      tmp = fold_build1_loc (input_location, NEGATE_EXPR, TREE_TYPE (tmp), tmp);
      if (huge_cst)
	huge_cst = fold_build1_loc (input_location, NEGATE_EXPR,
				    TREE_TYPE (huge_cst), huge_cst);
    }

  if (op == GT_EXPR && expr->ts.type == BT_INTEGER)
    tmp = fold_build2_loc (input_location, MINUS_EXPR, TREE_TYPE (tmp),
			   tmp, build_int_cst (type, 1));

  gfc_add_modify (&se->pre, limit, tmp);

  /* Walk the arguments.  */
  actual = expr->value.function.actual;
  arrayexpr = actual->expr;
  arrayss = gfc_walk_expr (arrayexpr);
  gcc_assert (arrayss != gfc_ss_terminator);

  actual = actual->next->next;
  gcc_assert (actual);
  maskexpr = actual->expr;
  nonempty = NULL;
  if (maskexpr && maskexpr->rank != 0)
    {
      maskss = gfc_walk_expr (maskexpr);
      gcc_assert (maskss != gfc_ss_terminator);
    }
  else
    {
      mpz_t asize;
      if (gfc_array_size (arrayexpr, &asize) == SUCCESS)
	{
	  nonempty = gfc_conv_mpz_to_tree (asize, gfc_index_integer_kind);
	  mpz_clear (asize);
	  nonempty = fold_build2_loc (input_location, GT_EXPR,
				      boolean_type_node, nonempty,
				      gfc_index_zero_node);
	}
      maskss = NULL;
    }

  /* Initialize the scalarizer.  */
  gfc_init_loopinfo (&loop);
  gfc_add_ss_to_loop (&loop, arrayss);
  if (maskss)
    gfc_add_ss_to_loop (&loop, maskss);

  /* Initialize the loop.  */
  gfc_conv_ss_startstride (&loop);

  /* The code generated can have more than one loop in sequence (see the
     comment at the function header).  This doesn't work well with the
     scalarizer, which changes arrays' offset when the scalarization loops
     are generated (see gfc_trans_preloop_setup).  Fortunately, {min,max}val
     are  currently inlined in the scalar case only.  As there is no dependency
     to care about in that case, there is no temporary, so that we can use the
     scalarizer temporary code to handle multiple loops.  Thus, we set temp_dim
     here, we call gfc_mark_ss_chain_used with flag=3 later, and we use
     gfc_trans_scalarized_loop_boundary even later to restore offset.
     TODO: this prevents inlining of rank > 0 minmaxval calls, so this
     should eventually go away.  We could either create two loops properly,
     or find another way to save/restore the array offsets between the two
     loops (without conflicting with temporary management), or use a single
     loop minmaxval implementation.  See PR 31067.  */
  loop.temp_dim = loop.dimen;
  gfc_conv_loop_setup (&loop, &expr->where);

  if (nonempty == NULL && maskss == NULL
      && loop.dimen == 1 && loop.from[0] && loop.to[0])
    nonempty = fold_build2_loc (input_location, LE_EXPR, boolean_type_node,
				loop.from[0], loop.to[0]);
  nonempty_var = NULL;
  if (nonempty == NULL
      && (HONOR_INFINITIES (DECL_MODE (limit))
	  || HONOR_NANS (DECL_MODE (limit))))
    {
      nonempty_var = gfc_create_var (boolean_type_node, "nonempty");
      gfc_add_modify (&se->pre, nonempty_var, boolean_false_node);
      nonempty = nonempty_var;
    }
  lab = NULL;
  fast = NULL;
  if (HONOR_NANS (DECL_MODE (limit)))
    {
      if (loop.dimen == 1)
	{
	  lab = gfc_build_label_decl (NULL_TREE);
	  TREE_USED (lab) = 1;
	}
      else
	{
	  fast = gfc_create_var (boolean_type_node, "fast");
	  gfc_add_modify (&se->pre, fast, boolean_false_node);
	}
    }

  gfc_mark_ss_chain_used (arrayss, lab ? 3 : 1);
  if (maskss)
    gfc_mark_ss_chain_used (maskss, lab ? 3 : 1);
  /* Generate the loop body.  */
  gfc_start_scalarized_body (&loop, &body);

  /* If we have a mask, only add this element if the mask is set.  */
  if (maskss)
    {
      gfc_init_se (&maskse, NULL);
      gfc_copy_loopinfo_to_se (&maskse, &loop);
      maskse.ss = maskss;
      gfc_conv_expr_val (&maskse, maskexpr);
      gfc_add_block_to_block (&body, &maskse.pre);

      gfc_start_block (&block);
    }
  else
    gfc_init_block (&block);

  /* Compare with the current limit.  */
  gfc_init_se (&arrayse, NULL);
  gfc_copy_loopinfo_to_se (&arrayse, &loop);
  arrayse.ss = arrayss;
  gfc_conv_expr_val (&arrayse, arrayexpr);
  gfc_add_block_to_block (&block, &arrayse.pre);

  gfc_init_block (&block2);

  if (nonempty_var)
    gfc_add_modify (&block2, nonempty_var, boolean_true_node);

  if (HONOR_NANS (DECL_MODE (limit)))
    {
      tmp = fold_build2_loc (input_location, op == GT_EXPR ? GE_EXPR : LE_EXPR,
			     boolean_type_node, arrayse.expr, limit);
      if (lab)
	ifbody = build1_v (GOTO_EXPR, lab);
      else
	{
	  stmtblock_t ifblock;

	  gfc_init_block (&ifblock);
	  gfc_add_modify (&ifblock, limit, arrayse.expr);
	  gfc_add_modify (&ifblock, fast, boolean_true_node);
	  ifbody = gfc_finish_block (&ifblock);
	}
      tmp = build3_v (COND_EXPR, tmp, ifbody,
		      build_empty_stmt (input_location));
      gfc_add_expr_to_block (&block2, tmp);
    }
  else
    {
      /* MIN_EXPR/MAX_EXPR has unspecified behavior with NaNs or
	 signed zeros.  */
      if (HONOR_SIGNED_ZEROS (DECL_MODE (limit)))
	{
	  tmp = fold_build2_loc (input_location, op, boolean_type_node,
				 arrayse.expr, limit);
	  ifbody = build2_v (MODIFY_EXPR, limit, arrayse.expr);
	  tmp = build3_v (COND_EXPR, tmp, ifbody,
			  build_empty_stmt (input_location));
	  gfc_add_expr_to_block (&block2, tmp);
	}
      else
	{
	  tmp = fold_build2_loc (input_location,
				 op == GT_EXPR ? MAX_EXPR : MIN_EXPR,
				 type, arrayse.expr, limit);
	  gfc_add_modify (&block2, limit, tmp);
	}
    }

  if (fast)
    {
      tree elsebody = gfc_finish_block (&block2);

      /* MIN_EXPR/MAX_EXPR has unspecified behavior with NaNs or
	 signed zeros.  */
      if (HONOR_NANS (DECL_MODE (limit))
	  || HONOR_SIGNED_ZEROS (DECL_MODE (limit)))
	{
	  tmp = fold_build2_loc (input_location, op, boolean_type_node,
				 arrayse.expr, limit);
	  ifbody = build2_v (MODIFY_EXPR, limit, arrayse.expr);
	  ifbody = build3_v (COND_EXPR, tmp, ifbody,
			     build_empty_stmt (input_location));
	}
      else
	{
	  tmp = fold_build2_loc (input_location,
				 op == GT_EXPR ? MAX_EXPR : MIN_EXPR,
				 type, arrayse.expr, limit);
	  ifbody = build2_v (MODIFY_EXPR, limit, tmp);
	}
      tmp = build3_v (COND_EXPR, fast, ifbody, elsebody);
      gfc_add_expr_to_block (&block, tmp);
    }
  else
    gfc_add_block_to_block (&block, &block2);

  gfc_add_block_to_block (&block, &arrayse.post);

  tmp = gfc_finish_block (&block);
  if (maskss)
    /* We enclose the above in if (mask) {...}.  */
    tmp = build3_v (COND_EXPR, maskse.expr, tmp,
		    build_empty_stmt (input_location));
  gfc_add_expr_to_block (&body, tmp);

  if (lab)
    {
      gfc_trans_scalarized_loop_boundary (&loop, &body);

      tmp = fold_build3_loc (input_location, COND_EXPR, type, nonempty,
			     nan_cst, huge_cst);
      gfc_add_modify (&loop.code[0], limit, tmp);
      gfc_add_expr_to_block (&loop.code[0], build1_v (LABEL_EXPR, lab));

      /* If we have a mask, only add this element if the mask is set.  */
      if (maskss)
	{
	  gfc_init_se (&maskse, NULL);
	  gfc_copy_loopinfo_to_se (&maskse, &loop);
	  maskse.ss = maskss;
	  gfc_conv_expr_val (&maskse, maskexpr);
	  gfc_add_block_to_block (&body, &maskse.pre);

	  gfc_start_block (&block);
	}
      else
	gfc_init_block (&block);

      /* Compare with the current limit.  */
      gfc_init_se (&arrayse, NULL);
      gfc_copy_loopinfo_to_se (&arrayse, &loop);
      arrayse.ss = arrayss;
      gfc_conv_expr_val (&arrayse, arrayexpr);
      gfc_add_block_to_block (&block, &arrayse.pre);

      /* MIN_EXPR/MAX_EXPR has unspecified behavior with NaNs or
	 signed zeros.  */
      if (HONOR_NANS (DECL_MODE (limit))
	  || HONOR_SIGNED_ZEROS (DECL_MODE (limit)))
	{
	  tmp = fold_build2_loc (input_location, op, boolean_type_node,
				 arrayse.expr, limit);
	  ifbody = build2_v (MODIFY_EXPR, limit, arrayse.expr);
	  tmp = build3_v (COND_EXPR, tmp, ifbody,
			  build_empty_stmt (input_location));
	  gfc_add_expr_to_block (&block, tmp);
	}
      else
	{
	  tmp = fold_build2_loc (input_location,
				 op == GT_EXPR ? MAX_EXPR : MIN_EXPR,
				 type, arrayse.expr, limit);
	  gfc_add_modify (&block, limit, tmp);
	}

      gfc_add_block_to_block (&block, &arrayse.post);

      tmp = gfc_finish_block (&block);
      if (maskss)
	/* We enclose the above in if (mask) {...}.  */
	tmp = build3_v (COND_EXPR, maskse.expr, tmp,
			build_empty_stmt (input_location));
      gfc_add_expr_to_block (&body, tmp);
      /* Avoid initializing loopvar[0] again, it should be left where
	 it finished by the first loop.  */
      loop.from[0] = loop.loopvar[0];
    }
  gfc_trans_scalarizing_loops (&loop, &body);

  if (fast)
    {
      tmp = fold_build3_loc (input_location, COND_EXPR, type, nonempty,
			     nan_cst, huge_cst);
      ifbody = build2_v (MODIFY_EXPR, limit, tmp);
      tmp = build3_v (COND_EXPR, fast, build_empty_stmt (input_location),
		      ifbody);
      gfc_add_expr_to_block (&loop.pre, tmp);
    }
  else if (HONOR_INFINITIES (DECL_MODE (limit)) && !lab)
    {
      tmp = fold_build3_loc (input_location, COND_EXPR, type, nonempty, limit,
			     huge_cst);
      gfc_add_modify (&loop.pre, limit, tmp);
    }

  /* For a scalar mask, enclose the loop in an if statement.  */
  if (maskexpr && maskss == NULL)
    {
      tree else_stmt;

      gfc_init_se (&maskse, NULL);
      gfc_conv_expr_val (&maskse, maskexpr);
      gfc_init_block (&block);
      gfc_add_block_to_block (&block, &loop.pre);
      gfc_add_block_to_block (&block, &loop.post);
      tmp = gfc_finish_block (&block);

      if (HONOR_INFINITIES (DECL_MODE (limit)))
	else_stmt = build2_v (MODIFY_EXPR, limit, huge_cst);
      else
	else_stmt = build_empty_stmt (input_location);
      tmp = build3_v (COND_EXPR, maskse.expr, tmp, else_stmt);
      gfc_add_expr_to_block (&block, tmp);
      gfc_add_block_to_block (&se->pre, &block);
    }
  else
    {
      gfc_add_block_to_block (&se->pre, &loop.pre);
      gfc_add_block_to_block (&se->pre, &loop.post);
    }

  gfc_cleanup_loop (&loop);

  se->expr = limit;
}

/* BTEST (i, pos) = (i & (1 << pos)) != 0.  */
static void
gfc_conv_intrinsic_btest (gfc_se * se, gfc_expr * expr)
{
  tree args[2];
  tree type;
  tree tmp;

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  type = TREE_TYPE (args[0]);

  tmp = fold_build2_loc (input_location, LSHIFT_EXPR, type,
			 build_int_cst (type, 1), args[1]);
  tmp = fold_build2_loc (input_location, BIT_AND_EXPR, type, args[0], tmp);
  tmp = fold_build2_loc (input_location, NE_EXPR, boolean_type_node, tmp,
			 build_int_cst (type, 0));
  type = gfc_typenode_for_spec (&expr->ts);
  se->expr = convert (type, tmp);
}


/* Generate code for BGE, BGT, BLE and BLT intrinsics.  */
static void
gfc_conv_intrinsic_bitcomp (gfc_se * se, gfc_expr * expr, enum tree_code op)
{
  tree args[2];

  gfc_conv_intrinsic_function_args (se, expr, args, 2);

  /* Convert both arguments to the unsigned type of the same size.  */
  args[0] = fold_convert (unsigned_type_for (TREE_TYPE (args[0])), args[0]);
  args[1] = fold_convert (unsigned_type_for (TREE_TYPE (args[1])), args[1]);

  /* If they have unequal type size, convert to the larger one.  */
  if (TYPE_PRECISION (TREE_TYPE (args[0]))
      > TYPE_PRECISION (TREE_TYPE (args[1])))
    args[1] = fold_convert (TREE_TYPE (args[0]), args[1]);
  else if (TYPE_PRECISION (TREE_TYPE (args[1]))
	   > TYPE_PRECISION (TREE_TYPE (args[0])))
    args[0] = fold_convert (TREE_TYPE (args[1]), args[0]);

  /* Now, we compare them.  */
  se->expr = fold_build2_loc (input_location, op, boolean_type_node,
			      args[0], args[1]);
}


/* Generate code to perform the specified operation.  */
static void
gfc_conv_intrinsic_bitop (gfc_se * se, gfc_expr * expr, enum tree_code op)
{
  tree args[2];

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  se->expr = fold_build2_loc (input_location, op, TREE_TYPE (args[0]),
			      args[0], args[1]);
}

/* Bitwise not.  */
static void
gfc_conv_intrinsic_not (gfc_se * se, gfc_expr * expr)
{
  tree arg;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  se->expr = fold_build1_loc (input_location, BIT_NOT_EXPR,
			      TREE_TYPE (arg), arg);
}

/* Set or clear a single bit.  */
static void
gfc_conv_intrinsic_singlebitop (gfc_se * se, gfc_expr * expr, int set)
{
  tree args[2];
  tree type;
  tree tmp;
  enum tree_code op;

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  type = TREE_TYPE (args[0]);

  tmp = fold_build2_loc (input_location, LSHIFT_EXPR, type,
			 build_int_cst (type, 1), args[1]);
  if (set)
    op = BIT_IOR_EXPR;
  else
    {
      op = BIT_AND_EXPR;
      tmp = fold_build1_loc (input_location, BIT_NOT_EXPR, type, tmp);
    }
  se->expr = fold_build2_loc (input_location, op, type, args[0], tmp);
}

/* Extract a sequence of bits.
    IBITS(I, POS, LEN) = (I >> POS) & ~((~0) << LEN).  */
static void
gfc_conv_intrinsic_ibits (gfc_se * se, gfc_expr * expr)
{
  tree args[3];
  tree type;
  tree tmp;
  tree mask;

  gfc_conv_intrinsic_function_args (se, expr, args, 3);
  type = TREE_TYPE (args[0]);

  mask = build_int_cst (type, -1);
  mask = fold_build2_loc (input_location, LSHIFT_EXPR, type, mask, args[2]);
  mask = fold_build1_loc (input_location, BIT_NOT_EXPR, type, mask);

  tmp = fold_build2_loc (input_location, RSHIFT_EXPR, type, args[0], args[1]);

  se->expr = fold_build2_loc (input_location, BIT_AND_EXPR, type, tmp, mask);
}

static void
gfc_conv_intrinsic_shift (gfc_se * se, gfc_expr * expr, bool right_shift,
			  bool arithmetic)
{
  tree args[2], type, num_bits, cond;

  gfc_conv_intrinsic_function_args (se, expr, args, 2);

  args[0] = gfc_evaluate_now (args[0], &se->pre);
  args[1] = gfc_evaluate_now (args[1], &se->pre);
  type = TREE_TYPE (args[0]);

  if (!arithmetic)
    args[0] = fold_convert (unsigned_type_for (type), args[0]);
  else
    gcc_assert (right_shift);

  se->expr = fold_build2_loc (input_location,
			      right_shift ? RSHIFT_EXPR : LSHIFT_EXPR,
			      TREE_TYPE (args[0]), args[0], args[1]);

  if (!arithmetic)
    se->expr = fold_convert (type, se->expr);

  /* The Fortran standard allows shift widths <= BIT_SIZE(I), whereas
     gcc requires a shift width < BIT_SIZE(I), so we have to catch this
     special case.  */
  num_bits = build_int_cst (TREE_TYPE (args[1]), TYPE_PRECISION (type));
  cond = fold_build2_loc (input_location, GE_EXPR, boolean_type_node,
			  args[1], num_bits);

  se->expr = fold_build3_loc (input_location, COND_EXPR, type, cond,
			      build_int_cst (type, 0), se->expr);
}

/* ISHFT (I, SHIFT) = (abs (shift) >= BIT_SIZE (i))
                        ? 0
	 	        : ((shift >= 0) ? i << shift : i >> -shift)
   where all shifts are logical shifts.  */
static void
gfc_conv_intrinsic_ishft (gfc_se * se, gfc_expr * expr)
{
  tree args[2];
  tree type;
  tree utype;
  tree tmp;
  tree width;
  tree num_bits;
  tree cond;
  tree lshift;
  tree rshift;

  gfc_conv_intrinsic_function_args (se, expr, args, 2);

  args[0] = gfc_evaluate_now (args[0], &se->pre);
  args[1] = gfc_evaluate_now (args[1], &se->pre);

  type = TREE_TYPE (args[0]);
  utype = unsigned_type_for (type);

  width = fold_build1_loc (input_location, ABS_EXPR, TREE_TYPE (args[1]),
			   args[1]);

  /* Left shift if positive.  */
  lshift = fold_build2_loc (input_location, LSHIFT_EXPR, type, args[0], width);

  /* Right shift if negative.
     We convert to an unsigned type because we want a logical shift.
     The standard doesn't define the case of shifting negative
     numbers, and we try to be compatible with other compilers, most
     notably g77, here.  */
  rshift = fold_convert (type, fold_build2_loc (input_location, RSHIFT_EXPR,
				    utype, convert (utype, args[0]), width));

  tmp = fold_build2_loc (input_location, GE_EXPR, boolean_type_node, args[1],
			 build_int_cst (TREE_TYPE (args[1]), 0));
  tmp = fold_build3_loc (input_location, COND_EXPR, type, tmp, lshift, rshift);

  /* The Fortran standard allows shift widths <= BIT_SIZE(I), whereas
     gcc requires a shift width < BIT_SIZE(I), so we have to catch this
     special case.  */
  num_bits = build_int_cst (TREE_TYPE (args[1]), TYPE_PRECISION (type));
  cond = fold_build2_loc (input_location, GE_EXPR, boolean_type_node, width,
			  num_bits);
  se->expr = fold_build3_loc (input_location, COND_EXPR, type, cond,
			      build_int_cst (type, 0), tmp);
}


/* Circular shift.  AKA rotate or barrel shift.  */

static void
gfc_conv_intrinsic_ishftc (gfc_se * se, gfc_expr * expr)
{
  tree *args;
  tree type;
  tree tmp;
  tree lrot;
  tree rrot;
  tree zero;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, num_args);

  gfc_conv_intrinsic_function_args (se, expr, args, num_args);

  if (num_args == 3)
    {
      /* Use a library function for the 3 parameter version.  */
      tree int4type = gfc_get_int_type (4);

      type = TREE_TYPE (args[0]);
      /* We convert the first argument to at least 4 bytes, and
	 convert back afterwards.  This removes the need for library
	 functions for all argument sizes, and function will be
	 aligned to at least 32 bits, so there's no loss.  */
      if (expr->ts.kind < 4)
	args[0] = convert (int4type, args[0]);

      /* Convert the SHIFT and SIZE args to INTEGER*4 otherwise we would
         need loads of library  functions.  They cannot have values >
	 BIT_SIZE (I) so the conversion is safe.  */
      args[1] = convert (int4type, args[1]);
      args[2] = convert (int4type, args[2]);

      switch (expr->ts.kind)
	{
	case 1:
	case 2:
	case 4:
	  tmp = gfor_fndecl_math_ishftc4;
	  break;
	case 8:
	  tmp = gfor_fndecl_math_ishftc8;
	  break;
	case 16:
	  tmp = gfor_fndecl_math_ishftc16;
	  break;
	default:
	  gcc_unreachable ();
	}
      se->expr = build_call_expr_loc (input_location,
				      tmp, 3, args[0], args[1], args[2]);
      /* Convert the result back to the original type, if we extended
	 the first argument's width above.  */
      if (expr->ts.kind < 4)
	se->expr = convert (type, se->expr);

      return;
    }
  type = TREE_TYPE (args[0]);

  /* Evaluate arguments only once.  */
  args[0] = gfc_evaluate_now (args[0], &se->pre);
  args[1] = gfc_evaluate_now (args[1], &se->pre);

  /* Rotate left if positive.  */
  lrot = fold_build2_loc (input_location, LROTATE_EXPR, type, args[0], args[1]);

  /* Rotate right if negative.  */
  tmp = fold_build1_loc (input_location, NEGATE_EXPR, TREE_TYPE (args[1]),
			 args[1]);
  rrot = fold_build2_loc (input_location,RROTATE_EXPR, type, args[0], tmp);

  zero = build_int_cst (TREE_TYPE (args[1]), 0);
  tmp = fold_build2_loc (input_location, GT_EXPR, boolean_type_node, args[1],
			 zero);
  rrot = fold_build3_loc (input_location, COND_EXPR, type, tmp, lrot, rrot);

  /* Do nothing if shift == 0.  */
  tmp = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, args[1],
			 zero);
  se->expr = fold_build3_loc (input_location, COND_EXPR, type, tmp, args[0],
			      rrot);
}


/* LEADZ (i) = (i == 0) ? BIT_SIZE (i)
			: __builtin_clz(i) - (BIT_SIZE('int') - BIT_SIZE(i))

   The conditional expression is necessary because the result of LEADZ(0)
   is defined, but the result of __builtin_clz(0) is undefined for most
   targets.

   For INTEGER kinds smaller than the C 'int' type, we have to subtract the
   difference in bit size between the argument of LEADZ and the C int.  */
 
static void
gfc_conv_intrinsic_leadz (gfc_se * se, gfc_expr * expr)
{
  tree arg;
  tree arg_type;
  tree cond;
  tree result_type;
  tree leadz;
  tree bit_size;
  tree tmp;
  tree func;
  int s, argsize;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  argsize = TYPE_PRECISION (TREE_TYPE (arg));

  /* Which variant of __builtin_clz* should we call?  */
  if (argsize <= INT_TYPE_SIZE)
    {
      arg_type = unsigned_type_node;
      func = builtin_decl_explicit (BUILT_IN_CLZ);
    }
  else if (argsize <= LONG_TYPE_SIZE)
    {
      arg_type = long_unsigned_type_node;
      func = builtin_decl_explicit (BUILT_IN_CLZL);
    }
  else if (argsize <= LONG_LONG_TYPE_SIZE)
    {
      arg_type = long_long_unsigned_type_node;
      func = builtin_decl_explicit (BUILT_IN_CLZLL);
    }
  else
    {
      gcc_assert (argsize == 2 * LONG_LONG_TYPE_SIZE);
      arg_type = gfc_build_uint_type (argsize);
      func = NULL_TREE;
    }

  /* Convert the actual argument twice: first, to the unsigned type of the
     same size; then, to the proper argument type for the built-in
     function.  But the return type is of the default INTEGER kind.  */
  arg = fold_convert (gfc_build_uint_type (argsize), arg);
  arg = fold_convert (arg_type, arg);
  arg = gfc_evaluate_now (arg, &se->pre);
  result_type = gfc_get_int_type (gfc_default_integer_kind);

  /* Compute LEADZ for the case i .ne. 0.  */
  if (func)
    {
      s = TYPE_PRECISION (arg_type) - argsize;
      tmp = fold_convert (result_type,
			  build_call_expr_loc (input_location, func,
					       1, arg));
      leadz = fold_build2_loc (input_location, MINUS_EXPR, result_type,
			       tmp, build_int_cst (result_type, s));
    }
  else
    {
      /* We end up here if the argument type is larger than 'long long'.
	 We generate this code:
  
	    if (x & (ULL_MAX << ULL_SIZE) != 0)
	      return clzll ((unsigned long long) (x >> ULLSIZE));
	    else
	      return ULL_SIZE + clzll ((unsigned long long) x);
	 where ULL_MAX is the largest value that a ULL_MAX can hold
	 (0xFFFFFFFFFFFFFFFF for a 64-bit long long type), and ULLSIZE
	 is the bit-size of the long long type (64 in this example).  */
      tree ullsize, ullmax, tmp1, tmp2, btmp;

      ullsize = build_int_cst (result_type, LONG_LONG_TYPE_SIZE);
      ullmax = fold_build1_loc (input_location, BIT_NOT_EXPR,
				long_long_unsigned_type_node,
				build_int_cst (long_long_unsigned_type_node,
					       0));

      cond = fold_build2_loc (input_location, LSHIFT_EXPR, arg_type,
			      fold_convert (arg_type, ullmax), ullsize);
      cond = fold_build2_loc (input_location, BIT_AND_EXPR, arg_type,
			      arg, cond);
      cond = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
			      cond, build_int_cst (arg_type, 0));

      tmp1 = fold_build2_loc (input_location, RSHIFT_EXPR, arg_type,
			      arg, ullsize);
      tmp1 = fold_convert (long_long_unsigned_type_node, tmp1);
      btmp = builtin_decl_explicit (BUILT_IN_CLZLL);
      tmp1 = fold_convert (result_type,
			   build_call_expr_loc (input_location, btmp, 1, tmp1));

      tmp2 = fold_convert (long_long_unsigned_type_node, arg);
      btmp = builtin_decl_explicit (BUILT_IN_CLZLL);
      tmp2 = fold_convert (result_type,
			   build_call_expr_loc (input_location, btmp, 1, tmp2));
      tmp2 = fold_build2_loc (input_location, PLUS_EXPR, result_type,
			      tmp2, ullsize);

      leadz = fold_build3_loc (input_location, COND_EXPR, result_type,
			       cond, tmp1, tmp2);
    }

  /* Build BIT_SIZE.  */
  bit_size = build_int_cst (result_type, argsize);

  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
			  arg, build_int_cst (arg_type, 0));
  se->expr = fold_build3_loc (input_location, COND_EXPR, result_type, cond,
			      bit_size, leadz);
}


/* TRAILZ(i) = (i == 0) ? BIT_SIZE (i) : __builtin_ctz(i)

   The conditional expression is necessary because the result of TRAILZ(0)
   is defined, but the result of __builtin_ctz(0) is undefined for most
   targets.  */
 
static void
gfc_conv_intrinsic_trailz (gfc_se * se, gfc_expr *expr)
{
  tree arg;
  tree arg_type;
  tree cond;
  tree result_type;
  tree trailz;
  tree bit_size;
  tree func;
  int argsize;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  argsize = TYPE_PRECISION (TREE_TYPE (arg));

  /* Which variant of __builtin_ctz* should we call?  */
  if (argsize <= INT_TYPE_SIZE)
    {
      arg_type = unsigned_type_node;
      func = builtin_decl_explicit (BUILT_IN_CTZ);
    }
  else if (argsize <= LONG_TYPE_SIZE)
    {
      arg_type = long_unsigned_type_node;
      func = builtin_decl_explicit (BUILT_IN_CTZL);
    }
  else if (argsize <= LONG_LONG_TYPE_SIZE)
    {
      arg_type = long_long_unsigned_type_node;
      func = builtin_decl_explicit (BUILT_IN_CTZLL);
    }
  else
    {
      gcc_assert (argsize == 2 * LONG_LONG_TYPE_SIZE);
      arg_type = gfc_build_uint_type (argsize);
      func = NULL_TREE;
    }

  /* Convert the actual argument twice: first, to the unsigned type of the
     same size; then, to the proper argument type for the built-in
     function.  But the return type is of the default INTEGER kind.  */
  arg = fold_convert (gfc_build_uint_type (argsize), arg);
  arg = fold_convert (arg_type, arg);
  arg = gfc_evaluate_now (arg, &se->pre);
  result_type = gfc_get_int_type (gfc_default_integer_kind);

  /* Compute TRAILZ for the case i .ne. 0.  */
  if (func)
    trailz = fold_convert (result_type, build_call_expr_loc (input_location,
							     func, 1, arg));
  else
    {
      /* We end up here if the argument type is larger than 'long long'.
	 We generate this code:
  
	    if ((x & ULL_MAX) == 0)
	      return ULL_SIZE + ctzll ((unsigned long long) (x >> ULLSIZE));
	    else
	      return ctzll ((unsigned long long) x);

	 where ULL_MAX is the largest value that a ULL_MAX can hold
	 (0xFFFFFFFFFFFFFFFF for a 64-bit long long type), and ULLSIZE
	 is the bit-size of the long long type (64 in this example).  */
      tree ullsize, ullmax, tmp1, tmp2, btmp;

      ullsize = build_int_cst (result_type, LONG_LONG_TYPE_SIZE);
      ullmax = fold_build1_loc (input_location, BIT_NOT_EXPR,
				long_long_unsigned_type_node,
				build_int_cst (long_long_unsigned_type_node, 0));

      cond = fold_build2_loc (input_location, BIT_AND_EXPR, arg_type, arg,
			      fold_convert (arg_type, ullmax));
      cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, cond,
			      build_int_cst (arg_type, 0));

      tmp1 = fold_build2_loc (input_location, RSHIFT_EXPR, arg_type,
			      arg, ullsize);
      tmp1 = fold_convert (long_long_unsigned_type_node, tmp1);
      btmp = builtin_decl_explicit (BUILT_IN_CTZLL);
      tmp1 = fold_convert (result_type,
			   build_call_expr_loc (input_location, btmp, 1, tmp1));
      tmp1 = fold_build2_loc (input_location, PLUS_EXPR, result_type,
			      tmp1, ullsize);

      tmp2 = fold_convert (long_long_unsigned_type_node, arg);
      btmp = builtin_decl_explicit (BUILT_IN_CTZLL);
      tmp2 = fold_convert (result_type,
			   build_call_expr_loc (input_location, btmp, 1, tmp2));

      trailz = fold_build3_loc (input_location, COND_EXPR, result_type,
				cond, tmp1, tmp2);
    }

  /* Build BIT_SIZE.  */
  bit_size = build_int_cst (result_type, argsize);

  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
			  arg, build_int_cst (arg_type, 0));
  se->expr = fold_build3_loc (input_location, COND_EXPR, result_type, cond,
			      bit_size, trailz);
}

/* Using __builtin_popcount for POPCNT and __builtin_parity for POPPAR;
   for types larger than "long long", we call the long long built-in for
   the lower and higher bits and combine the result.  */
 
static void
gfc_conv_intrinsic_popcnt_poppar (gfc_se * se, gfc_expr *expr, int parity)
{
  tree arg;
  tree arg_type;
  tree result_type;
  tree func;
  int argsize;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  argsize = TYPE_PRECISION (TREE_TYPE (arg));
  result_type = gfc_get_int_type (gfc_default_integer_kind);

  /* Which variant of the builtin should we call?  */
  if (argsize <= INT_TYPE_SIZE)
    {
      arg_type = unsigned_type_node;
      func = builtin_decl_explicit (parity
				    ? BUILT_IN_PARITY
				    : BUILT_IN_POPCOUNT);
    }
  else if (argsize <= LONG_TYPE_SIZE)
    {
      arg_type = long_unsigned_type_node;
      func = builtin_decl_explicit (parity
				    ? BUILT_IN_PARITYL
				    : BUILT_IN_POPCOUNTL);
    }
  else if (argsize <= LONG_LONG_TYPE_SIZE)
    {
      arg_type = long_long_unsigned_type_node;
      func = builtin_decl_explicit (parity
				    ? BUILT_IN_PARITYLL
				    : BUILT_IN_POPCOUNTLL);
    }
  else
    {
      /* Our argument type is larger than 'long long', which mean none
	 of the POPCOUNT builtins covers it.  We thus call the 'long long'
	 variant multiple times, and add the results.  */
      tree utype, arg2, call1, call2;

      /* For now, we only cover the case where argsize is twice as large
	 as 'long long'.  */
      gcc_assert (argsize == 2 * LONG_LONG_TYPE_SIZE);

      func = builtin_decl_explicit (parity
				    ? BUILT_IN_PARITYLL
				    : BUILT_IN_POPCOUNTLL);

      /* Convert it to an integer, and store into a variable.  */
      utype = gfc_build_uint_type (argsize);
      arg = fold_convert (utype, arg);
      arg = gfc_evaluate_now (arg, &se->pre);

      /* Call the builtin twice.  */
      call1 = build_call_expr_loc (input_location, func, 1,
				   fold_convert (long_long_unsigned_type_node,
						 arg));

      arg2 = fold_build2_loc (input_location, RSHIFT_EXPR, utype, arg,
			      build_int_cst (utype, LONG_LONG_TYPE_SIZE));
      call2 = build_call_expr_loc (input_location, func, 1,
				   fold_convert (long_long_unsigned_type_node,
						 arg2));
			  
      /* Combine the results.  */
      if (parity)
	se->expr = fold_build2_loc (input_location, BIT_XOR_EXPR, result_type,
				    call1, call2);
      else
	se->expr = fold_build2_loc (input_location, PLUS_EXPR, result_type,
				    call1, call2);

      return;
    }

  /* Convert the actual argument twice: first, to the unsigned type of the
     same size; then, to the proper argument type for the built-in
     function.  */
  arg = fold_convert (gfc_build_uint_type (argsize), arg);
  arg = fold_convert (arg_type, arg);

  se->expr = fold_convert (result_type,
			   build_call_expr_loc (input_location, func, 1, arg));
}


/* Process an intrinsic with unspecified argument-types that has an optional
   argument (which could be of type character), e.g. EOSHIFT.  For those, we
   need to append the string length of the optional argument if it is not
   present and the type is really character.
   primary specifies the position (starting at 1) of the non-optional argument
   specifying the type and optional gives the position of the optional
   argument in the arglist.  */

static void
conv_generic_with_optional_char_arg (gfc_se* se, gfc_expr* expr,
				     unsigned primary, unsigned optional)
{
  gfc_actual_arglist* prim_arg;
  gfc_actual_arglist* opt_arg;
  unsigned cur_pos;
  gfc_actual_arglist* arg;
  gfc_symbol* sym;
  vec<tree, va_gc> *append_args;

  /* Find the two arguments given as position.  */
  cur_pos = 0;
  prim_arg = NULL;
  opt_arg = NULL;
  for (arg = expr->value.function.actual; arg; arg = arg->next)
    {
      ++cur_pos;

      if (cur_pos == primary)
	prim_arg = arg;
      if (cur_pos == optional)
	opt_arg = arg;

      if (cur_pos >= primary && cur_pos >= optional)
	break;
    }
  gcc_assert (prim_arg);
  gcc_assert (prim_arg->expr);
  gcc_assert (opt_arg);

  /* If we do have type CHARACTER and the optional argument is really absent,
     append a dummy 0 as string length.  */
  append_args = NULL;
  if (prim_arg->expr->ts.type == BT_CHARACTER && !opt_arg->expr)
    {
      tree dummy;

      dummy = build_int_cst (gfc_charlen_type_node, 0);
      vec_alloc (append_args, 1);
      append_args->quick_push (dummy);
    }

  /* Build the call itself.  */
  sym = gfc_get_symbol_for_expr (expr);
  gfc_conv_procedure_call (se, sym, expr->value.function.actual, expr,
			  append_args);
  gfc_free_symbol (sym);
}


/* The length of a character string.  */
static void
gfc_conv_intrinsic_len (gfc_se * se, gfc_expr * expr)
{
  tree len;
  tree type;
  tree decl;
  gfc_symbol *sym;
  gfc_se argse;
  gfc_expr *arg;

  gcc_assert (!se->ss);

  arg = expr->value.function.actual->expr;

  type = gfc_typenode_for_spec (&expr->ts);
  switch (arg->expr_type)
    {
    case EXPR_CONSTANT:
      len = build_int_cst (gfc_charlen_type_node, arg->value.character.length);
      break;

    case EXPR_ARRAY:
      /* Obtain the string length from the function used by
         trans-array.c(gfc_trans_array_constructor).  */
      len = NULL_TREE;
      get_array_ctor_strlen (&se->pre, arg->value.constructor, &len);
      break;

    case EXPR_VARIABLE:
      if (arg->ref == NULL
	    || (arg->ref->next == NULL && arg->ref->type == REF_ARRAY))
	{
	  /* This doesn't catch all cases.
	     See http://gcc.gnu.org/ml/fortran/2004-06/msg00165.html
	     and the surrounding thread.  */
	  sym = arg->symtree->n.sym;
	  decl = gfc_get_symbol_decl (sym);
	  if (decl == current_function_decl && sym->attr.function
		&& (sym->result == sym))
	    decl = gfc_get_fake_result_decl (sym, 0);

	  len = sym->ts.u.cl->backend_decl;
	  gcc_assert (len);
	  break;
	}

      /* Otherwise fall through.  */

    default:
      /* Anybody stupid enough to do this deserves inefficient code.  */
      gfc_init_se (&argse, se);
      if (arg->rank == 0)
	gfc_conv_expr (&argse, arg);
      else
	gfc_conv_expr_descriptor (&argse, arg);
      gfc_add_block_to_block (&se->pre, &argse.pre);
      gfc_add_block_to_block (&se->post, &argse.post);
      len = argse.string_length;
      break;
    }
  se->expr = convert (type, len);
}

/* The length of a character string not including trailing blanks.  */
static void
gfc_conv_intrinsic_len_trim (gfc_se * se, gfc_expr * expr)
{
  int kind = expr->value.function.actual->expr->ts.kind;
  tree args[2], type, fndecl;

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  type = gfc_typenode_for_spec (&expr->ts);

  if (kind == 1)
    fndecl = gfor_fndecl_string_len_trim;
  else if (kind == 4)
    fndecl = gfor_fndecl_string_len_trim_char4;
  else
    gcc_unreachable ();

  se->expr = build_call_expr_loc (input_location,
			      fndecl, 2, args[0], args[1]);
  se->expr = convert (type, se->expr);
}


/* Returns the starting position of a substring within a string.  */

static void
gfc_conv_intrinsic_index_scan_verify (gfc_se * se, gfc_expr * expr,
				      tree function)
{
  tree logical4_type_node = gfc_get_logical_type (4);
  tree type;
  tree fndecl;
  tree *args;
  unsigned int num_args;

  args = XALLOCAVEC (tree, 5);

  /* Get number of arguments; characters count double due to the
     string length argument. Kind= is not passed to the library
     and thus ignored.  */
  if (expr->value.function.actual->next->next->expr == NULL)
    num_args = 4;
  else
    num_args = 5;

  gfc_conv_intrinsic_function_args (se, expr, args, num_args);
  type = gfc_typenode_for_spec (&expr->ts);

  if (num_args == 4)
    args[4] = build_int_cst (logical4_type_node, 0);
  else
    args[4] = convert (logical4_type_node, args[4]);

  fndecl = build_addr (function, current_function_decl);
  se->expr = build_call_array_loc (input_location,
			       TREE_TYPE (TREE_TYPE (function)), fndecl,
			       5, args);
  se->expr = convert (type, se->expr);

}

/* The ascii value for a single character.  */
static void
gfc_conv_intrinsic_ichar (gfc_se * se, gfc_expr * expr)
{
  tree args[2], type, pchartype;

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  gcc_assert (POINTER_TYPE_P (TREE_TYPE (args[1])));
  pchartype = gfc_get_pchar_type (expr->value.function.actual->expr->ts.kind);
  args[1] = fold_build1_loc (input_location, NOP_EXPR, pchartype, args[1]);
  type = gfc_typenode_for_spec (&expr->ts);

  se->expr = build_fold_indirect_ref_loc (input_location,
				      args[1]);
  se->expr = convert (type, se->expr);
}


/* Intrinsic ISNAN calls __builtin_isnan.  */

static void
gfc_conv_intrinsic_isnan (gfc_se * se, gfc_expr * expr)
{
  tree arg;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  se->expr = build_call_expr_loc (input_location,
				  builtin_decl_explicit (BUILT_IN_ISNAN),
				  1, arg);
  STRIP_TYPE_NOPS (se->expr);
  se->expr = fold_convert (gfc_typenode_for_spec (&expr->ts), se->expr);
}


/* Intrinsics IS_IOSTAT_END and IS_IOSTAT_EOR just need to compare
   their argument against a constant integer value.  */

static void
gfc_conv_has_intvalue (gfc_se * se, gfc_expr * expr, const int value)
{
  tree arg;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  se->expr = fold_build2_loc (input_location, EQ_EXPR,
			      gfc_typenode_for_spec (&expr->ts),
			      arg, build_int_cst (TREE_TYPE (arg), value));
}



/* MERGE (tsource, fsource, mask) = mask ? tsource : fsource.  */

static void
gfc_conv_intrinsic_merge (gfc_se * se, gfc_expr * expr)
{
  tree tsource;
  tree fsource;
  tree mask;
  tree type;
  tree len, len2;
  tree *args;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr);
  args = XALLOCAVEC (tree, num_args);

  gfc_conv_intrinsic_function_args (se, expr, args, num_args);
  if (expr->ts.type != BT_CHARACTER)
    {
      tsource = args[0];
      fsource = args[1];
      mask = args[2];
    }
  else
    {
      /* We do the same as in the non-character case, but the argument
	 list is different because of the string length arguments. We
	 also have to set the string length for the result.  */
      len = args[0];
      tsource = args[1];
      len2 = args[2];
      fsource = args[3];
      mask = args[4];

      gfc_trans_same_strlen_check ("MERGE intrinsic", &expr->where, len, len2,
				   &se->pre);
      se->string_length = len;
    }
  type = TREE_TYPE (tsource);
  se->expr = fold_build3_loc (input_location, COND_EXPR, type, mask, tsource,
			      fold_convert (type, fsource));
}


/* MERGE_BITS (I, J, MASK) = (I & MASK) | (I & (~MASK)).  */

static void
gfc_conv_intrinsic_merge_bits (gfc_se * se, gfc_expr * expr)
{
  tree args[3], mask, type;

  gfc_conv_intrinsic_function_args (se, expr, args, 3);
  mask = gfc_evaluate_now (args[2], &se->pre);

  type = TREE_TYPE (args[0]);
  gcc_assert (TREE_TYPE (args[1]) == type);
  gcc_assert (TREE_TYPE (mask) == type);

  args[0] = fold_build2_loc (input_location, BIT_AND_EXPR, type, args[0], mask);
  args[1] = fold_build2_loc (input_location, BIT_AND_EXPR, type, args[1],
			     fold_build1_loc (input_location, BIT_NOT_EXPR,
					      type, mask));
  se->expr = fold_build2_loc (input_location, BIT_IOR_EXPR, type,
			      args[0], args[1]);
}


/* MASKL(n)  =  n == 0 ? 0 : (~0) << (BIT_SIZE - n)
   MASKR(n)  =  n == BIT_SIZE ? ~0 : ~((~0) << n)  */

static void
gfc_conv_intrinsic_mask (gfc_se * se, gfc_expr * expr, int left)
{
  tree arg, allones, type, utype, res, cond, bitsize;
  int i;
 
  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  arg = gfc_evaluate_now (arg, &se->pre);

  type = gfc_get_int_type (expr->ts.kind);
  utype = unsigned_type_for (type);

  i = gfc_validate_kind (BT_INTEGER, expr->ts.kind, false);
  bitsize = build_int_cst (TREE_TYPE (arg), gfc_integer_kinds[i].bit_size);

  allones = fold_build1_loc (input_location, BIT_NOT_EXPR, utype,
			     build_int_cst (utype, 0));

  if (left)
    {
      /* Left-justified mask.  */
      res = fold_build2_loc (input_location, MINUS_EXPR, TREE_TYPE (arg),
			     bitsize, arg);
      res = fold_build2_loc (input_location, LSHIFT_EXPR, utype, allones,
			     fold_convert (utype, res));

      /* Special case arg == 0, because SHIFT_EXPR wants a shift strictly
	 smaller than type width.  */
      cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, arg,
			      build_int_cst (TREE_TYPE (arg), 0));
      res = fold_build3_loc (input_location, COND_EXPR, utype, cond,
			     build_int_cst (utype, 0), res);
    }
  else
    {
      /* Right-justified mask.  */
      res = fold_build2_loc (input_location, LSHIFT_EXPR, utype, allones,
			     fold_convert (utype, arg));
      res = fold_build1_loc (input_location, BIT_NOT_EXPR, utype, res);

      /* Special case agr == bit_size, because SHIFT_EXPR wants a shift
	 strictly smaller than type width.  */
      cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
			      arg, bitsize);
      res = fold_build3_loc (input_location, COND_EXPR, utype,
			     cond, allones, res);
    }

  se->expr = fold_convert (type, res);
}


/* FRACTION (s) is translated into frexp (s, &dummy_int).  */
static void
gfc_conv_intrinsic_fraction (gfc_se * se, gfc_expr * expr)
{
  tree arg, type, tmp, frexp;

  frexp = gfc_builtin_decl_for_float_kind (BUILT_IN_FREXP, expr->ts.kind);

  type = gfc_typenode_for_spec (&expr->ts);
  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  tmp = gfc_create_var (integer_type_node, NULL);
  se->expr = build_call_expr_loc (input_location, frexp, 2,
				  fold_convert (type, arg),
				  gfc_build_addr_expr (NULL_TREE, tmp));
  se->expr = fold_convert (type, se->expr);
}


/* NEAREST (s, dir) is translated into
     tmp = copysign (HUGE_VAL, dir);
     return nextafter (s, tmp);
 */
static void
gfc_conv_intrinsic_nearest (gfc_se * se, gfc_expr * expr)
{
  tree args[2], type, tmp, nextafter, copysign, huge_val;

  nextafter = gfc_builtin_decl_for_float_kind (BUILT_IN_NEXTAFTER, expr->ts.kind);
  copysign = gfc_builtin_decl_for_float_kind (BUILT_IN_COPYSIGN, expr->ts.kind);

  type = gfc_typenode_for_spec (&expr->ts);
  gfc_conv_intrinsic_function_args (se, expr, args, 2);

  huge_val = gfc_build_inf_or_huge (type, expr->ts.kind);
  tmp = build_call_expr_loc (input_location, copysign, 2, huge_val,
			     fold_convert (type, args[1]));
  se->expr = build_call_expr_loc (input_location, nextafter, 2,
				  fold_convert (type, args[0]), tmp);
  se->expr = fold_convert (type, se->expr);
}


/* SPACING (s) is translated into
    int e;
    if (s == 0)
      res = tiny;
    else
    {
      frexp (s, &e);
      e = e - prec;
      e = MAX_EXPR (e, emin);
      res = scalbn (1., e);
    }
    return res;

 where prec is the precision of s, gfc_real_kinds[k].digits,
       emin is min_exponent - 1, gfc_real_kinds[k].min_exponent - 1,
   and tiny is tiny(s), gfc_real_kinds[k].tiny.  */

static void
gfc_conv_intrinsic_spacing (gfc_se * se, gfc_expr * expr)
{
  tree arg, type, prec, emin, tiny, res, e;
  tree cond, tmp, frexp, scalbn;
  int k;
  stmtblock_t block;

  k = gfc_validate_kind (BT_REAL, expr->ts.kind, false);
  prec = build_int_cst (integer_type_node, gfc_real_kinds[k].digits);
  emin = build_int_cst (integer_type_node, gfc_real_kinds[k].min_exponent - 1);
  tiny = gfc_conv_mpfr_to_tree (gfc_real_kinds[k].tiny, expr->ts.kind, 0);

  frexp = gfc_builtin_decl_for_float_kind (BUILT_IN_FREXP, expr->ts.kind);
  scalbn = gfc_builtin_decl_for_float_kind (BUILT_IN_SCALBN, expr->ts.kind);

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  arg = gfc_evaluate_now (arg, &se->pre);

  type = gfc_typenode_for_spec (&expr->ts);
  e = gfc_create_var (integer_type_node, NULL);
  res = gfc_create_var (type, NULL);


  /* Build the block for s /= 0.  */
  gfc_start_block (&block);
  tmp = build_call_expr_loc (input_location, frexp, 2, arg,
			     gfc_build_addr_expr (NULL_TREE, e));
  gfc_add_expr_to_block (&block, tmp);

  tmp = fold_build2_loc (input_location, MINUS_EXPR, integer_type_node, e,
			 prec);
  gfc_add_modify (&block, e, fold_build2_loc (input_location, MAX_EXPR,
					      integer_type_node, tmp, emin));

  tmp = build_call_expr_loc (input_location, scalbn, 2,
			 build_real_from_int_cst (type, integer_one_node), e);
  gfc_add_modify (&block, res, tmp);

  /* Finish by building the IF statement.  */
  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, arg,
			  build_real_from_int_cst (type, integer_zero_node));
  tmp = build3_v (COND_EXPR, cond, build2_v (MODIFY_EXPR, res, tiny),
		  gfc_finish_block (&block));

  gfc_add_expr_to_block (&se->pre, tmp);
  se->expr = res;
}


/* RRSPACING (s) is translated into
      int e;
      real x;
      x = fabs (s);
      if (x != 0)
      {
	frexp (s, &e);
	x = scalbn (x, precision - e);
      }
      return x;

 where precision is gfc_real_kinds[k].digits.  */

static void
gfc_conv_intrinsic_rrspacing (gfc_se * se, gfc_expr * expr)
{
  tree arg, type, e, x, cond, stmt, tmp, frexp, scalbn, fabs;
  int prec, k;
  stmtblock_t block;

  k = gfc_validate_kind (BT_REAL, expr->ts.kind, false);
  prec = gfc_real_kinds[k].digits;

  frexp = gfc_builtin_decl_for_float_kind (BUILT_IN_FREXP, expr->ts.kind);
  scalbn = gfc_builtin_decl_for_float_kind (BUILT_IN_SCALBN, expr->ts.kind);
  fabs = gfc_builtin_decl_for_float_kind (BUILT_IN_FABS, expr->ts.kind);

  type = gfc_typenode_for_spec (&expr->ts);
  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);
  arg = gfc_evaluate_now (arg, &se->pre);

  e = gfc_create_var (integer_type_node, NULL);
  x = gfc_create_var (type, NULL);
  gfc_add_modify (&se->pre, x,
		  build_call_expr_loc (input_location, fabs, 1, arg));


  gfc_start_block (&block);
  tmp = build_call_expr_loc (input_location, frexp, 2, arg,
			     gfc_build_addr_expr (NULL_TREE, e));
  gfc_add_expr_to_block (&block, tmp);

  tmp = fold_build2_loc (input_location, MINUS_EXPR, integer_type_node,
			 build_int_cst (integer_type_node, prec), e);
  tmp = build_call_expr_loc (input_location, scalbn, 2, x, tmp);
  gfc_add_modify (&block, x, tmp);
  stmt = gfc_finish_block (&block);

  cond = fold_build2_loc (input_location, NE_EXPR, boolean_type_node, x,
			  build_real_from_int_cst (type, integer_zero_node));
  tmp = build3_v (COND_EXPR, cond, stmt, build_empty_stmt (input_location));
  gfc_add_expr_to_block (&se->pre, tmp);

  se->expr = fold_convert (type, x);
}


/* SCALE (s, i) is translated into scalbn (s, i).  */
static void
gfc_conv_intrinsic_scale (gfc_se * se, gfc_expr * expr)
{
  tree args[2], type, scalbn;

  scalbn = gfc_builtin_decl_for_float_kind (BUILT_IN_SCALBN, expr->ts.kind);

  type = gfc_typenode_for_spec (&expr->ts);
  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  se->expr = build_call_expr_loc (input_location, scalbn, 2,
				  fold_convert (type, args[0]),
				  fold_convert (integer_type_node, args[1]));
  se->expr = fold_convert (type, se->expr);
}


/* SET_EXPONENT (s, i) is translated into
   scalbn (frexp (s, &dummy_int), i).  */
static void
gfc_conv_intrinsic_set_exponent (gfc_se * se, gfc_expr * expr)
{
  tree args[2], type, tmp, frexp, scalbn;

  frexp = gfc_builtin_decl_for_float_kind (BUILT_IN_FREXP, expr->ts.kind);
  scalbn = gfc_builtin_decl_for_float_kind (BUILT_IN_SCALBN, expr->ts.kind);

  type = gfc_typenode_for_spec (&expr->ts);
  gfc_conv_intrinsic_function_args (se, expr, args, 2);

  tmp = gfc_create_var (integer_type_node, NULL);
  tmp = build_call_expr_loc (input_location, frexp, 2,
			     fold_convert (type, args[0]),
			     gfc_build_addr_expr (NULL_TREE, tmp));
  se->expr = build_call_expr_loc (input_location, scalbn, 2, tmp,
				  fold_convert (integer_type_node, args[1]));
  se->expr = fold_convert (type, se->expr);
}


static void
gfc_conv_intrinsic_size (gfc_se * se, gfc_expr * expr)
{
  gfc_actual_arglist *actual;
  tree arg1;
  tree type;
  tree fncall0;
  tree fncall1;
  gfc_se argse;

  gfc_init_se (&argse, NULL);
  actual = expr->value.function.actual;

  if (actual->expr->ts.type == BT_CLASS)
    gfc_add_class_array_ref (actual->expr);

  argse.want_pointer = 1;
  argse.data_not_needed = 1;
  gfc_conv_expr_descriptor (&argse, actual->expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);
  arg1 = gfc_evaluate_now (argse.expr, &se->pre);

  /* Build the call to size0.  */
  fncall0 = build_call_expr_loc (input_location,
			     gfor_fndecl_size0, 1, arg1);

  actual = actual->next;

  if (actual->expr)
    {
      gfc_init_se (&argse, NULL);
      gfc_conv_expr_type (&argse, actual->expr,
			  gfc_array_index_type);
      gfc_add_block_to_block (&se->pre, &argse.pre);

      /* Unusually, for an intrinsic, size does not exclude
	 an optional arg2, so we must test for it.  */  
      if (actual->expr->expr_type == EXPR_VARIABLE
	    && actual->expr->symtree->n.sym->attr.dummy
	    && actual->expr->symtree->n.sym->attr.optional)
	{
	  tree tmp;
	  /* Build the call to size1.  */
	  fncall1 = build_call_expr_loc (input_location,
				     gfor_fndecl_size1, 2,
				     arg1, argse.expr);

	  gfc_init_se (&argse, NULL);
	  argse.want_pointer = 1;
	  argse.data_not_needed = 1;
	  gfc_conv_expr (&argse, actual->expr);
	  gfc_add_block_to_block (&se->pre, &argse.pre);
	  tmp = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
				 argse.expr, null_pointer_node);
	  tmp = gfc_evaluate_now (tmp, &se->pre);
	  se->expr = fold_build3_loc (input_location, COND_EXPR,
				      pvoid_type_node, tmp, fncall1, fncall0);
	}
      else
	{
	  se->expr = NULL_TREE;
	  argse.expr = fold_build2_loc (input_location, MINUS_EXPR,
					gfc_array_index_type,
					argse.expr, gfc_index_one_node);
	}
    }
  else if (expr->value.function.actual->expr->rank == 1)
    {
      argse.expr = gfc_index_zero_node;
      se->expr = NULL_TREE;
    }
  else
    se->expr = fncall0;

  if (se->expr == NULL_TREE)
    {
      tree ubound, lbound;

      arg1 = build_fold_indirect_ref_loc (input_location,
				      arg1);
      ubound = gfc_conv_descriptor_ubound_get (arg1, argse.expr);
      lbound = gfc_conv_descriptor_lbound_get (arg1, argse.expr);
      se->expr = fold_build2_loc (input_location, MINUS_EXPR,
				  gfc_array_index_type, ubound, lbound);
      se->expr = fold_build2_loc (input_location, PLUS_EXPR,
				  gfc_array_index_type,
				  se->expr, gfc_index_one_node);
      se->expr = fold_build2_loc (input_location, MAX_EXPR,
				  gfc_array_index_type, se->expr,
				  gfc_index_zero_node);
    }

  type = gfc_typenode_for_spec (&expr->ts);
  se->expr = convert (type, se->expr);
}


/* Helper function to compute the size of a character variable,
   excluding the terminating null characters.  The result has
   gfc_array_index_type type.  */

static tree
size_of_string_in_bytes (int kind, tree string_length)
{
  tree bytesize;
  int i = gfc_validate_kind (BT_CHARACTER, kind, false);
 
  bytesize = build_int_cst (gfc_array_index_type,
			    gfc_character_kinds[i].bit_size / 8);

  return fold_build2_loc (input_location, MULT_EXPR, gfc_array_index_type,
			  bytesize,
			  fold_convert (gfc_array_index_type, string_length));
}


static void
gfc_conv_intrinsic_sizeof (gfc_se *se, gfc_expr *expr)
{
  gfc_expr *arg;
  gfc_se argse;
  tree source_bytes;
  tree type;
  tree tmp;
  tree lower;
  tree upper;
  int n;

  arg = expr->value.function.actual->expr;

  gfc_init_se (&argse, NULL);

  if (arg->rank == 0)
    {
      if (arg->ts.type == BT_CLASS)
	gfc_add_data_component (arg);

      gfc_conv_expr_reference (&argse, arg);

      type = TREE_TYPE (build_fold_indirect_ref_loc (input_location,
						 argse.expr));

      /* Obtain the source word length.  */
      if (arg->ts.type == BT_CHARACTER)
	se->expr = size_of_string_in_bytes (arg->ts.kind,
					    argse.string_length);
      else
	se->expr = fold_convert (gfc_array_index_type, size_in_bytes (type)); 
    }
  else
    {
      source_bytes = gfc_create_var (gfc_array_index_type, "bytes");
      argse.want_pointer = 0;
      gfc_conv_expr_descriptor (&argse, arg);
      type = gfc_get_element_type (TREE_TYPE (argse.expr));

      /* Obtain the argument's word length.  */
      if (arg->ts.type == BT_CHARACTER)
	tmp = size_of_string_in_bytes (arg->ts.kind, argse.string_length);
      else
	tmp = fold_convert (gfc_array_index_type,
			    size_in_bytes (type)); 
      gfc_add_modify (&argse.pre, source_bytes, tmp);

      /* Obtain the size of the array in bytes.  */
      for (n = 0; n < arg->rank; n++)
	{
	  tree idx;
	  idx = gfc_rank_cst[n];
	  lower = gfc_conv_descriptor_lbound_get (argse.expr, idx);
	  upper = gfc_conv_descriptor_ubound_get (argse.expr, idx);
	  tmp = fold_build2_loc (input_location, MINUS_EXPR,
				 gfc_array_index_type, upper, lower);
	  tmp = fold_build2_loc (input_location, PLUS_EXPR,
				 gfc_array_index_type, tmp, gfc_index_one_node);
	  tmp = fold_build2_loc (input_location, MULT_EXPR,
				 gfc_array_index_type, tmp, source_bytes);
	  gfc_add_modify (&argse.pre, source_bytes, tmp);
	}
      se->expr = source_bytes;
    }

  gfc_add_block_to_block (&se->pre, &argse.pre);
}


static void
gfc_conv_intrinsic_storage_size (gfc_se *se, gfc_expr *expr)
{
  gfc_expr *arg;
  gfc_se argse,eight;
  tree type, result_type, tmp;

  arg = expr->value.function.actual->expr;
  gfc_init_se (&eight, NULL);
  gfc_conv_expr (&eight, gfc_get_int_expr (expr->ts.kind, NULL, 8));
  
  gfc_init_se (&argse, NULL);
  result_type = gfc_get_int_type (expr->ts.kind);

  if (arg->rank == 0)
    {
      if (arg->ts.type == BT_CLASS)
      {
	gfc_add_vptr_component (arg);
	gfc_add_size_component (arg);
	gfc_conv_expr (&argse, arg);
	tmp = fold_convert (result_type, argse.expr);
	goto done;
      }

      gfc_conv_expr_reference (&argse, arg);
      type = TREE_TYPE (build_fold_indirect_ref_loc (input_location, 
						     argse.expr));
    }
  else
    {
      argse.want_pointer = 0;
      gfc_conv_expr_descriptor (&argse, arg);
      type = gfc_get_element_type (TREE_TYPE (argse.expr));
    }
    
  /* Obtain the argument's word length.  */
  if (arg->ts.type == BT_CHARACTER)
    tmp = size_of_string_in_bytes (arg->ts.kind, argse.string_length);
  else
    tmp = fold_convert (result_type, size_in_bytes (type)); 

done:
  se->expr = fold_build2_loc (input_location, MULT_EXPR, result_type, tmp,
			      eight.expr);
  gfc_add_block_to_block (&se->pre, &argse.pre);
}


/* Intrinsic string comparison functions.  */

static void
gfc_conv_intrinsic_strcmp (gfc_se * se, gfc_expr * expr, enum tree_code op)
{
  tree args[4];

  gfc_conv_intrinsic_function_args (se, expr, args, 4);

  se->expr
    = gfc_build_compare_string (args[0], args[1], args[2], args[3],
				expr->value.function.actual->expr->ts.kind,
				op);
  se->expr = fold_build2_loc (input_location, op,
			      gfc_typenode_for_spec (&expr->ts), se->expr,
			      build_int_cst (TREE_TYPE (se->expr), 0));
}

/* Generate a call to the adjustl/adjustr library function.  */
static void
gfc_conv_intrinsic_adjust (gfc_se * se, gfc_expr * expr, tree fndecl)
{
  tree args[3];
  tree len;
  tree type;
  tree var;
  tree tmp;

  gfc_conv_intrinsic_function_args (se, expr, &args[1], 2);
  len = args[1];

  type = TREE_TYPE (args[2]);
  var = gfc_conv_string_tmp (se, type, len);
  args[0] = var;

  tmp = build_call_expr_loc (input_location,
			 fndecl, 3, args[0], args[1], args[2]);
  gfc_add_expr_to_block (&se->pre, tmp);
  se->expr = var;
  se->string_length = len;
}


/* Generate code for the TRANSFER intrinsic:
	For scalar results:
	  DEST = TRANSFER (SOURCE, MOLD)
	where:
	  typeof<DEST> = typeof<MOLD>
	and:
	  MOLD is scalar.

	For array results:
	  DEST(1:N) = TRANSFER (SOURCE, MOLD[, SIZE])
	where:
	  typeof<DEST> = typeof<MOLD>
	and:
	  N = min (sizeof (SOURCE(:)), sizeof (DEST(:)),
	      sizeof (DEST(0) * SIZE).  */
static void
gfc_conv_intrinsic_transfer (gfc_se * se, gfc_expr * expr)
{
  tree tmp;
  tree tmpdecl;
  tree ptr;
  tree extent;
  tree source;
  tree source_type;
  tree source_bytes;
  tree mold_type;
  tree dest_word_len;
  tree size_words;
  tree size_bytes;
  tree upper;
  tree lower;
  tree stmt;
  gfc_actual_arglist *arg;
  gfc_se argse;
  gfc_array_info *info;
  stmtblock_t block;
  int n;
  bool scalar_mold;
  gfc_expr *source_expr, *mold_expr;

  info = NULL;
  if (se->loop)
    info = &se->ss->info->data.array;

  /* Convert SOURCE.  The output from this stage is:-
	source_bytes = length of the source in bytes
	source = pointer to the source data.  */
  arg = expr->value.function.actual;
  source_expr = arg->expr;

  /* Ensure double transfer through LOGICAL preserves all
     the needed bits.  */
  if (arg->expr->expr_type == EXPR_FUNCTION
	&& arg->expr->value.function.esym == NULL
	&& arg->expr->value.function.isym != NULL
	&& arg->expr->value.function.isym->id == GFC_ISYM_TRANSFER
	&& arg->expr->ts.type == BT_LOGICAL
	&& expr->ts.type != arg->expr->ts.type)
    arg->expr->value.function.name = "__transfer_in_transfer";

  gfc_init_se (&argse, NULL);

  source_bytes = gfc_create_var (gfc_array_index_type, NULL);

  /* Obtain the pointer to source and the length of source in bytes.  */
  if (arg->expr->rank == 0)
    {
      gfc_conv_expr_reference (&argse, arg->expr);
      if (arg->expr->ts.type == BT_CLASS)
	source = gfc_class_data_get (argse.expr);
      else
	source = argse.expr;

      /* Obtain the source word length.  */
      switch (arg->expr->ts.type)
	{
	case BT_CHARACTER:
	  tmp = size_of_string_in_bytes (arg->expr->ts.kind,
					 argse.string_length);
	  break;
	case BT_CLASS:
	  tmp = gfc_vtable_size_get (argse.expr);
	  break;
	default:
	  source_type = TREE_TYPE (build_fold_indirect_ref_loc (input_location,
								source));
	  tmp = fold_convert (gfc_array_index_type,
			      size_in_bytes (source_type));
	  break;
	}
    }
  else
    {
      argse.want_pointer = 0;
      gfc_conv_expr_descriptor (&argse, arg->expr);
      source = gfc_conv_descriptor_data_get (argse.expr);
      source_type = gfc_get_element_type (TREE_TYPE (argse.expr));

      /* Repack the source if not simply contiguous.  */
      if (!gfc_is_simply_contiguous (arg->expr, false))
	{
	  tmp = gfc_build_addr_expr (NULL_TREE, argse.expr);

	  if (gfc_option.warn_array_temp)
	    gfc_warning ("Creating array temporary at %L", &expr->where);

	  source = build_call_expr_loc (input_location,
				    gfor_fndecl_in_pack, 1, tmp);
	  source = gfc_evaluate_now (source, &argse.pre);

	  /* Free the temporary.  */
	  gfc_start_block (&block);
	  tmp = gfc_call_free (convert (pvoid_type_node, source));
	  gfc_add_expr_to_block (&block, tmp);
	  stmt = gfc_finish_block (&block);

	  /* Clean up if it was repacked.  */
	  gfc_init_block (&block);
	  tmp = gfc_conv_array_data (argse.expr);
	  tmp = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
				 source, tmp);
	  tmp = build3_v (COND_EXPR, tmp, stmt,
			  build_empty_stmt (input_location));
	  gfc_add_expr_to_block (&block, tmp);
	  gfc_add_block_to_block (&block, &se->post);
	  gfc_init_block (&se->post);
	  gfc_add_block_to_block (&se->post, &block);
	}

      /* Obtain the source word length.  */
      if (arg->expr->ts.type == BT_CHARACTER)
	tmp = size_of_string_in_bytes (arg->expr->ts.kind,
				       argse.string_length);
      else
	tmp = fold_convert (gfc_array_index_type,
			    size_in_bytes (source_type)); 

      /* Obtain the size of the array in bytes.  */
      extent = gfc_create_var (gfc_array_index_type, NULL);
      for (n = 0; n < arg->expr->rank; n++)
	{
	  tree idx;
	  idx = gfc_rank_cst[n];
	  gfc_add_modify (&argse.pre, source_bytes, tmp);
	  lower = gfc_conv_descriptor_lbound_get (argse.expr, idx);
	  upper = gfc_conv_descriptor_ubound_get (argse.expr, idx);
	  tmp = fold_build2_loc (input_location, MINUS_EXPR,
				 gfc_array_index_type, upper, lower);
	  gfc_add_modify (&argse.pre, extent, tmp);
	  tmp = fold_build2_loc (input_location, PLUS_EXPR,
				 gfc_array_index_type, extent,
				 gfc_index_one_node);
	  tmp = fold_build2_loc (input_location, MULT_EXPR,
				 gfc_array_index_type, tmp, source_bytes);
	}
    }

  gfc_add_modify (&argse.pre, source_bytes, tmp);
  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);

  /* Now convert MOLD.  The outputs are:
	mold_type = the TREE type of MOLD
	dest_word_len = destination word length in bytes.  */
  arg = arg->next;
  mold_expr = arg->expr;

  gfc_init_se (&argse, NULL);

  scalar_mold = arg->expr->rank == 0;

  if (arg->expr->rank == 0)
    {
      gfc_conv_expr_reference (&argse, arg->expr);
      mold_type = TREE_TYPE (build_fold_indirect_ref_loc (input_location,
							  argse.expr));
    }
  else
    {
      gfc_init_se (&argse, NULL);
      argse.want_pointer = 0;
      gfc_conv_expr_descriptor (&argse, arg->expr);
      mold_type = gfc_get_element_type (TREE_TYPE (argse.expr));
    }

  gfc_add_block_to_block (&se->pre, &argse.pre);
  gfc_add_block_to_block (&se->post, &argse.post);

  if (strcmp (expr->value.function.name, "__transfer_in_transfer") == 0)
    {
      /* If this TRANSFER is nested in another TRANSFER, use a type
	 that preserves all bits.  */
      if (arg->expr->ts.type == BT_LOGICAL)
	mold_type = gfc_get_int_type (arg->expr->ts.kind);
    }

  /* Obtain the destination word length.  */
  switch (arg->expr->ts.type)
    {
    case BT_CHARACTER:
      tmp = size_of_string_in_bytes (arg->expr->ts.kind, argse.string_length);
      mold_type = gfc_get_character_type_len (arg->expr->ts.kind, tmp);
      break;
    case BT_CLASS:
      tmp = gfc_vtable_size_get (argse.expr);
      break;
    default:
      tmp = fold_convert (gfc_array_index_type, size_in_bytes (mold_type));
      break;
    }
  dest_word_len = gfc_create_var (gfc_array_index_type, NULL);
  gfc_add_modify (&se->pre, dest_word_len, tmp);

  /* Finally convert SIZE, if it is present.  */
  arg = arg->next;
  size_words = gfc_create_var (gfc_array_index_type, NULL);

  if (arg->expr)
    {
      gfc_init_se (&argse, NULL);
      gfc_conv_expr_reference (&argse, arg->expr);
      tmp = convert (gfc_array_index_type,
		     build_fold_indirect_ref_loc (input_location,
					      argse.expr));
      gfc_add_block_to_block (&se->pre, &argse.pre);
      gfc_add_block_to_block (&se->post, &argse.post);
    }
  else
    tmp = NULL_TREE;

  /* Separate array and scalar results.  */
  if (scalar_mold && tmp == NULL_TREE)
    goto scalar_transfer;

  size_bytes = gfc_create_var (gfc_array_index_type, NULL);
  if (tmp != NULL_TREE)
    tmp = fold_build2_loc (input_location, MULT_EXPR, gfc_array_index_type,
			   tmp, dest_word_len);
  else
    tmp = source_bytes;

  gfc_add_modify (&se->pre, size_bytes, tmp);
  gfc_add_modify (&se->pre, size_words,
		       fold_build2_loc (input_location, CEIL_DIV_EXPR,
					gfc_array_index_type,
					size_bytes, dest_word_len));

  /* Evaluate the bounds of the result.  If the loop range exists, we have
     to check if it is too large.  If so, we modify loop->to be consistent
     with min(size, size(source)).  Otherwise, size is made consistent with
     the loop range, so that the right number of bytes is transferred.*/
  n = se->loop->order[0];
  if (se->loop->to[n] != NULL_TREE)
    {
      tmp = fold_build2_loc (input_location, MINUS_EXPR, gfc_array_index_type,
			     se->loop->to[n], se->loop->from[n]);
      tmp = fold_build2_loc (input_location, PLUS_EXPR, gfc_array_index_type,
			     tmp, gfc_index_one_node);
      tmp = fold_build2_loc (input_location, MIN_EXPR, gfc_array_index_type,
			 tmp, size_words);
      gfc_add_modify (&se->pre, size_words, tmp);
      gfc_add_modify (&se->pre, size_bytes,
			   fold_build2_loc (input_location, MULT_EXPR,
					    gfc_array_index_type,
					    size_words, dest_word_len));
      upper = fold_build2_loc (input_location, PLUS_EXPR, gfc_array_index_type,
			       size_words, se->loop->from[n]);
      upper = fold_build2_loc (input_location, MINUS_EXPR, gfc_array_index_type,
			       upper, gfc_index_one_node);
    }
  else
    {
      upper = fold_build2_loc (input_location, MINUS_EXPR, gfc_array_index_type,
			       size_words, gfc_index_one_node);
      se->loop->from[n] = gfc_index_zero_node;
    }

  se->loop->to[n] = upper;

  /* Build a destination descriptor, using the pointer, source, as the
     data field.  */
  gfc_trans_create_temp_array (&se->pre, &se->post, se->ss, mold_type,
			       NULL_TREE, false, true, false, &expr->where);

  /* Cast the pointer to the result.  */
  tmp = gfc_conv_descriptor_data_get (info->descriptor);
  tmp = fold_convert (pvoid_type_node, tmp);

  /* Use memcpy to do the transfer.  */
  tmp
    = build_call_expr_loc (input_location,
			   builtin_decl_explicit (BUILT_IN_MEMCPY), 3, tmp,
			   fold_convert (pvoid_type_node, source),
			   fold_convert (size_type_node,
					 fold_build2_loc (input_location,
							  MIN_EXPR,
							  gfc_array_index_type,
							  size_bytes,
							  source_bytes)));
  gfc_add_expr_to_block (&se->pre, tmp);

  se->expr = info->descriptor;
  if (expr->ts.type == BT_CHARACTER)
    se->string_length = fold_convert (gfc_charlen_type_node, dest_word_len);

  return;

/* Deal with scalar results.  */
scalar_transfer:
  extent = fold_build2_loc (input_location, MIN_EXPR, gfc_array_index_type,
			    dest_word_len, source_bytes);
  extent = fold_build2_loc (input_location, MAX_EXPR, gfc_array_index_type,
			    extent, gfc_index_zero_node);

  if (expr->ts.type == BT_CHARACTER)
    {
      tree direct, indirect, free;

      ptr = convert (gfc_get_pchar_type (expr->ts.kind), source);
      tmpdecl = gfc_create_var (gfc_get_pchar_type (expr->ts.kind),
				"transfer");

      /* If source is longer than the destination, use a pointer to
	 the source directly.  */
      gfc_init_block (&block);
      gfc_add_modify (&block, tmpdecl, ptr);
      direct = gfc_finish_block (&block);

      /* Otherwise, allocate a string with the length of the destination
	 and copy the source into it.  */
      gfc_init_block (&block);
      tmp = gfc_get_pchar_type (expr->ts.kind);
      tmp = gfc_call_malloc (&block, tmp, dest_word_len);
      gfc_add_modify (&block, tmpdecl,
		      fold_convert (TREE_TYPE (ptr), tmp));
      tmp = build_call_expr_loc (input_location,
			     builtin_decl_explicit (BUILT_IN_MEMCPY), 3,
			     fold_convert (pvoid_type_node, tmpdecl),
			     fold_convert (pvoid_type_node, ptr),
			     fold_convert (size_type_node, extent));
      gfc_add_expr_to_block (&block, tmp);
      indirect = gfc_finish_block (&block);

      /* Wrap it up with the condition.  */
      tmp = fold_build2_loc (input_location, LE_EXPR, boolean_type_node,
			     dest_word_len, source_bytes);
      tmp = build3_v (COND_EXPR, tmp, direct, indirect);
      gfc_add_expr_to_block (&se->pre, tmp);

      /* Free the temporary string, if necessary.  */
      free = gfc_call_free (tmpdecl);
      tmp = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			     dest_word_len, source_bytes);
      tmp = build3_v (COND_EXPR, tmp, free, build_empty_stmt (input_location));
      gfc_add_expr_to_block (&se->post, tmp);

      se->expr = tmpdecl;
      se->string_length = fold_convert (gfc_charlen_type_node, dest_word_len);
    }
  else
    {
      tmpdecl = gfc_create_var (mold_type, "transfer");

      ptr = convert (build_pointer_type (mold_type), source);

      /* For CLASS results, allocate the needed memory first.  */
      if (mold_expr->ts.type == BT_CLASS)
	{
	  tree cdata;
	  cdata = gfc_class_data_get (tmpdecl);
	  tmp = gfc_call_malloc (&se->pre, TREE_TYPE (cdata), dest_word_len);
	  gfc_add_modify (&se->pre, cdata, tmp);
	}

      /* Use memcpy to do the transfer.  */
      if (mold_expr->ts.type == BT_CLASS)
	tmp = gfc_class_data_get (tmpdecl);
      else
	tmp = gfc_build_addr_expr (NULL_TREE, tmpdecl);

      tmp = build_call_expr_loc (input_location,
			     builtin_decl_explicit (BUILT_IN_MEMCPY), 3,
			     fold_convert (pvoid_type_node, tmp),
			     fold_convert (pvoid_type_node, ptr),
			     fold_convert (size_type_node, extent));
      gfc_add_expr_to_block (&se->pre, tmp);

      /* For CLASS results, set the _vptr.  */
      if (mold_expr->ts.type == BT_CLASS)
	{
	  tree vptr;
	  gfc_symbol *vtab;
	  vptr = gfc_class_vptr_get (tmpdecl);
	  vtab = gfc_find_derived_vtab (source_expr->ts.u.derived);
	  gcc_assert (vtab);
	  tmp = gfc_build_addr_expr (NULL_TREE, gfc_get_symbol_decl (vtab));
	  gfc_add_modify (&se->pre, vptr, fold_convert (TREE_TYPE (vptr), tmp));
	}

      se->expr = tmpdecl;
    }
}


/* Generate code for the ALLOCATED intrinsic.
   Generate inline code that directly check the address of the argument.  */

static void
gfc_conv_allocated (gfc_se *se, gfc_expr *expr)
{
  gfc_actual_arglist *arg1;
  gfc_se arg1se;
  tree tmp;

  gfc_init_se (&arg1se, NULL);
  arg1 = expr->value.function.actual;

  if (arg1->expr->ts.type == BT_CLASS)
    {
      /* Make sure that class array expressions have both a _data
	 component reference and an array reference....  */
      if (CLASS_DATA (arg1->expr)->attr.dimension)
	gfc_add_class_array_ref (arg1->expr);
      /* .... whilst scalars only need the _data component.  */
      else
	gfc_add_data_component (arg1->expr);
    }

  if (arg1->expr->rank == 0)
    {
      /* Allocatable scalar.  */
      arg1se.want_pointer = 1;
      gfc_conv_expr (&arg1se, arg1->expr);
      tmp = arg1se.expr;
    }
  else
    {
      /* Allocatable array.  */
      arg1se.descriptor_only = 1;
      gfc_conv_expr_descriptor (&arg1se, arg1->expr);
      tmp = gfc_conv_descriptor_data_get (arg1se.expr);
    }

  tmp = fold_build2_loc (input_location, NE_EXPR, boolean_type_node, tmp,
			 fold_convert (TREE_TYPE (tmp), null_pointer_node));
  se->expr = convert (gfc_typenode_for_spec (&expr->ts), tmp);
}


/* Generate code for the ASSOCIATED intrinsic.
   If both POINTER and TARGET are arrays, generate a call to library function
   _gfor_associated, and pass descriptors of POINTER and TARGET to it.
   In other cases, generate inline code that directly compare the address of
   POINTER with the address of TARGET.  */

static void
gfc_conv_associated (gfc_se *se, gfc_expr *expr)
{
  gfc_actual_arglist *arg1;
  gfc_actual_arglist *arg2;
  gfc_se arg1se;
  gfc_se arg2se;
  tree tmp2;
  tree tmp;
  tree nonzero_charlen;
  tree nonzero_arraylen;
  gfc_ss *ss;
  bool scalar;

  gfc_init_se (&arg1se, NULL);
  gfc_init_se (&arg2se, NULL);
  arg1 = expr->value.function.actual;
  arg2 = arg1->next;

  /* Check whether the expression is a scalar or not; we cannot use
     arg1->expr->rank as it can be nonzero for proc pointers.  */
  ss = gfc_walk_expr (arg1->expr);
  scalar = ss == gfc_ss_terminator;
  if (!scalar)
    gfc_free_ss_chain (ss);

  if (!arg2->expr)
    {
      /* No optional target.  */
      if (scalar)
        {
	  /* A pointer to a scalar.  */
	  arg1se.want_pointer = 1;
	  gfc_conv_expr (&arg1se, arg1->expr);
	  if (arg1->expr->symtree->n.sym->attr.proc_pointer
	      && arg1->expr->symtree->n.sym->attr.dummy)
	    arg1se.expr = build_fold_indirect_ref_loc (input_location,
						       arg1se.expr);
	  if (arg1->expr->ts.type == BT_CLASS)
	      tmp2 = gfc_class_data_get (arg1se.expr);
	  else
	    tmp2 = arg1se.expr;
        }
      else
        {
          /* A pointer to an array.  */
          gfc_conv_expr_descriptor (&arg1se, arg1->expr);
          tmp2 = gfc_conv_descriptor_data_get (arg1se.expr);
        }
      gfc_add_block_to_block (&se->pre, &arg1se.pre);
      gfc_add_block_to_block (&se->post, &arg1se.post);
      tmp = fold_build2_loc (input_location, NE_EXPR, boolean_type_node, tmp2,
			     fold_convert (TREE_TYPE (tmp2), null_pointer_node));
      se->expr = tmp;
    }
  else
    {
      /* An optional target.  */
      if (arg2->expr->ts.type == BT_CLASS)
	gfc_add_data_component (arg2->expr);

      nonzero_charlen = NULL_TREE;
      if (arg1->expr->ts.type == BT_CHARACTER)
	nonzero_charlen = fold_build2_loc (input_location, NE_EXPR,
					   boolean_type_node,
					   arg1->expr->ts.u.cl->backend_decl,
					   integer_zero_node);
      if (scalar)
        {
	  /* A pointer to a scalar.  */
	  arg1se.want_pointer = 1;
	  gfc_conv_expr (&arg1se, arg1->expr);
	  if (arg1->expr->symtree->n.sym->attr.proc_pointer
	      && arg1->expr->symtree->n.sym->attr.dummy)
	    arg1se.expr = build_fold_indirect_ref_loc (input_location,
						       arg1se.expr);
	  if (arg1->expr->ts.type == BT_CLASS)
	    arg1se.expr = gfc_class_data_get (arg1se.expr);

	  arg2se.want_pointer = 1;
	  gfc_conv_expr (&arg2se, arg2->expr);
	  if (arg2->expr->symtree->n.sym->attr.proc_pointer
	      && arg2->expr->symtree->n.sym->attr.dummy)
	    arg2se.expr = build_fold_indirect_ref_loc (input_location,
						       arg2se.expr);
	  gfc_add_block_to_block (&se->pre, &arg1se.pre);
	  gfc_add_block_to_block (&se->post, &arg1se.post);
          tmp = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node,
				 arg1se.expr, arg2se.expr);
          tmp2 = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
				  arg1se.expr, null_pointer_node);
          se->expr = fold_build2_loc (input_location, TRUTH_AND_EXPR,
				      boolean_type_node, tmp, tmp2);
        }
      else
        {
	  /* An array pointer of zero length is not associated if target is
	     present.  */
	  arg1se.descriptor_only = 1;
	  gfc_conv_expr_lhs (&arg1se, arg1->expr);
	  if (arg1->expr->rank == -1)
	    {
	      tmp = gfc_conv_descriptor_rank (arg1se.expr);
	      tmp = fold_build2_loc (input_location, MINUS_EXPR,
				     TREE_TYPE (tmp), tmp, gfc_index_one_node);
	    }
	  else
	    tmp = gfc_rank_cst[arg1->expr->rank - 1];
	  tmp = gfc_conv_descriptor_stride_get (arg1se.expr, tmp);
	  nonzero_arraylen = fold_build2_loc (input_location, NE_EXPR,
					      boolean_type_node, tmp,
					      build_int_cst (TREE_TYPE (tmp), 0));

          /* A pointer to an array, call library function _gfor_associated.  */
          arg1se.want_pointer = 1;
          gfc_conv_expr_descriptor (&arg1se, arg1->expr);

          arg2se.want_pointer = 1;
          gfc_conv_expr_descriptor (&arg2se, arg2->expr);
          gfc_add_block_to_block (&se->pre, &arg2se.pre);
          gfc_add_block_to_block (&se->post, &arg2se.post);
          se->expr = build_call_expr_loc (input_location,
				      gfor_fndecl_associated, 2,
				      arg1se.expr, arg2se.expr);
	  se->expr = convert (boolean_type_node, se->expr);
	  se->expr = fold_build2_loc (input_location, TRUTH_AND_EXPR,
				      boolean_type_node, se->expr,
				      nonzero_arraylen);
        }

      /* If target is present zero character length pointers cannot
	 be associated.  */
      if (nonzero_charlen != NULL_TREE)
	se->expr = fold_build2_loc (input_location, TRUTH_AND_EXPR,
				    boolean_type_node,
				    se->expr, nonzero_charlen);
    }

  se->expr = convert (gfc_typenode_for_spec (&expr->ts), se->expr);
}


/* Generate code for the SAME_TYPE_AS intrinsic.
   Generate inline code that directly checks the vindices.  */

static void
gfc_conv_same_type_as (gfc_se *se, gfc_expr *expr)
{
  gfc_expr *a, *b;
  gfc_se se1, se2;
  tree tmp;
  tree conda = NULL_TREE, condb = NULL_TREE;

  gfc_init_se (&se1, NULL);
  gfc_init_se (&se2, NULL);

  a = expr->value.function.actual->expr;
  b = expr->value.function.actual->next->expr;

  if (UNLIMITED_POLY (a))
    {
      tmp = gfc_class_vptr_get (a->symtree->n.sym->backend_decl);
      conda = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
			       tmp, build_int_cst (TREE_TYPE (tmp), 0));
    }

  if (UNLIMITED_POLY (b))
    {
      tmp = gfc_class_vptr_get (b->symtree->n.sym->backend_decl);
      condb = fold_build2_loc (input_location, NE_EXPR, boolean_type_node,
			       tmp, build_int_cst (TREE_TYPE (tmp), 0));
    }

  if (a->ts.type == BT_CLASS)
    {
      gfc_add_vptr_component (a);
      gfc_add_hash_component (a);
    }
  else if (a->ts.type == BT_DERIVED)
    a = gfc_get_int_expr (gfc_default_integer_kind, NULL,
			  a->ts.u.derived->hash_value);

  if (b->ts.type == BT_CLASS)
    {
      gfc_add_vptr_component (b);
      gfc_add_hash_component (b);
    }
  else if (b->ts.type == BT_DERIVED)
    b = gfc_get_int_expr (gfc_default_integer_kind, NULL,
			  b->ts.u.derived->hash_value);

  gfc_conv_expr (&se1, a);
  gfc_conv_expr (&se2, b);

  tmp = fold_build2_loc (input_location, EQ_EXPR,
			 boolean_type_node, se1.expr,
			 fold_convert (TREE_TYPE (se1.expr), se2.expr));

  if (conda)
    tmp = fold_build2_loc (input_location, TRUTH_ANDIF_EXPR,
			   boolean_type_node, conda, tmp);

  if (condb)
    tmp = fold_build2_loc (input_location, TRUTH_ANDIF_EXPR,
			   boolean_type_node, condb, tmp);

  se->expr = convert (gfc_typenode_for_spec (&expr->ts), tmp);
}


/* Generate code for SELECTED_CHAR_KIND (NAME) intrinsic function.  */

static void
gfc_conv_intrinsic_sc_kind (gfc_se *se, gfc_expr *expr)
{
  tree args[2];

  gfc_conv_intrinsic_function_args (se, expr, args, 2);
  se->expr = build_call_expr_loc (input_location,
			      gfor_fndecl_sc_kind, 2, args[0], args[1]);
  se->expr = fold_convert (gfc_typenode_for_spec (&expr->ts), se->expr);
}


/* Generate code for SELECTED_INT_KIND (R) intrinsic function.  */

static void
gfc_conv_intrinsic_si_kind (gfc_se *se, gfc_expr *expr)
{
  tree arg, type;

  gfc_conv_intrinsic_function_args (se, expr, &arg, 1);

  /* The argument to SELECTED_INT_KIND is INTEGER(4).  */
  type = gfc_get_int_type (4); 
  arg = gfc_build_addr_expr (NULL_TREE, fold_convert (type, arg));

  /* Convert it to the required type.  */
  type = gfc_typenode_for_spec (&expr->ts);
  se->expr = build_call_expr_loc (input_location,
			      gfor_fndecl_si_kind, 1, arg);
  se->expr = fold_convert (type, se->expr);
}


/* Generate code for SELECTED_REAL_KIND (P, R, RADIX) intrinsic function.  */

static void
gfc_conv_intrinsic_sr_kind (gfc_se *se, gfc_expr *expr)
{
  gfc_actual_arglist *actual;
  tree type;
  gfc_se argse;
  vec<tree, va_gc> *args = NULL;

  for (actual = expr->value.function.actual; actual; actual = actual->next)
    {
      gfc_init_se (&argse, se);

      /* Pass a NULL pointer for an absent arg.  */
      if (actual->expr == NULL)
        argse.expr = null_pointer_node;
      else
	{
	  gfc_typespec ts;
          gfc_clear_ts (&ts);

	  if (actual->expr->ts.kind != gfc_c_int_kind)
	    {
  	      /* The arguments to SELECTED_REAL_KIND are INTEGER(4).  */
	      ts.type = BT_INTEGER;
	      ts.kind = gfc_c_int_kind;
	      gfc_convert_type (actual->expr, &ts, 2);
	    }
	  gfc_conv_expr_reference (&argse, actual->expr);
	} 

      gfc_add_block_to_block (&se->pre, &argse.pre);
      gfc_add_block_to_block (&se->post, &argse.post);
      vec_safe_push (args, argse.expr);
    }

  /* Convert it to the required type.  */
  type = gfc_typenode_for_spec (&expr->ts);
  se->expr = build_call_expr_loc_vec (input_location,
				      gfor_fndecl_sr_kind, args);
  se->expr = fold_convert (type, se->expr);
}


/* Generate code for TRIM (A) intrinsic function.  */

static void
gfc_conv_intrinsic_trim (gfc_se * se, gfc_expr * expr)
{
  tree var;
  tree len;
  tree addr;
  tree tmp;
  tree cond;
  tree fndecl;
  tree function;
  tree *args;
  unsigned int num_args;

  num_args = gfc_intrinsic_argument_list_length (expr) + 2;
  args = XALLOCAVEC (tree, num_args);

  var = gfc_create_var (gfc_get_pchar_type (expr->ts.kind), "pstr");
  addr = gfc_build_addr_expr (ppvoid_type_node, var);
  len = gfc_create_var (gfc_charlen_type_node, "len");

  gfc_conv_intrinsic_function_args (se, expr, &args[2], num_args - 2);
  args[0] = gfc_build_addr_expr (NULL_TREE, len);
  args[1] = addr;

  if (expr->ts.kind == 1)
    function = gfor_fndecl_string_trim;
  else if (expr->ts.kind == 4)
    function = gfor_fndecl_string_trim_char4;
  else
    gcc_unreachable ();

  fndecl = build_addr (function, current_function_decl);
  tmp = build_call_array_loc (input_location,
			  TREE_TYPE (TREE_TYPE (function)), fndecl,
			  num_args, args);
  gfc_add_expr_to_block (&se->pre, tmp);

  /* Free the temporary afterwards, if necessary.  */
  cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			  len, build_int_cst (TREE_TYPE (len), 0));
  tmp = gfc_call_free (var);
  tmp = build3_v (COND_EXPR, cond, tmp, build_empty_stmt (input_location));
  gfc_add_expr_to_block (&se->post, tmp);

  se->expr = var;
  se->string_length = len;
}


/* Generate code for REPEAT (STRING, NCOPIES) intrinsic function.  */

static void
gfc_conv_intrinsic_repeat (gfc_se * se, gfc_expr * expr)
{
  tree args[3], ncopies, dest, dlen, src, slen, ncopies_type;
  tree type, cond, tmp, count, exit_label, n, max, largest;
  tree size;
  stmtblock_t block, body;
  int i;

  /* We store in charsize the size of a character.  */
  i = gfc_validate_kind (BT_CHARACTER, expr->ts.kind, false);
  size = build_int_cst (size_type_node, gfc_character_kinds[i].bit_size / 8);

  /* Get the arguments.  */
  gfc_conv_intrinsic_function_args (se, expr, args, 3);
  slen = fold_convert (size_type_node, gfc_evaluate_now (args[0], &se->pre));
  src = args[1];
  ncopies = gfc_evaluate_now (args[2], &se->pre);
  ncopies_type = TREE_TYPE (ncopies);

  /* Check that NCOPIES is not negative.  */
  cond = fold_build2_loc (input_location, LT_EXPR, boolean_type_node, ncopies,
			  build_int_cst (ncopies_type, 0));
  gfc_trans_runtime_check (true, false, cond, &se->pre, &expr->where,
			   "Argument NCOPIES of REPEAT intrinsic is negative "
			   "(its value is %ld)",
			   fold_convert (long_integer_type_node, ncopies));

  /* If the source length is zero, any non negative value of NCOPIES
     is valid, and nothing happens.  */
  n = gfc_create_var (ncopies_type, "ncopies");
  cond = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, slen,
			  build_int_cst (size_type_node, 0));
  tmp = fold_build3_loc (input_location, COND_EXPR, ncopies_type, cond,
			 build_int_cst (ncopies_type, 0), ncopies);
  gfc_add_modify (&se->pre, n, tmp);
  ncopies = n;

  /* Check that ncopies is not too large: ncopies should be less than
     (or equal to) MAX / slen, where MAX is the maximal integer of
     the gfc_charlen_type_node type.  If slen == 0, we need a special
     case to avoid the division by zero.  */
  i = gfc_validate_kind (BT_INTEGER, gfc_charlen_int_kind, false);
  max = gfc_conv_mpz_to_tree (gfc_integer_kinds[i].huge, gfc_charlen_int_kind);
  max = fold_build2_loc (input_location, TRUNC_DIV_EXPR, size_type_node,
			  fold_convert (size_type_node, max), slen);
  largest = TYPE_PRECISION (size_type_node) > TYPE_PRECISION (ncopies_type)
	      ? size_type_node : ncopies_type;
  cond = fold_build2_loc (input_location, GT_EXPR, boolean_type_node,
			  fold_convert (largest, ncopies),
			  fold_convert (largest, max));
  tmp = fold_build2_loc (input_location, EQ_EXPR, boolean_type_node, slen,
			 build_int_cst (size_type_node, 0));
  cond = fold_build3_loc (input_location, COND_EXPR, boolean_type_node, tmp,
			  boolean_false_node, cond);
  gfc_trans_runtime_check (true, false, cond, &se->pre, &expr->where,
			   "Argument NCOPIES of REPEAT intrinsic is too large");

  /* Compute the destination length.  */
  dlen = fold_build2_loc (input_location, MULT_EXPR, gfc_charlen_type_node,
			  fold_convert (gfc_charlen_type_node, slen),
			  fold_convert (gfc_charlen_type_node, ncopies));
  type = gfc_get_character_type (expr->ts.kind, expr->ts.u.cl);
  dest = gfc_conv_string_tmp (se, build_pointer_type (type), dlen);

  /* Generate the code to do the repeat operation:
       for (i = 0; i < ncopies; i++)
         memmove (dest + (i * slen * size), src, slen*size);  */
  gfc_start_block (&block);
  count = gfc_create_var (ncopies_type, "count");
  gfc_add_modify (&block, count, build_int_cst (ncopies_type, 0));
  exit_label = gfc_build_label_decl (NULL_TREE);

  /* Start the loop body.  */
  gfc_start_block (&body);

  /* Exit the loop if count >= ncopies.  */
  cond = fold_build2_loc (input_location, GE_EXPR, boolean_type_node, count,
			  ncopies);
  tmp = build1_v (GOTO_EXPR, exit_label);
  TREE_USED (exit_label) = 1;
  tmp = fold_build3_loc (input_location, COND_EXPR, void_type_node, cond, tmp,
			 build_empty_stmt (input_location));
  gfc_add_expr_to_block (&body, tmp);

  /* Call memmove (dest + (i*slen*size), src, slen*size).  */
  tmp = fold_build2_loc (input_location, MULT_EXPR, gfc_charlen_type_node,
			 fold_convert (gfc_charlen_type_node, slen),
			 fold_convert (gfc_charlen_type_node, count));
  tmp = fold_build2_loc (input_location, MULT_EXPR, gfc_charlen_type_node,
			 tmp, fold_convert (gfc_charlen_type_node, size));
  tmp = fold_build_pointer_plus_loc (input_location,
				     fold_convert (pvoid_type_node, dest), tmp);
  tmp = build_call_expr_loc (input_location,
			     builtin_decl_explicit (BUILT_IN_MEMMOVE),
			     3, tmp, src,
			     fold_build2_loc (input_location, MULT_EXPR,
					      size_type_node, slen,
					      fold_convert (size_type_node,
							    size)));
  gfc_add_expr_to_block (&body, tmp);

  /* Increment count.  */
  tmp = fold_build2_loc (input_location, PLUS_EXPR, ncopies_type,
			 count, build_int_cst (TREE_TYPE (count), 1));
  gfc_add_modify (&body, count, tmp);

  /* Build the loop.  */
  tmp = build1_v (LOOP_EXPR, gfc_finish_block (&body));
  gfc_add_expr_to_block (&block, tmp);

  /* Add the exit label.  */
  tmp = build1_v (LABEL_EXPR, exit_label);
  gfc_add_expr_to_block (&block, tmp);

  /* Finish the block.  */
  tmp = gfc_finish_block (&block);
  gfc_add_expr_to_block (&se->pre, tmp);

  /* Set the result value.  */
  se->expr = dest;
  se->string_length = dlen;
}


/* Generate code for the IARGC intrinsic.  */

static void
gfc_conv_intrinsic_iargc (gfc_se * se, gfc_expr * expr)
{
  tree tmp;
  tree fndecl;
  tree type;

  /* Call the library function.  This always returns an INTEGER(4).  */
  fndecl = gfor_fndecl_iargc;
  tmp = build_call_expr_loc (input_location,
			 fndecl, 0);

  /* Convert it to the required type.  */
  type = gfc_typenode_for_spec (&expr->ts);
  tmp = fold_convert (type, tmp);

  se->expr = tmp;
}


/* The loc intrinsic returns the address of its argument as
   gfc_index_integer_kind integer.  */

static void
gfc_conv_intrinsic_loc (gfc_se * se, gfc_expr * expr)
{
  tree temp_var;
  gfc_expr *arg_expr;

  gcc_assert (!se->ss);

  arg_expr = expr->value.function.actual->expr;
  if (arg_expr->rank == 0)
    gfc_conv_expr_reference (se, arg_expr);
  else
    gfc_conv_array_parameter (se, arg_expr, true, NULL, NULL, NULL);
  se->expr= convert (gfc_get_int_type (gfc_index_integer_kind), se->expr);
   
  /* Create a temporary variable for loc return value.  Without this, 
     we get an error an ICE in gcc/expr.c(expand_expr_addr_expr_1).  */
  temp_var = gfc_create_var (gfc_get_int_type (gfc_index_integer_kind), NULL);
  gfc_add_modify (&se->pre, temp_var, se->expr);
  se->expr = temp_var;
}

/* Generate code for an intrinsic function.  Some map directly to library
   calls, others get special handling.  In some cases the name of the function
   used depends on the type specifiers.  */

void
gfc_conv_intrinsic_function (gfc_se * se, gfc_expr * expr)
{
  const char *name;
  int lib, kind;
  tree fndecl;

  name = &expr->value.function.name[2];

  if (expr->rank > 0)
    {
      lib = gfc_is_intrinsic_libcall (expr);
      if (lib != 0)
	{
	  if (lib == 1)
	    se->ignore_optional = 1;

	  switch (expr->value.function.isym->id)
	    {
	    case GFC_ISYM_EOSHIFT:
	    case GFC_ISYM_PACK:
	    case GFC_ISYM_RESHAPE:
	      /* For all of those the first argument specifies the type and the
		 third is optional.  */
	      conv_generic_with_optional_char_arg (se, expr, 1, 3);
	      break;

	    default:
	      gfc_conv_intrinsic_funcall (se, expr);
	      break;
	    }

	  return;
	}
    }

  switch (expr->value.function.isym->id)
    {
    case GFC_ISYM_NONE:
      gcc_unreachable ();

    case GFC_ISYM_REPEAT:
      gfc_conv_intrinsic_repeat (se, expr);
      break;

    case GFC_ISYM_TRIM:
      gfc_conv_intrinsic_trim (se, expr);
      break;

    case GFC_ISYM_SC_KIND:
      gfc_conv_intrinsic_sc_kind (se, expr);
      break;

    case GFC_ISYM_SI_KIND:
      gfc_conv_intrinsic_si_kind (se, expr);
      break;

    case GFC_ISYM_SR_KIND:
      gfc_conv_intrinsic_sr_kind (se, expr);
      break;

    case GFC_ISYM_EXPONENT:
      gfc_conv_intrinsic_exponent (se, expr);
      break;

    case GFC_ISYM_SCAN:
      kind = expr->value.function.actual->expr->ts.kind;
      if (kind == 1)
       fndecl = gfor_fndecl_string_scan;
      else if (kind == 4)
       fndecl = gfor_fndecl_string_scan_char4;
      else
       gcc_unreachable ();

      gfc_conv_intrinsic_index_scan_verify (se, expr, fndecl);
      break;

    case GFC_ISYM_VERIFY:
      kind = expr->value.function.actual->expr->ts.kind;
      if (kind == 1)
       fndecl = gfor_fndecl_string_verify;
      else if (kind == 4)
       fndecl = gfor_fndecl_string_verify_char4;
      else
       gcc_unreachable ();

      gfc_conv_intrinsic_index_scan_verify (se, expr, fndecl);
      break;

    case GFC_ISYM_ALLOCATED:
      gfc_conv_allocated (se, expr);
      break;

    case GFC_ISYM_ASSOCIATED:
      gfc_conv_associated(se, expr);
      break;

    case GFC_ISYM_SAME_TYPE_AS:
      gfc_conv_same_type_as (se, expr);
      break;

    case GFC_ISYM_ABS:
      gfc_conv_intrinsic_abs (se, expr);
      break;

    case GFC_ISYM_ADJUSTL:
      if (expr->ts.kind == 1)
       fndecl = gfor_fndecl_adjustl;
      else if (expr->ts.kind == 4)
       fndecl = gfor_fndecl_adjustl_char4;
      else
       gcc_unreachable ();

      gfc_conv_intrinsic_adjust (se, expr, fndecl);
      break;

    case GFC_ISYM_ADJUSTR:
      if (expr->ts.kind == 1)
       fndecl = gfor_fndecl_adjustr;
      else if (expr->ts.kind == 4)
       fndecl = gfor_fndecl_adjustr_char4;
      else
       gcc_unreachable ();

      gfc_conv_intrinsic_adjust (se, expr, fndecl);
      break;

    case GFC_ISYM_AIMAG:
      gfc_conv_intrinsic_imagpart (se, expr);
      break;

    case GFC_ISYM_AINT:
      gfc_conv_intrinsic_aint (se, expr, RND_TRUNC);
      break;

    case GFC_ISYM_ALL:
      gfc_conv_intrinsic_anyall (se, expr, EQ_EXPR);
      break;

    case GFC_ISYM_ANINT:
      gfc_conv_intrinsic_aint (se, expr, RND_ROUND);
      break;

    case GFC_ISYM_AND:
      gfc_conv_intrinsic_bitop (se, expr, BIT_AND_EXPR);
      break;

    case GFC_ISYM_ANY:
      gfc_conv_intrinsic_anyall (se, expr, NE_EXPR);
      break;

    case GFC_ISYM_BTEST:
      gfc_conv_intrinsic_btest (se, expr);
      break;

    case GFC_ISYM_BGE:
      gfc_conv_intrinsic_bitcomp (se, expr, GE_EXPR);
      break;

    case GFC_ISYM_BGT:
      gfc_conv_intrinsic_bitcomp (se, expr, GT_EXPR);
      break;

    case GFC_ISYM_BLE:
      gfc_conv_intrinsic_bitcomp (se, expr, LE_EXPR);
      break;

    case GFC_ISYM_BLT:
      gfc_conv_intrinsic_bitcomp (se, expr, LT_EXPR);
      break;

    case GFC_ISYM_ACHAR:
    case GFC_ISYM_CHAR:
      gfc_conv_intrinsic_char (se, expr);
      break;

    case GFC_ISYM_CONVERSION:
    case GFC_ISYM_REAL:
    case GFC_ISYM_LOGICAL:
    case GFC_ISYM_DBLE:
      gfc_conv_intrinsic_conversion (se, expr);
      break;

      /* Integer conversions are handled separately to make sure we get the
         correct rounding mode.  */
    case GFC_ISYM_INT:
    case GFC_ISYM_INT2:
    case GFC_ISYM_INT8:
    case GFC_ISYM_LONG:
      gfc_conv_intrinsic_int (se, expr, RND_TRUNC);
      break;

    case GFC_ISYM_NINT:
      gfc_conv_intrinsic_int (se, expr, RND_ROUND);
      break;

    case GFC_ISYM_CEILING:
      gfc_conv_intrinsic_int (se, expr, RND_CEIL);
      break;

    case GFC_ISYM_FLOOR:
      gfc_conv_intrinsic_int (se, expr, RND_FLOOR);
      break;

    case GFC_ISYM_MOD:
      gfc_conv_intrinsic_mod (se, expr, 0);
      break;

    case GFC_ISYM_MODULO:
      gfc_conv_intrinsic_mod (se, expr, 1);
      break;

    case GFC_ISYM_CMPLX:
      gfc_conv_intrinsic_cmplx (se, expr, name[5] == '1');
      break;

    case GFC_ISYM_COMMAND_ARGUMENT_COUNT:
      gfc_conv_intrinsic_iargc (se, expr);
      break;

    case GFC_ISYM_COMPLEX:
      gfc_conv_intrinsic_cmplx (se, expr, 1);
      break;

    case GFC_ISYM_CONJG:
      gfc_conv_intrinsic_conjg (se, expr);
      break;

    case GFC_ISYM_COUNT:
      gfc_conv_intrinsic_count (se, expr);
      break;

    case GFC_ISYM_CTIME:
      gfc_conv_intrinsic_ctime (se, expr);
      break;

    case GFC_ISYM_DIM:
      gfc_conv_intrinsic_dim (se, expr);
      break;

    case GFC_ISYM_DOT_PRODUCT:
      gfc_conv_intrinsic_dot_product (se, expr);
      break;

    case GFC_ISYM_DPROD:
      gfc_conv_intrinsic_dprod (se, expr);
      break;

    case GFC_ISYM_DSHIFTL:
      gfc_conv_intrinsic_dshift (se, expr, true);
      break;

    case GFC_ISYM_DSHIFTR:
      gfc_conv_intrinsic_dshift (se, expr, false);
      break;

    case GFC_ISYM_FDATE:
      gfc_conv_intrinsic_fdate (se, expr);
      break;

    case GFC_ISYM_FRACTION:
      gfc_conv_intrinsic_fraction (se, expr);
      break;

    case GFC_ISYM_IALL:
      gfc_conv_intrinsic_arith (se, expr, BIT_AND_EXPR, false);
      break;

    case GFC_ISYM_IAND:
      gfc_conv_intrinsic_bitop (se, expr, BIT_AND_EXPR);
      break;

    case GFC_ISYM_IANY:
      gfc_conv_intrinsic_arith (se, expr, BIT_IOR_EXPR, false);
      break;

    case GFC_ISYM_IBCLR:
      gfc_conv_intrinsic_singlebitop (se, expr, 0);
      break;

    case GFC_ISYM_IBITS:
      gfc_conv_intrinsic_ibits (se, expr);
      break;

    case GFC_ISYM_IBSET:
      gfc_conv_intrinsic_singlebitop (se, expr, 1);
      break;

    case GFC_ISYM_IACHAR:
    case GFC_ISYM_ICHAR:
      /* We assume ASCII character sequence.  */
      gfc_conv_intrinsic_ichar (se, expr);
      break;

    case GFC_ISYM_IARGC:
      gfc_conv_intrinsic_iargc (se, expr);
      break;

    case GFC_ISYM_IEOR:
      gfc_conv_intrinsic_bitop (se, expr, BIT_XOR_EXPR);
      break;

    case GFC_ISYM_INDEX:
      kind = expr->value.function.actual->expr->ts.kind;
      if (kind == 1)
       fndecl = gfor_fndecl_string_index;
      else if (kind == 4)
       fndecl = gfor_fndecl_string_index_char4;
      else
       gcc_unreachable ();

      gfc_conv_intrinsic_index_scan_verify (se, expr, fndecl);
      break;

    case GFC_ISYM_IOR:
      gfc_conv_intrinsic_bitop (se, expr, BIT_IOR_EXPR);
      break;

    case GFC_ISYM_IPARITY:
      gfc_conv_intrinsic_arith (se, expr, BIT_XOR_EXPR, false);
      break;

    case GFC_ISYM_IS_IOSTAT_END:
      gfc_conv_has_intvalue (se, expr, LIBERROR_END);
      break;

    case GFC_ISYM_IS_IOSTAT_EOR:
      gfc_conv_has_intvalue (se, expr, LIBERROR_EOR);
      break;

    case GFC_ISYM_ISNAN:
      gfc_conv_intrinsic_isnan (se, expr);
      break;

    case GFC_ISYM_LSHIFT:
      gfc_conv_intrinsic_shift (se, expr, false, false);
      break;

    case GFC_ISYM_RSHIFT:
      gfc_conv_intrinsic_shift (se, expr, true, true);
      break;

    case GFC_ISYM_SHIFTA:
      gfc_conv_intrinsic_shift (se, expr, true, true);
      break;

    case GFC_ISYM_SHIFTL:
      gfc_conv_intrinsic_shift (se, expr, false, false);
      break;

    case GFC_ISYM_SHIFTR:
      gfc_conv_intrinsic_shift (se, expr, true, false);
      break;

    case GFC_ISYM_ISHFT:
      gfc_conv_intrinsic_ishft (se, expr);
      break;

    case GFC_ISYM_ISHFTC:
      gfc_conv_intrinsic_ishftc (se, expr);
      break;

    case GFC_ISYM_LEADZ:
      gfc_conv_intrinsic_leadz (se, expr);
      break;

    case GFC_ISYM_TRAILZ:
      gfc_conv_intrinsic_trailz (se, expr);
      break;

    case GFC_ISYM_POPCNT:
      gfc_conv_intrinsic_popcnt_poppar (se, expr, 0);
      break;

    case GFC_ISYM_POPPAR:
      gfc_conv_intrinsic_popcnt_poppar (se, expr, 1);
      break;

    case GFC_ISYM_LBOUND:
      gfc_conv_intrinsic_bound (se, expr, 0);
      break;

    case GFC_ISYM_LCOBOUND:
      conv_intrinsic_cobound (se, expr);
      break;

    case GFC_ISYM_TRANSPOSE:
      /* The scalarizer has already been set up for reversed dimension access
	 order ; now we just get the argument value normally.  */
      gfc_conv_expr (se, expr->value.function.actual->expr);
      break;

    case GFC_ISYM_LEN:
      gfc_conv_intrinsic_len (se, expr);
      break;

    case GFC_ISYM_LEN_TRIM:
      gfc_conv_intrinsic_len_trim (se, expr);
      break;

    case GFC_ISYM_LGE:
      gfc_conv_intrinsic_strcmp (se, expr, GE_EXPR);
      break;

    case GFC_ISYM_LGT:
      gfc_conv_intrinsic_strcmp (se, expr, GT_EXPR);
      break;

    case GFC_ISYM_LLE:
      gfc_conv_intrinsic_strcmp (se, expr, LE_EXPR);
      break;

    case GFC_ISYM_LLT:
      gfc_conv_intrinsic_strcmp (se, expr, LT_EXPR);
      break;

    case GFC_ISYM_MASKL:
      gfc_conv_intrinsic_mask (se, expr, 1);
      break;

    case GFC_ISYM_MASKR:
      gfc_conv_intrinsic_mask (se, expr, 0);
      break;

    case GFC_ISYM_MAX:
      if (expr->ts.type == BT_CHARACTER)
	gfc_conv_intrinsic_minmax_char (se, expr, 1);
      else
	gfc_conv_intrinsic_minmax (se, expr, GT_EXPR);
      break;

    case GFC_ISYM_MAXLOC:
      gfc_conv_intrinsic_minmaxloc (se, expr, GT_EXPR);
      break;

    case GFC_ISYM_MAXVAL:
      gfc_conv_intrinsic_minmaxval (se, expr, GT_EXPR);
      break;

    case GFC_ISYM_MERGE:
      gfc_conv_intrinsic_merge (se, expr);
      break;

    case GFC_ISYM_MERGE_BITS:
      gfc_conv_intrinsic_merge_bits (se, expr);
      break;

    case GFC_ISYM_MIN:
      if (expr->ts.type == BT_CHARACTER)
	gfc_conv_intrinsic_minmax_char (se, expr, -1);
      else
	gfc_conv_intrinsic_minmax (se, expr, LT_EXPR);
      break;

    case GFC_ISYM_MINLOC:
      gfc_conv_intrinsic_minmaxloc (se, expr, LT_EXPR);
      break;

    case GFC_ISYM_MINVAL:
      gfc_conv_intrinsic_minmaxval (se, expr, LT_EXPR);
      break;

    case GFC_ISYM_NEAREST:
      gfc_conv_intrinsic_nearest (se, expr);
      break;

    case GFC_ISYM_NORM2:
      gfc_conv_intrinsic_arith (se, expr, PLUS_EXPR, true);
      break;

    case GFC_ISYM_NOT:
      gfc_conv_intrinsic_not (se, expr);
      break;

    case GFC_ISYM_OR:
      gfc_conv_intrinsic_bitop (se, expr, BIT_IOR_EXPR);
      break;

    case GFC_ISYM_PARITY:
      gfc_conv_intrinsic_arith (se, expr, NE_EXPR, false);
      break;

    case GFC_ISYM_PRESENT:
      gfc_conv_intrinsic_present (se, expr);
      break;

    case GFC_ISYM_PRODUCT:
      gfc_conv_intrinsic_arith (se, expr, MULT_EXPR, false);
      break;

    case GFC_ISYM_RANK:
      gfc_conv_intrinsic_rank (se, expr);
      break;

    case GFC_ISYM_RRSPACING:
      gfc_conv_intrinsic_rrspacing (se, expr);
      break;

    case GFC_ISYM_SET_EXPONENT:
      gfc_conv_intrinsic_set_exponent (se, expr);
      break;

    case GFC_ISYM_SCALE:
      gfc_conv_intrinsic_scale (se, expr);
      break;

    case GFC_ISYM_SIGN:
      gfc_conv_intrinsic_sign (se, expr);
      break;

    case GFC_ISYM_SIZE:
      gfc_conv_intrinsic_size (se, expr);
      break;

    case GFC_ISYM_SIZEOF:
    case GFC_ISYM_C_SIZEOF:
      gfc_conv_intrinsic_sizeof (se, expr);
      break;

    case GFC_ISYM_STORAGE_SIZE:
      gfc_conv_intrinsic_storage_size (se, expr);
      break;

    case GFC_ISYM_SPACING:
      gfc_conv_intrinsic_spacing (se, expr);
      break;

    case GFC_ISYM_STRIDE:
      conv_intrinsic_stride (se, expr);
      break;

    case GFC_ISYM_SUM:
      gfc_conv_intrinsic_arith (se, expr, PLUS_EXPR, false);
      break;

    case GFC_ISYM_TRANSFER:
      if (se->ss && se->ss->info->useflags)
	/* Access the previously obtained result.  */
	gfc_conv_tmp_array_ref (se);
      else
	gfc_conv_intrinsic_transfer (se, expr);
      break;

    case GFC_ISYM_TTYNAM:
      gfc_conv_intrinsic_ttynam (se, expr);
      break;

    case GFC_ISYM_UBOUND:
      gfc_conv_intrinsic_bound (se, expr, 1);
      break;

    case GFC_ISYM_UCOBOUND:
      conv_intrinsic_cobound (se, expr);
      break;

    case GFC_ISYM_XOR:
      gfc_conv_intrinsic_bitop (se, expr, BIT_XOR_EXPR);
      break;

    case GFC_ISYM_LOC:
      gfc_conv_intrinsic_loc (se, expr);
      break;

    case GFC_ISYM_THIS_IMAGE:
      /* For num_images() == 1, handle as LCOBOUND.  */
      if (expr->value.function.actual->expr
	  && gfc_option.coarray == GFC_FCOARRAY_SINGLE)
	conv_intrinsic_cobound (se, expr);
      else
	trans_this_image (se, expr);
      break;

    case GFC_ISYM_IMAGE_INDEX:
      trans_image_index (se, expr);
      break;

    case GFC_ISYM_NUM_IMAGES:
      trans_num_images (se);
      break;

    case GFC_ISYM_ACCESS:
    case GFC_ISYM_CHDIR:
    case GFC_ISYM_CHMOD:
    case GFC_ISYM_DTIME:
    case GFC_ISYM_ETIME:
    case GFC_ISYM_EXTENDS_TYPE_OF:
    case GFC_ISYM_FGET:
    case GFC_ISYM_FGETC:
    case GFC_ISYM_FNUM:
    case GFC_ISYM_FPUT:
    case GFC_ISYM_FPUTC:
    case GFC_ISYM_FSTAT:
    case GFC_ISYM_FTELL:
    case GFC_ISYM_GETCWD:
    case GFC_ISYM_GETGID:
    case GFC_ISYM_GETPID:
    case GFC_ISYM_GETUID:
    case GFC_ISYM_HOSTNM:
    case GFC_ISYM_KILL:
    case GFC_ISYM_IERRNO:
    case GFC_ISYM_IRAND:
    case GFC_ISYM_ISATTY:
    case GFC_ISYM_JN2:
    case GFC_ISYM_LINK:
    case GFC_ISYM_LSTAT:
    case GFC_ISYM_MALLOC:
    case GFC_ISYM_MATMUL:
    case GFC_ISYM_MCLOCK:
    case GFC_ISYM_MCLOCK8:
    case GFC_ISYM_RAND:
    case GFC_ISYM_RENAME:
    case GFC_ISYM_SECOND:
    case GFC_ISYM_SECNDS:
    case GFC_ISYM_SIGNAL:
    case GFC_ISYM_STAT:
    case GFC_ISYM_SYMLNK:
    case GFC_ISYM_SYSTEM:
    case GFC_ISYM_TIME:
    case GFC_ISYM_TIME8:
    case GFC_ISYM_UMASK:
    case GFC_ISYM_UNLINK:
    case GFC_ISYM_YN2:
      gfc_conv_intrinsic_funcall (se, expr);
      break;

    case GFC_ISYM_EOSHIFT:
    case GFC_ISYM_PACK:
    case GFC_ISYM_RESHAPE:
      /* For those, expr->rank should always be >0 and thus the if above the
	 switch should have matched.  */
      gcc_unreachable ();
      break;

    default:
      gfc_conv_intrinsic_lib_function (se, expr);
      break;
    }
}


static gfc_ss *
walk_inline_intrinsic_transpose (gfc_ss *ss, gfc_expr *expr)
{
  gfc_ss *arg_ss, *tmp_ss;
  gfc_actual_arglist *arg;

  arg = expr->value.function.actual;

  gcc_assert (arg->expr);

  arg_ss = gfc_walk_subexpr (gfc_ss_terminator, arg->expr);
  gcc_assert (arg_ss != gfc_ss_terminator);

  for (tmp_ss = arg_ss; ; tmp_ss = tmp_ss->next)
    {
      if (tmp_ss->info->type != GFC_SS_SCALAR
	  && tmp_ss->info->type != GFC_SS_REFERENCE)
	{
	  int tmp_dim;

	  gcc_assert (tmp_ss->dimen == 2);

	  /* We just invert dimensions.  */
	  tmp_dim = tmp_ss->dim[0];
	  tmp_ss->dim[0] = tmp_ss->dim[1];
	  tmp_ss->dim[1] = tmp_dim;
	}

      /* Stop when tmp_ss points to the last valid element of the chain...  */
      if (tmp_ss->next == gfc_ss_terminator)
	break;
    }

  /* ... so that we can attach the rest of the chain to it.  */
  tmp_ss->next = ss;

  return arg_ss;
}


/* Move the given dimension of the given gfc_ss list to a nested gfc_ss list.
   This has the side effect of reversing the nested list, so there is no
   need to call gfc_reverse_ss on it (the given list is assumed not to be
   reversed yet).   */

static gfc_ss *
nest_loop_dimension (gfc_ss *ss, int dim)
{
  int ss_dim, i;
  gfc_ss *new_ss, *prev_ss = gfc_ss_terminator;
  gfc_loopinfo *new_loop;

  gcc_assert (ss != gfc_ss_terminator);

  for (; ss != gfc_ss_terminator; ss = ss->next)
    {
      new_ss = gfc_get_ss ();
      new_ss->next = prev_ss;
      new_ss->parent = ss;
      new_ss->info = ss->info;
      new_ss->info->refcount++;
      if (ss->dimen != 0)
	{
	  gcc_assert (ss->info->type != GFC_SS_SCALAR
		      && ss->info->type != GFC_SS_REFERENCE);

	  new_ss->dimen = 1;
	  new_ss->dim[0] = ss->dim[dim];

	  gcc_assert (dim < ss->dimen);

	  ss_dim = --ss->dimen;
	  for (i = dim; i < ss_dim; i++)
	    ss->dim[i] = ss->dim[i + 1];

	  ss->dim[ss_dim] = 0;
	}
      prev_ss = new_ss;

      if (ss->nested_ss)
	{
	  ss->nested_ss->parent = new_ss;
	  new_ss->nested_ss = ss->nested_ss;
	}
      ss->nested_ss = new_ss;
    }

  new_loop = gfc_get_loopinfo ();
  gfc_init_loopinfo (new_loop);

  gcc_assert (prev_ss != NULL);
  gcc_assert (prev_ss != gfc_ss_terminator);
  gfc_add_ss_to_loop (new_loop, prev_ss);
  return new_ss->parent;
}


/* Create the gfc_ss list for the SUM/PRODUCT arguments when the function
   is to be inlined.  */

static gfc_ss *
walk_inline_intrinsic_arith (gfc_ss *ss, gfc_expr *expr)
{
  gfc_ss *tmp_ss, *tail, *array_ss;
  gfc_actual_arglist *arg1, *arg2, *arg3;
  int sum_dim;
  bool scalar_mask = false;

  /* The rank of the result will be determined later.  */
  arg1 = expr->value.function.actual;
  arg2 = arg1->next;
  arg3 = arg2->next;
  gcc_assert (arg3 != NULL);

  if (expr->rank == 0)
    return ss;

  tmp_ss = gfc_ss_terminator;

  if (arg3->expr)
    {
      gfc_ss *mask_ss;

      mask_ss = gfc_walk_subexpr (tmp_ss, arg3->expr);
      if (mask_ss == tmp_ss)
	scalar_mask = 1;

      tmp_ss = mask_ss;
    }

  array_ss = gfc_walk_subexpr (tmp_ss, arg1->expr);
  gcc_assert (array_ss != tmp_ss);

  /* Odd thing: If the mask is scalar, it is used by the frontend after
     the array (to make an if around the nested loop). Thus it shall
     be after array_ss once the gfc_ss list is reversed.  */
  if (scalar_mask)
    tmp_ss = gfc_get_scalar_ss (array_ss, arg3->expr);
  else
    tmp_ss = array_ss;

  /* "Hide" the dimension on which we will sum in the first arg's scalarization
     chain.  */
  sum_dim = mpz_get_si (arg2->expr->value.integer) - 1;
  tail = nest_loop_dimension (tmp_ss, sum_dim);
  tail->next = ss;

  return tmp_ss;
}


static gfc_ss *
walk_inline_intrinsic_function (gfc_ss * ss, gfc_expr * expr)
{

  switch (expr->value.function.isym->id)
    {
      case GFC_ISYM_PRODUCT:
      case GFC_ISYM_SUM:
	return walk_inline_intrinsic_arith (ss, expr);

      case GFC_ISYM_TRANSPOSE:
	return walk_inline_intrinsic_transpose (ss, expr);

      default:
	gcc_unreachable ();
    }
  gcc_unreachable ();
}


/* This generates code to execute before entering the scalarization loop.
   Currently does nothing.  */

void
gfc_add_intrinsic_ss_code (gfc_loopinfo * loop ATTRIBUTE_UNUSED, gfc_ss * ss)
{
  switch (ss->info->expr->value.function.isym->id)
    {
    case GFC_ISYM_UBOUND:
    case GFC_ISYM_LBOUND:
    case GFC_ISYM_UCOBOUND:
    case GFC_ISYM_LCOBOUND:
    case GFC_ISYM_THIS_IMAGE:
      break;

    default:
      gcc_unreachable ();
    }
}


/* The LBOUND, LCOBOUND, UBOUND and UCOBOUND intrinsics with one parameter
   are expanded into code inside the scalarization loop.  */

static gfc_ss *
gfc_walk_intrinsic_bound (gfc_ss * ss, gfc_expr * expr)
{
  if (expr->value.function.actual->expr->ts.type == BT_CLASS)
    gfc_add_class_array_ref (expr->value.function.actual->expr);

  /* The two argument version returns a scalar.  */
  if (expr->value.function.actual->next->expr)
    return ss;

  return gfc_get_array_ss (ss, expr, 1, GFC_SS_INTRINSIC);
}


/* Walk an intrinsic array libcall.  */

static gfc_ss *
gfc_walk_intrinsic_libfunc (gfc_ss * ss, gfc_expr * expr)
{
  gcc_assert (expr->rank > 0);
  return gfc_get_array_ss (ss, expr, expr->rank, GFC_SS_FUNCTION);
}


/* Return whether the function call expression EXPR will be expanded
   inline by gfc_conv_intrinsic_function.  */

bool
gfc_inline_intrinsic_function_p (gfc_expr *expr)
{
  gfc_actual_arglist *args;

  if (!expr->value.function.isym)
    return false;

  switch (expr->value.function.isym->id)
    {
    case GFC_ISYM_PRODUCT:
    case GFC_ISYM_SUM:
      /* Disable inline expansion if code size matters.  */
      if (optimize_size)
	return false;

      args = expr->value.function.actual;
      /* We need to be able to subset the SUM argument at compile-time.  */
      if (args->next->expr && args->next->expr->expr_type != EXPR_CONSTANT)
	return false;

      return true;

    case GFC_ISYM_TRANSPOSE:
      return true;

    default:
      return false;
    }
}


/* Returns nonzero if the specified intrinsic function call maps directly to
   an external library call.  Should only be used for functions that return
   arrays.  */

int
gfc_is_intrinsic_libcall (gfc_expr * expr)
{
  gcc_assert (expr->expr_type == EXPR_FUNCTION && expr->value.function.isym);
  gcc_assert (expr->rank > 0);

  if (gfc_inline_intrinsic_function_p (expr))
    return 0;

  switch (expr->value.function.isym->id)
    {
    case GFC_ISYM_ALL:
    case GFC_ISYM_ANY:
    case GFC_ISYM_COUNT:
    case GFC_ISYM_JN2:
    case GFC_ISYM_IANY:
    case GFC_ISYM_IALL:
    case GFC_ISYM_IPARITY:
    case GFC_ISYM_MATMUL:
    case GFC_ISYM_MAXLOC:
    case GFC_ISYM_MAXVAL:
    case GFC_ISYM_MINLOC:
    case GFC_ISYM_MINVAL:
    case GFC_ISYM_NORM2:
    case GFC_ISYM_PARITY:
    case GFC_ISYM_PRODUCT:
    case GFC_ISYM_SUM:
    case GFC_ISYM_SHAPE:
    case GFC_ISYM_SPREAD:
    case GFC_ISYM_YN2:
      /* Ignore absent optional parameters.  */
      return 1;

    case GFC_ISYM_RESHAPE:
    case GFC_ISYM_CSHIFT:
    case GFC_ISYM_EOSHIFT:
    case GFC_ISYM_PACK:
    case GFC_ISYM_UNPACK:
      /* Pass absent optional parameters.  */
      return 2;

    default:
      return 0;
    }
}

/* Walk an intrinsic function.  */
gfc_ss *
gfc_walk_intrinsic_function (gfc_ss * ss, gfc_expr * expr,
			     gfc_intrinsic_sym * isym)
{
  gcc_assert (isym);

  if (isym->elemental)
    return gfc_walk_elemental_function_args (ss, expr->value.function.actual,
					     NULL, GFC_SS_SCALAR);

  if (expr->rank == 0)
    return ss;

  if (gfc_inline_intrinsic_function_p (expr))
    return walk_inline_intrinsic_function (ss, expr);

  if (gfc_is_intrinsic_libcall (expr))
    return gfc_walk_intrinsic_libfunc (ss, expr);

  /* Special cases.  */
  switch (isym->id)
    {
    case GFC_ISYM_LBOUND:
    case GFC_ISYM_LCOBOUND:
    case GFC_ISYM_UBOUND:
    case GFC_ISYM_UCOBOUND:
    case GFC_ISYM_THIS_IMAGE:
      return gfc_walk_intrinsic_bound (ss, expr);

    case GFC_ISYM_TRANSFER:
      return gfc_walk_intrinsic_libfunc (ss, expr);

    default:
      /* This probably meant someone forgot to add an intrinsic to the above
         list(s) when they implemented it, or something's gone horribly
	 wrong.  */
      gcc_unreachable ();
    }
}


static tree
conv_intrinsic_atomic_def (gfc_code *code)
{
  gfc_se atom, value;
  stmtblock_t block;

  gfc_init_se (&atom, NULL);
  gfc_init_se (&value, NULL);
  gfc_conv_expr (&atom, code->ext.actual->expr);
  gfc_conv_expr (&value, code->ext.actual->next->expr);

  gfc_init_block (&block);
  gfc_add_modify (&block, atom.expr,
		  fold_convert (TREE_TYPE (atom.expr), value.expr));
  return gfc_finish_block (&block);
}


static tree
conv_intrinsic_atomic_ref (gfc_code *code)
{
  gfc_se atom, value;
  stmtblock_t block;

  gfc_init_se (&atom, NULL);
  gfc_init_se (&value, NULL);
  gfc_conv_expr (&value, code->ext.actual->expr);
  gfc_conv_expr (&atom, code->ext.actual->next->expr);

  gfc_init_block (&block);
  gfc_add_modify (&block, value.expr,
		  fold_convert (TREE_TYPE (value.expr), atom.expr));
  return gfc_finish_block (&block);
}


static tree
conv_intrinsic_move_alloc (gfc_code *code)
{
  stmtblock_t block;
  gfc_expr *from_expr, *to_expr;
  gfc_expr *to_expr2, *from_expr2 = NULL;
  gfc_se from_se, to_se;
  tree tmp;
  bool coarray;

  gfc_start_block (&block);

  from_expr = code->ext.actual->expr;
  to_expr = code->ext.actual->next->expr;

  gfc_init_se (&from_se, NULL);
  gfc_init_se (&to_se, NULL);

  gcc_assert (from_expr->ts.type != BT_CLASS
	      || to_expr->ts.type == BT_CLASS);
  coarray = gfc_get_corank (from_expr) != 0;

  if (from_expr->rank == 0 && !coarray)
    {
      if (from_expr->ts.type != BT_CLASS)
	from_expr2 = from_expr;
      else
	{
	  from_expr2 = gfc_copy_expr (from_expr);
	  gfc_add_data_component (from_expr2);
	}

      if (to_expr->ts.type != BT_CLASS)
	to_expr2 = to_expr;
      else
	{
	  to_expr2 = gfc_copy_expr (to_expr);
	  gfc_add_data_component (to_expr2);
	}

      from_se.want_pointer = 1;
      to_se.want_pointer = 1;
      gfc_conv_expr (&from_se, from_expr2);
      gfc_conv_expr (&to_se, to_expr2);
      gfc_add_block_to_block (&block, &from_se.pre);
      gfc_add_block_to_block (&block, &to_se.pre);

      /* Deallocate "to".  */
      tmp = gfc_deallocate_scalar_with_status (to_se.expr, NULL_TREE, true,
					       to_expr, to_expr->ts);
      gfc_add_expr_to_block (&block, tmp);

      /* Assign (_data) pointers.  */
      gfc_add_modify_loc (input_location, &block, to_se.expr,
			  fold_convert (TREE_TYPE (to_se.expr), from_se.expr));

      /* Set "from" to NULL.  */
      gfc_add_modify_loc (input_location, &block, from_se.expr,
			  fold_convert (TREE_TYPE (from_se.expr), null_pointer_node));

      gfc_add_block_to_block (&block, &from_se.post);
      gfc_add_block_to_block (&block, &to_se.post);

      /* Set _vptr.  */
      if (to_expr->ts.type == BT_CLASS)
	{
	  gfc_symbol *vtab;

	  gfc_free_expr (to_expr2);
	  gfc_init_se (&to_se, NULL);
	  to_se.want_pointer = 1;
	  gfc_add_vptr_component (to_expr);
	  gfc_conv_expr (&to_se, to_expr);

	  if (from_expr->ts.type == BT_CLASS)
	    {
	      if (UNLIMITED_POLY (from_expr))
		vtab = NULL;
	      else
		{
		  vtab = gfc_find_derived_vtab (from_expr->ts.u.derived);
		  gcc_assert (vtab);
		}

	      gfc_free_expr (from_expr2);
	      gfc_init_se (&from_se, NULL);
	      from_se.want_pointer = 1;
	      gfc_add_vptr_component (from_expr);
	      gfc_conv_expr (&from_se, from_expr);
	      gfc_add_modify_loc (input_location, &block, to_se.expr,
				  fold_convert (TREE_TYPE (to_se.expr),
				  from_se.expr));

              /* Reset _vptr component to declared type.  */
	      if (UNLIMITED_POLY (from_expr))
		gfc_add_modify_loc (input_location, &block, from_se.expr,
				    fold_convert (TREE_TYPE (from_se.expr),
						  null_pointer_node));
	      else
		{
		  tmp = gfc_build_addr_expr (NULL_TREE, gfc_get_symbol_decl (vtab));
		  gfc_add_modify_loc (input_location, &block, from_se.expr,
				      fold_convert (TREE_TYPE (from_se.expr), tmp));
		}
	    }
	  else
	    {
	      if (from_expr->ts.type != BT_DERIVED)
		vtab = gfc_find_intrinsic_vtab (&from_expr->ts);
	      else
		vtab = gfc_find_derived_vtab (from_expr->ts.u.derived);
	      gcc_assert (vtab);
	      tmp = gfc_build_addr_expr (NULL_TREE, gfc_get_symbol_decl (vtab));
	      gfc_add_modify_loc (input_location, &block, to_se.expr,
				  fold_convert (TREE_TYPE (to_se.expr), tmp));
	    }
	}

      return gfc_finish_block (&block);
    }

  /* Update _vptr component.  */
  if (to_expr->ts.type == BT_CLASS)
    {
      gfc_symbol *vtab;

      to_se.want_pointer = 1;
      to_expr2 = gfc_copy_expr (to_expr);
      gfc_add_vptr_component (to_expr2);
      gfc_conv_expr (&to_se, to_expr2);

      if (from_expr->ts.type == BT_CLASS)
	{
	  if (UNLIMITED_POLY (from_expr))
	    vtab = NULL;
	  else
	    {
	      vtab = gfc_find_derived_vtab (from_expr->ts.u.derived);
	      gcc_assert (vtab);
	    }

	  from_se.want_pointer = 1;
	  from_expr2 = gfc_copy_expr (from_expr);
	  gfc_add_vptr_component (from_expr2);
	  gfc_conv_expr (&from_se, from_expr2);
	  gfc_add_modify_loc (input_location, &block, to_se.expr,
			      fold_convert (TREE_TYPE (to_se.expr),
			      from_se.expr));

	  /* Reset _vptr component to declared type.  */
	  if (UNLIMITED_POLY (from_expr))
	    gfc_add_modify_loc (input_location, &block, from_se.expr,
				fold_convert (TREE_TYPE (from_se.expr),
					      null_pointer_node));
	  else
	    {
	      tmp = gfc_build_addr_expr (NULL_TREE, gfc_get_symbol_decl (vtab));
	      gfc_add_modify_loc (input_location, &block, from_se.expr,
				  fold_convert (TREE_TYPE (from_se.expr), tmp));
	    }
	}
      else
	{
	  if (from_expr->ts.type != BT_DERIVED)
	    vtab = gfc_find_intrinsic_vtab (&from_expr->ts);
	  else
	    vtab = gfc_find_derived_vtab (from_expr->ts.u.derived);
	  gcc_assert (vtab);
	  tmp = gfc_build_addr_expr (NULL_TREE, gfc_get_symbol_decl (vtab));
	  gfc_add_modify_loc (input_location, &block, to_se.expr,
			      fold_convert (TREE_TYPE (to_se.expr), tmp));
	}

      gfc_free_expr (to_expr2);
      gfc_init_se (&to_se, NULL);

      if (from_expr->ts.type == BT_CLASS)
	{
	  gfc_free_expr (from_expr2);
	  gfc_init_se (&from_se, NULL);
	}
    }


  /* Deallocate "to".  */
  if (from_expr->rank == 0)
    {
      to_se.want_coarray = 1;
      from_se.want_coarray = 1;
    }
  gfc_conv_expr_descriptor (&to_se, to_expr);
  gfc_conv_expr_descriptor (&from_se, from_expr);

  /* For coarrays, call SYNC ALL if TO is already deallocated as MOVE_ALLOC
     is an image control "statement", cf. IR F08/0040 in 12-006A.  */
  if (coarray && gfc_option.coarray == GFC_FCOARRAY_LIB)
    {
      tree cond;

      tmp = gfc_deallocate_with_status (to_se.expr, NULL_TREE, NULL_TREE,
					NULL_TREE, NULL_TREE, true, to_expr,
					true);
      gfc_add_expr_to_block (&block, tmp);

      tmp = gfc_conv_descriptor_data_get (to_se.expr);
      cond = fold_build2_loc (input_location, EQ_EXPR,
			      boolean_type_node, tmp,
			      fold_convert (TREE_TYPE (tmp),
					    null_pointer_node));
      tmp = build_call_expr_loc (input_location, gfor_fndecl_caf_sync_all,
				 3, null_pointer_node, null_pointer_node,
				 build_int_cst (integer_type_node, 0));

      tmp = fold_build3_loc (input_location, COND_EXPR, void_type_node, cond,
			     tmp, build_empty_stmt (input_location));
      gfc_add_expr_to_block (&block, tmp);
    }
  else
    {
      tmp = gfc_conv_descriptor_data_get (to_se.expr);
      tmp = gfc_deallocate_with_status (tmp, NULL_TREE, NULL_TREE, NULL_TREE,
					NULL_TREE, true, to_expr, false);
      gfc_add_expr_to_block (&block, tmp);
    }

  /* Move the pointer and update the array descriptor data.  */
  gfc_add_modify_loc (input_location, &block, to_se.expr, from_se.expr);

  /* Set "from" to NULL.  */
  tmp = gfc_conv_descriptor_data_get (from_se.expr);
  gfc_add_modify_loc (input_location, &block, tmp,
		      fold_convert (TREE_TYPE (tmp), null_pointer_node));

  return gfc_finish_block (&block);
}


tree
gfc_conv_intrinsic_subroutine (gfc_code *code)
{
  tree res;

  gcc_assert (code->resolved_isym);

  switch (code->resolved_isym->id)
    {
    case GFC_ISYM_MOVE_ALLOC:
      res = conv_intrinsic_move_alloc (code);
      break;

    case GFC_ISYM_ATOMIC_DEF:
      res = conv_intrinsic_atomic_def (code);
      break;

    case GFC_ISYM_ATOMIC_REF:
      res = conv_intrinsic_atomic_ref (code);
      break;

    default:
      res = NULL_TREE;
      break;
    }

  return res;
}

#include "gt-fortran-trans-intrinsic.h"
