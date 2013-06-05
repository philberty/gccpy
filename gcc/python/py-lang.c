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

char * GPY_current_module_name = NULL;
bool GPY_dump_dot = false;

/* Language-dependent contents of a type.  */
struct GTY(()) lang_type {
  char dummy;
} ;
/* Language-dependent contents of a decl.  */
struct GTY(()) lang_decl {
  char dummy;
} ;
/* Language-dependent contents of an identifier.  This must include a
   tree_identifier.
*/
struct GTY(()) lang_identifier {
  struct tree_identifier common;
} ;

/* The resulting tree type.  */
union GTY((desc ("TREE_CODE (&%h.generic) == IDENTIFIER_NODE"),
           chain_next ("CODE_CONTAINS_STRUCT (TREE_CODE (&%h.generic), TS_COMMON) ? ((union lang_tree_node *) TREE_CHAIN (&%h.generic)) : NULL")))
lang_tree_node
{
  union tree_node GTY((tag ("0"),
                       desc ("tree_node_structure (&%h)"))) generic;
  struct lang_identifier GTY((tag ("1"))) identifier;
};

/* We don't use language_function.  */
struct GTY(()) language_function {
  int dummy;
};

/* Language hooks.  */
static
bool gpy_langhook_init (void)
{
  build_common_tree_nodes (false, false);
  // build_common_builtin_nodes ();

  // shouldnt have to do this...
  void_list_node = build_tree_list (NULL_TREE, void_type_node);

  // init some internal gccpy types
  gpy_dot_types_init ();

  /* The default precision for floating point numbers.  This is used
     for floating point constants with abstract type.  This may
     eventually be controllable by a command line option.  */
  mpfr_set_default_prec (128);
  // for exceptions
  using_eh_for_cleanups ();

  return true;
}

/* Initialize before parsing options.  */
static void
gpy_langhook_init_options (unsigned int decoded_options_count ATTRIBUTE_UNUSED,
                           struct cl_decoded_option *decoded_options ATTRIBUTE_UNUSED)
{
  return;
}

/* Handle gpy specific options.  Return 0 if we didn't do anything.  */
static bool
gpy_langhook_handle_option (size_t scode, const char *arg ATTRIBUTE_UNUSED,
			    int value ATTRIBUTE_UNUSED, int kind ATTRIBUTE_UNUSED,
                            location_t l ATTRIBUTE_UNUSED,
			    const struct cl_option_handlers * handlers ATTRIBUTE_UNUSED)
{
  enum opt_code code = (enum opt_code) scode;
  int retval = 1;

  switch (code)
    {
      /* ignore options for now... */

    default:
      break;
    }

  return retval;
}

/* Run after parsing options.  */
static
bool gpy_langhook_post_options (const char **pfilename
				ATTRIBUTE_UNUSED)
{
  if (flag_excess_precision_cmdline == EXCESS_PRECISION_DEFAULT)
    flag_excess_precision_cmdline = EXCESS_PRECISION_STANDARD;

  /* Returning false means that the backend should be used.  */
  return false;
}

static
void gpy_langhook_parse_file (void)
{
  /* Add the initial line map */
  linemap_add (line_table, LC_ENTER, 0, in_fnames[0], 1);

  unsigned int idx;
  for (idx = 0; idx < num_in_fnames; ++idx)
    {
      const char * in = in_fnames[idx];
      /* this will break when handling num_in_fnames > 1
	 would be ideal to get fname base name and use this
	 as prefix for identifiers within input module.
       */

      // Get rid of the .py at the end..
      size_t len = strlen (in_fnames [idx]);
      GPY_current_module_name = xstrdup (in_fnames [idx]);
      GPY_current_module_name[len - 3] = '\0';

      // lets now finally parse this..
      gpy_do_compile (in);
    }
}

static tree
gpy_langhook_type_for_size (unsigned int bits ATTRIBUTE_UNUSED,
                            int unsignedp ATTRIBUTE_UNUSED)
{
  return NULL;
}

static
tree gpy_langhook_type_for_mode (enum machine_mode mode ATTRIBUTE_UNUSED,
                                 int unsignedp ATTRIBUTE_UNUSED)
{
  return NULL_TREE;
}

/* Record a builtin function.  We just ignore builtin functions.  */
static
tree gpy_langhook_builtin_function (tree decl ATTRIBUTE_UNUSED)
{
  return decl;
}

static
bool gpy_langhook_global_bindings_p (void)
{
  return current_function_decl == NULL_TREE;
}

/* Ideally we should use these (push/get decls) langhooks
   but its simplier for  gccpy to use those defined in
   dot-pass-manager.c due to the internal IL called dot.
*/
static
tree gpy_langhook_pushdecl (tree decl ATTRIBUTE_UNUSED)
{
  gcc_unreachable ();
  return NULL;
}

static
tree gpy_langhook_getdecls (void)
{
  gcc_unreachable ();
  return NULL;
}

/* Write out globals.  */
static
void gpy_langhook_write_globals (void)
{
  // pass off to middle end function basically.
  dot_pass_manager_WriteGlobals ();
}

static
unsigned int gpy_option_lang_mask (void)
{
  return CL_Python;
}

static int
gpy_langhook_gimplify_expr (tree *expr_p ATTRIBUTE_UNUSED,
                            gimple_seq *pre_p ATTRIBUTE_UNUSED,
                            gimple_seq *post_p ATTRIBUTE_UNUSED)
{
  if (TREE_CODE (*expr_p) == CALL_EXPR
      && CALL_EXPR_STATIC_CHAIN (*expr_p) != NULL_TREE)
    gimplify_expr (&CALL_EXPR_STATIC_CHAIN (*expr_p), pre_p, post_p,
		   is_gimple_val, fb_rvalue);
  /* Often useful to use debug_tree here to see whats going on because
     ever gimplication calls this. */
  // debug_tree (*expr_p)
  return GS_UNHANDLED;
}

/* Functions called directly by the generic backend.  */
tree convert (tree type, tree expr)
{
  if (type == error_mark_node
      || expr == error_mark_node
      || TREE_TYPE (expr) == error_mark_node)
    return error_mark_node;

  if (type == TREE_TYPE (expr))
    return expr;

  if (TYPE_MAIN_VARIANT (type) == TYPE_MAIN_VARIANT (TREE_TYPE (expr)))
    return fold_convert (type, expr);

  switch (TREE_CODE (type))
    {
    case VOID_TYPE:
    case BOOLEAN_TYPE:
      return fold_convert (type, expr);
    case INTEGER_TYPE:
      return fold (convert_to_integer (type, expr));
    case POINTER_TYPE:
      return fold (convert_to_pointer (type, expr));
    case REAL_TYPE:
      return fold (convert_to_real (type, expr));
    case COMPLEX_TYPE:
      return fold (convert_to_complex (type, expr));
    default:
      break;
    }

  gcc_unreachable ();
}

static GTY(()) tree gpy_gc_root;
void gpy_preserve_from_gc (tree t)
{
  gpy_gc_root = tree_cons (NULL_TREE, t, gpy_gc_root);
}

/* Useful logging kind of ... */
void __gpy_debug__ (const char * file, unsigned int lineno,
		    const char * fmt, ...)
{
  va_list args;
  fprintf (stderr, "DEBUG: <%s:%i> ", file, lineno);
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
}

/* The language hooks data structure. This is the main interface between the GCC front-end
 * and the GCC middle-end/back-end. A list of language hooks could be found in
 * <gcc>/langhooks.h
 */
#undef LANG_HOOKS_NAME
#undef LANG_HOOKS_INIT
#undef LANG_HOOKS_INIT_OPTIONS
#undef LANG_HOOKS_HANDLE_OPTION
#undef LANG_HOOKS_POST_OPTIONS
#undef LANG_HOOKS_PARSE_FILE
#undef LANG_HOOKS_TYPE_FOR_MODE
#undef LANG_HOOKS_TYPE_FOR_SIZE
#undef LANG_HOOKS_BUILTIN_FUNCTION
#undef LANG_HOOKS_GLOBAL_BINDINGS_P
#undef LANG_HOOKS_PUSHDECL
#undef LANG_HOOKS_GETDECLS
#undef LANG_HOOKS_WRITE_GLOBALS
#undef LANG_HOOKS_GIMPLIFY_EXPR
#undef LANG_HOOKS_GIMPLIFY_EXPR
#undef LANG_HOOKS_OPTION_LANG_MASK

#define LANG_HOOKS_NAME                 "GNU Python"
#define LANG_HOOKS_INIT                 gpy_langhook_init
#define LANG_HOOKS_INIT_OPTIONS         gpy_langhook_init_options
#define LANG_HOOKS_HANDLE_OPTION        gpy_langhook_handle_option
#define LANG_HOOKS_POST_OPTIONS         gpy_langhook_post_options
#define LANG_HOOKS_PARSE_FILE           gpy_langhook_parse_file
#define LANG_HOOKS_TYPE_FOR_MODE        gpy_langhook_type_for_mode
#define LANG_HOOKS_TYPE_FOR_SIZE        gpy_langhook_type_for_size
#define LANG_HOOKS_BUILTIN_FUNCTION     gpy_langhook_builtin_function
#define LANG_HOOKS_GLOBAL_BINDINGS_P    gpy_langhook_global_bindings_p
#define LANG_HOOKS_PUSHDECL             gpy_langhook_pushdecl
#define LANG_HOOKS_GETDECLS             gpy_langhook_getdecls
#define LANG_HOOKS_WRITE_GLOBALS        gpy_langhook_write_globals
#define LANG_HOOKS_GIMPLIFY_EXPR        gpy_langhook_gimplify_expr
#define LANG_HOOKS_OPTION_LANG_MASK     gpy_option_lang_mask

struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;

#include "gt-python-py-lang.h"
#include "gtype-python.h"
