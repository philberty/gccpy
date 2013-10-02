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

#include <gpython/gpython.h>

#define PYTHON_VERSION "2.4"

static bool __MOD_INIT_CHK = false;
static int __MOD_OFFSET = 0;

static const char *mod_items[] = {
  "version",
  NULL
};

void gpy_builtin_sys_init (void)
{
  // if already initilized..
  if (__MOD_INIT_CHK)
    return;

  __MOD_OFFSET = gpy_rr_extendRRStack (nitems (mod_items - 1),
				       "sys", (char **) mod_items);
  gpy_object_t ** esp =  __GPY_RR_STACK_PTR + __MOD_OFFSET;  

  // sys.version
  char * buf = (char *) alloca (128);
  snprintf (buf, 128, "%s - %s part of GCC Python Compiler\n"
	    "[%s - Targeting python %s]\n",
	    PACKAGE_NAME, PACKAGE_VERSION, PACKAGE_BUGREPORT, PYTHON_VERSION);

  gpy_object_t * vstr = gpy_rr_fold_string (buf);
  esp[0] = vstr;

  __MOD_INIT_CHK = true;
}
