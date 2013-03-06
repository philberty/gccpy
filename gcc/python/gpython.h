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

#ifndef __GCC_GPYTHON_H__
#define __GCC_GPYTHON_H__

#include "config.h"
#include "system.h"
#include "ansidecl.h"
#include "coretypes.h"
#include "tm.h"
#include "opts.h"
#include "tree.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "gimple.h"
#include "toplev.h"
#include "debug.h"
#include "options.h"
#include "flags.h"
#include "convert.h"
#include "diagnostic-core.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "target.h"
#include "cgraph.h"

#include <gmp.h>
#include <mpfr.h>
#include "vec.h"
#include "hashtab.h"

/* gccpy headers needed ... */
#include "dot-dot.h"
#include "dot-hashtab.h"
#include "dot-tree.h"
#include "gpy-runtime.h"

extern bool GPY_dump_dot;
extern char * GPY_current_module_name;

#if !defined(YYLTYPE)
typedef struct gpy_location {
  int line;
  int column;
} gpy_location_t;
typedef gpy_location_t YYLTYPE;
#define YYLTYPE YYLTYPE
#endif

/* important langhook prototypes */
extern void gpy_set_prefix (const char *);
extern void gpy_preserve_from_gc (tree);
extern void gpy_add_search_path (const char *);
extern void gpy_parse_input_files (const char **, unsigned int);
extern tree gpy_type_for_size (unsigned int, int);
extern tree gpy_type_for_mode (enum machine_mode, int);

extern int gpy_do_compile (const char *);
extern void __gpy_debug__ (const char *, unsigned int, const char *, ...)
  __attribute__ ((format (printf, 3, 4)));

#define debug(...)					\
  __gpy_debug__(__FILE__, __LINE__, __VA_ARGS__);

#endif /* __GCC_GPYTHON_H__ */
