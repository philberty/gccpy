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

static const char * builtins[] = {
  "sys",
  NULL
};

int gpy_checkBuiltin (const char * import)
{
  int found = -1, c = 0;
  const char ** i;
  for (i = builtins; *i != NULL; ++i)
    {
      if (!strcmp (*i, import))
	{
	  found = c + 1;
	  break;
	}
      c++;
    }
  return found;
}
