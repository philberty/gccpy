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

/*
  This pass should pass over the IL at its most basic form
  stright from the parser and preform sanity checks to make
  sure everything looks correct before other pass's
*/
VEC(gpydot,gc) * dot_pass_check1 (VEC(gpydot,gc) * decls)
{
  return decls;
}
