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

static vec<gpydot,va_gc> * gpy_decls;
typedef vec<gpydot,va_gc> * (*dot_pass)(vec<gpydot,va_gc> *);
static dot_pass dot_pass_mngr[] =
{
  &dot_pass_check1,       /* sanity checks */
  &dot_pass_const_fold,   /* Constant folding */
  &dot_pass_translate,    /* translate/fix the python code for lowering */
  &dot_pass_PrettyPrint,  /* pretty print if -fdump-dot */
  /*
    Potential to add in more passes here ... just hook the function pointer in here
    and it shall be called and you gain access to the current state of the dot AST.
   */
  NULL                         /* sentinal */
};

/* silly helper function... */
char * dot_pass_concat (const char * s1, const char * s2)
{
  size_t s1len = strlen (s1);
  size_t s2len = strlen (s2);
  size_t tlen = s1len + s2len;

  char *buffer = new char[tlen + 3];
  char * p;
  for (p = buffer; *s1 != '\0'; ++s1)
    {
      *p = *s1;
      ++p;
    }
  *p = '.';
  p++;
  for (; *s2 != '\0'; ++s2)
    {
      *p = *s2;
      ++p;
    }
  *p = '\0';
  return buffer;
}

/* Pushes each decl from the parser onto the current translation unit */
void dot_pass_manager_ProcessDecl (gpy_dot_tree_t * const dot)
{
  /* Push the declaration! */
  vec_safe_push (gpy_decls, dot);
}

/* Function to run over the pass manager hooks and
   generate the generic code to pass to gcc middle-end
 */
void dot_pass_manager_WriteGlobals (void)
{
  dot_pass *p = NULL;
  vec<gpydot,va_gc> * dot_decls = gpy_decls;

  /* walk the passes */
  for (p = dot_pass_mngr; *p != NULL; ++p)
    dot_decls = (*p)(dot_decls);

  /* generate the types from the passed decls */
  vec<tree,va_gc> * module_types = dot_pass_GenTypes (dot_decls);
  dot_pass_pretty_PrintTypes (module_types);

  /* lower the decls from DOT -> GENERIC */
  vec<tree,va_gc> * globals =  dot_pass_genericify (module_types, dot_decls);

  int global_vec_len = vec_safe_length (globals);
  tree * global_vec = new tree[global_vec_len];
  tree itx = NULL_TREE;
  int idx, idy = 0;
  /*
     Lets make sure to dump the Translation Unit this isn't that
     useful to read over but can help to make sure certain tree's
     are being generated...

     We also fill up the vector of tree's to be passed to the middle-end
   */
  FILE * tu_stream = dump_begin (TDI_tu, NULL);
  for (idx = 0; vec_safe_iterate (globals, idx, &itx); ++idx)
    {
      if (tu_stream)
	dump_node (itx, 0, tu_stream);
      global_vec [idy] = itx;
      idy++;
    }
  if (tu_stream)
    dump_end(TDI_tu, tu_stream);

  /* Passing control to GCC middle-end */
  wrapup_global_declarations (global_vec, global_vec_len);
  finalize_compilation_unit ();
  check_global_declarations (global_vec, global_vec_len);
  emit_debug_global_declarations (global_vec, global_vec_len);

  delete [] global_vec;
}
