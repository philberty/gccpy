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

/* for object read/write */
#include "simple-object.h"
#include "output.h"
#include "target.h"
#include "common/common-target.h"

#ifndef PY_EXPORT_SEGMENT_NAME
#define PY_EXPORT_SEGMENT_NAME "__GNU_PY"
#endif

/* The section name we use when reading and writing export data.  */

#ifndef PY_EXPORT_SECTION_NAME
#define PY_EXPORT_SECTION_NAME ".gpy_export"
#endif

void gpy_write_export_data (const char * bytes, unsigned int size)
{
  static section * sec;

  if (sec == NULL)
    {
      gcc_assert (targetm_common.have_named_sections);
      sec = get_section (PY_EXPORT_SECTION_NAME, SECTION_DEBUG, NULL);
    }
  switch_to_section (sec);
  assemble_string (bytes, size);
}
