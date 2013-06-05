%{
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
#include "y.gpy.h"

extern int IMPparse (void);
%}

%option noyywrap
%option yylineno

DIGIT        [0-9]
ID           [_a-zA-Z][a-zA_Z0-9_$]*
QSTRING      \"[^\"\n]*[\"\n]
NEWLINES     (\r\n|\r|\n)
FORMFEED     [\f]
WHITESPACE   [ \t]

%%

MODULE       { return MODULE; }
HAS_MAIN     { return HAS_MAIN; }
ENTRY        { return ENTRY; }
True         { return ITRUE; }
False        { return IFALSE; }

\{           { return '{'; }
\}           { return '}'; }

{QSTRING}               {
  IMPlval.string= xstrdup (IMPtext + 1);
  if (IMPlval.string [IMPleng - 2] != '\"') {
    error ("Un-termintated character string!\n");
  }
  else {
    IMPlval.string [IMPleng - 2] = '\0';
  }
  return STRING;
}

\t       { /* Can be ignored! */ }
\n       { /* Can be ignored! */ }
.        { /* Can be ignored! */ }

%%

void gpy_import_read (const char * in)
{
  FILE * fd = fopen (in, "rb");
  if (!fd)
    {
      error ("Unable to open %s!\n", in);
      return;
    }

  IMPin = fd;
  IMPparse ();

  IMPlex_destroy ();
  fclose (fd);
}
