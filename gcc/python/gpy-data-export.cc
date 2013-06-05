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

#include "simple-object.h"
#include "rtl.h"
#include "tm_p.h"
#include "intl.h"
#include "output.h"	/* for assemble_string */
#include "target.h"
#include "common/common-target.h"

#define GPY_EXPORT_INFO ".export.gpyx"

static bool first = true;
static gpy_hash_tab_t imports;

void gpy_writeExport (const char * fname, bool isMain,
		      const char * modName,
		      const char * modInitilizer)
{
  debug ("Trying to write export data to %s\n", fname);

  FILE * fd = fopen (fname, "w");
  if (!fd)
    {
      error ("Unable to open %s for write of export data\n", fname);
      return;
    }

  const char * isMainStr;
  if (isMain)
    isMainStr = "True";
  else
    isMainStr = "False";

  fprintf (fd, "MODULE \"%s\" {\n", modName);
  fprintf (fd, "\tHAS_MAIN %s\n", isMainStr);
  fprintf (fd, "\tENTRY \"%s\"\n}\n", modInitilizer);

  fclose (fd);
}

void gpy_pushExportData (struct gpy_dataExport * exp)
{
  gpy_hashval_t h = gpy_dd_hash_string (exp->module);
  void ** slot = gpy_dd_hash_insert (h, (void *) exp, &imports);
  gcc_assert (slot == NULL);
}

struct gpy_dataExport * gpy_readExportData (const char * module)
{
  if (first)
    {
      gpy_dd_hash_init_table (&imports);
      first = false;
    }

  debug ("Trying to lookup module data on %s\n", module);

  char * buf = (char *) alloca (128);
  strcpy (buf, module);
  strcat (buf, GPY_EXPORT_INFO);

  // iterate over the SEARCH_PATH to find the export data..
  // ... TODO
  debug ("Trying to open <./%s>\n", buf);

  gpy_import_read (buf);
  struct gpy_dataExport * ret = NULL;

  gpy_hashval_t h = gpy_dd_hash_string (module);
  gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (&imports, h);
  if (e)
    if (e->data)
      ret = (struct gpy_dataExport *) e->data;

  return ret;
}
