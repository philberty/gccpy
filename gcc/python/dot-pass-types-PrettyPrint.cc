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

static void dot_pass_dump_type (FILE *, tree);

static
void dot_pass_dump_type (FILE * fd, tree decl)
{
  tree name = TYPE_NAME (decl);
  tree field;

  fprintf (fd, "struct %s {\n", IDENTIFIER_POINTER (name));
  for (field = TYPE_FIELDS (decl); field != NULL_TREE;
       field = DECL_CHAIN (field))
    {
      tree fname = DECL_NAME (field);
      fprintf (fd, "  %s %s;\n",
	       "object",
	       IDENTIFIER_POINTER (fname));
    }
  fprintf (fd, "};\n\n");
}

void dot_pass_pretty_PrintTypes (vec<tree,va_gc> * decls)
{
  if (GPY_OPT_dump_dot)
    {
      FILE * fd = fopen ("gccpy-types.dot", "w");
      gcc_assert (fd);

      int i;
      tree it;
      for (i = 0; decls->iterate (i, &it); ++i)
	dot_pass_dump_type (fd, it);

      fclose (fd);
    }
}
