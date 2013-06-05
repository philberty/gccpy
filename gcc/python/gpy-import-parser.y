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

extern int IMPlineno;
extern int IMPlex (void);
extern void IMPerror (const char *);
%}

%union {
  char * string;
  bool bval;
  struct gpy_dataExport * exp;
}

%debug

%error-verbose
%start declarations

%token MODULE "MODULE"
%token HAS_MAIN "HAS_MAIN"
%token ENTRY "ENTRY"
%token ITRUE "True"
%token IFALSE "False"

%token<string> STRING "String"

%type<exp> module_decl
%type<exp> module_def
%type<bval> has_main
%type<string> entry

%%

declarations: module_decl
            { gpy_pushExportData ($1); }
            ;

module_decl: MODULE STRING '{' module_def '}'
           {
             $4->module = xstrdup ($2);
             $$ = $4;
           }
           ;

module_def: has_main entry
          {
            struct gpy_dataExport * val = (struct gpy_dataExport *)
	      xmalloc (sizeof (struct gpy_dataExport));
	    val->main = $1;
	    val->entry = $2;
	    $$ = val;
          }
          ;

has_main: HAS_MAIN ITRUE
        { $$ = true; }
        | HAS_MAIN IFALSE
        { $$ = false; }
        ;

entry: ENTRY STRING
     { $$ = xstrdup ($2); }
     ;

%%

void IMPerror (const char * msg)
{
  fatal_error ("%s at line %i\n", msg, IMPlineno);
}
