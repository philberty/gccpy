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

static void dot_pass_dump_IL (VEC(gpydot,gc) *, const char *);
static void dot_pass_dump_node (FILE *, gpy_dot_tree_t *, int);
static void dot_pass_dump_method (FILE *, gpy_dot_tree_t *, int);
static void dot_pass_dump_class (FILE *, gpy_dot_tree_t *, int);
static void dot_pass_dump_expr (FILE *, gpy_dot_tree_t *);

static
void dot_pass_dump_class (FILE * fd, gpy_dot_tree_t * node,
			  int indents)
{
  int i;
  const char * cid = DOT_IDENTIFIER_POINTER (DOT_FIELD (node));
  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");
  fprintf (fd, "class %s {\n", cid);

  gpy_dot_tree_t * suite = DOT_lhs_TT (node);
  do {
    dot_pass_dump_node (fd, suite, indents + 1);
    fprintf (fd, "\n");
  }
  while ((suite = DOT_CHAIN (suite)));

  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");
  fprintf (fd, "}\n");
}

static
void dot_pass_dump_method (FILE * fd, gpy_dot_tree_t * node,
			   int indents)
{
  int i;
  const char * method_id = DOT_IDENTIFIER_POINTER (DOT_FIELD (node));
  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");
  fprintf (fd, "def %s (", method_id);

  gpy_dot_tree_t * pnode = DOT_lhs_TT (node);
  gpy_dot_tree_t * p;
  for (p = pnode; p != NULL_DOT; p = DOT_CHAIN (p))
    {
      const char * pnodeid = DOT_IDENTIFIER_POINTER (p);
      fprintf (fd, "%s", pnodeid);
      if (DOT_CHAIN (p))
	fprintf (fd, ", ");
    }
  fprintf (fd, ") {\n");

  gpy_dot_tree_t * suite = DOT_rhs_TT (node);
  do {
    dot_pass_dump_node (fd, suite, indents + 1);
    fprintf (fd, "\n");
  }
  while ((suite = DOT_CHAIN (suite)));

  for (i = 0; i < indents; ++i)
    fprintf (fd, "  ");
  fprintf (fd, "}\n");
}

static
void dot_pass_dumpExprNode (FILE * fd, gpy_dot_tree_t * node)
{
  /* print expr tree ... */
  switch (DOT_TYPE (node))
    {
    case D_PRIMITIVE:
      /* all are integers for now... */
      fprintf (fd, "%i", DOT_lhs_TC (node)->o.integer);
      break;

    case D_IDENTIFIER:
      fprintf (fd, "%s", DOT_IDENTIFIER_POINTER (node));
      break;

    case D_T_LIST:
      {
	fprintf (fd, "[ ");
	gpy_dot_tree_t * args = DOT_lhs_TT (node);
	gpy_dot_tree_t * it;
	for (it = args; it != NULL_DOT;
	     it = DOT_CHAIN (it))
	  {
	    dot_pass_dumpExprNode (fd, it);
	    if (DOT_CHAIN (it) != NULL_DOT)
	      fprintf (fd, ", ");
	  }
	fprintf (fd, " ]");
      }
      break;

    case D_CALL_EXPR:
      {
	gpy_dot_tree_t * id = DOT_lhs_TT (node);
	gpy_dot_tree_t * parms = NULL;

	dot_pass_dump_expr (fd, id);

	fprintf (fd, " (");
	for (parms = DOT_rhs_TT (node); parms != NULL_DOT;
	     parms = DOT_CHAIN (parms))
	  {
	    dot_pass_dump_expr (fd, parms);
	    if (DOT_CHAIN (parms))
	      fprintf (fd, ", ");
	  }
	fprintf (fd, ")");
      }
      break;

    case D_ATTRIB_REF:
      {
	gpy_dot_tree_t * lhs = DOT_lhs_TT (node);
	gpy_dot_tree_t * rhs = DOT_rhs_TT (node);
	dot_pass_dumpExprNode (fd, lhs);
	fprintf (fd, ".");
	dot_pass_dumpExprNode (fd, rhs);
      }
      break;

    case D_STRUCT_CONDITIONAL:
      break;

    default:
      dot_pass_dump_expr (fd, node);
      break;
    }
}

static
void dot_pass_dump_expr (FILE * fd, gpy_dot_tree_t * node)
{
  switch (DOT_TYPE (node))
    {
    case D_PRIMITIVE:
    case D_IDENTIFIER:
    case D_ATTRIB_REF:
    case D_CALL_EXPR:
      dot_pass_dumpExprNode (fd, node);
      break;

    default:
      {
	/* print expr tree ... */
	gpy_dot_tree_t * lhs = DOT_lhs_TT (node);
	gpy_dot_tree_t * rhs = DOT_rhs_TT (node);

	dot_pass_dumpExprNode (fd, lhs);
	switch (DOT_TYPE (node))
	  {
	  case D_MODIFY_EXPR:
	    fprintf (fd, " = ");
	    break;

	  case D_ADD_EXPR:
	    fprintf (fd, " + ");
	    break;

	  default:
	    fatal_error ("unhandled dump!\n");
	    break;
	  }
	dot_pass_dumpExprNode (fd, rhs);
      }
      break;
    }
}

static
void dot_pass_dump_node (FILE * fd, gpy_dot_tree_t * node,
			 int indents)
{
   if (DOT_T_FIELD (node) ==  D_D_EXPR)
     {
       int i;
       for (i = 0; i < indents; ++i)
	 fprintf (fd, "  ");
       dot_pass_dump_expr (fd, node);
     }
  else
    {
      switch (DOT_TYPE (node))
	{
	case D_PRINT_STMT:
	  {
	    int i;
	    for (i = 0; i < indents; ++i)
	      fprintf (fd, "  ");
	    gpy_dot_tree_t * args = NULL;
	    fprintf (fd, "print ");
	    fprintf (fd, "(");
	    for (args = DOT_lhs_TT (node); args != NULL_DOT;
		 args = DOT_CHAIN (args))
	      {
		dot_pass_dump_expr (fd, args);
		if (DOT_CHAIN (args))
		  fprintf (fd, ", ");
	      }
	    fprintf (fd, ")");
	  }
	  break;

	case D_STRUCT_METHOD:
	  dot_pass_dump_method (fd, node, indents);
	  break;

	case D_STRUCT_CLASS:
	  dot_pass_dump_class (fd, node, indents);
	  break;

	default:
	  // just ignore ...
	  break;
	}
    }
}

static
void dot_pass_dump_IL (VEC(gpydot,gc) * decls, const char * outfile)
{
  FILE * fd = fopen (outfile, "w");
  if (!fd)
    {
      error ("unable to open <%s> for writeable!\n", outfile);
      return;
    }
  int idx;
  gpy_dot_tree_t * idtx = NULL_DOT;
  for (idx = 0; VEC_iterate (gpydot, decls, idx, idtx); ++idx)
    {
      dot_pass_dump_node (fd, idtx, 0);
      fprintf (fd, "\n");
    }
  fclose (fd);
}

/*
  A Pretty-printer to dump out the IL if -fpy-dump-dot was passed
*/
VEC(gpydot,gc) * dot_pass_PrettyPrint (VEC(gpydot,gc) * decls)
{
  if (GPY_OPT_dump_dot)
    dot_pass_dump_IL (decls, "gccpy-il.dot");

  return decls;
}
