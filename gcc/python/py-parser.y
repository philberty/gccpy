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

/* Grammar largely bassed on
 * - http://docs.python.org/release/2.5.2/ref/grammar.txt
 */

#include "gpython.h"

#if !defined(YYLLOC_DEFAULT)
# define YYLLOC_DEFAULT(Current, Rhs, N)                           \
  do								   \
    if (N)							   \
      {								   \
	(Current).line = YYRHSLOC(Rhs, 1).line;			   \
	(Current).column = YYRHSLOC(Rhs, 1).column;		   \
      }								   \
    else							   \
      {								   \
	(Current).line = YYRHSLOC(Rhs, 0).line;			   \
	(Current).column = YYRHSLOC(Rhs, 0).column;		   \
      }								   \
  while (0)
#endif

#define YYDEBUG 1

static vec<gpydot, va_gc> * gpy_symbol_stack;
extern int yylineno;

extern int yylex (void);
extern void yyerror (const char *);
%}

%union {
  char * string;
  long int integer;
  gpy_dot_tree_t * symbol;
  opcode_t opcode;
}

%debug
%locations

%error-verbose
%start declarations

%token CLASS "class"
%token DEF "def"
%token BREAK "break"
%token CONTINUE "continue"
%token RETURN "return"
%token FOR "for"
%token WHILE "while"
%token IN "in"
%token PRINT "print"

%token EXCEPT "except"
%token FINALLY "finally"
%token TRY "try"

%token AS "as"
%token ASSERT "assert"
%token DEL "del"
%token EXEC "exec"
%token FROM "from"
%token GLOBAL "global"
%token IMPORT "import"
%token IS "is"
%token LAMBDA "lambda"
%token PASS "pass"
%token RAISE "raise"
%token WITH "with"
%token YIELD "yield"

%token IF "if"
%token ELIF "elif"
%token ELSE "else"

%token OR "or"
%token AND "and"
%token NOT "not"

%token V_TRUE "True"
%token V_FALSE "False"

%token NEWLINE
%token INDENT
%token DEDENT

%token EQUAL_EQUAL
%token NOT_EQUAL
%token LESS
%token GREATER
%token LESS_EQUAL
%token GREATER_EQUAL

%token NONE
%token<string> IDENTIFIER
%token<string> STRING
%token<integer> INTEGER
%token<decimal> DOUBLE

%type<symbol> statement
%type<symbol> compound_stmt
%type<symbol> stmt_list
%type<symbol> simple_stmt
%type<symbol> expression_stmt
%type<symbol> target_list
%type<symbol> target
%type<symbol> funcdef
%type<symbol> classdef
%type<symbol> suite
%type<symbol> suite_statement_list
%type<symbol> indent_stmt
%type<symbol> literal
%type<symbol> atom
%type<symbol> primary
%type<symbol> expression
%type<symbol> call
%type<symbol> decl
%type<symbol> argument_list
%type<symbol> argument_list_stmt
%type<symbol> parameter_list
%type<symbol> parameter_list_stmt
%type<symbol> print_stmt
%type<symbol> attributeref
%type<symbol> ident
%type<symbol> while_stmt
%type<symbol> for_stmt
%type<symbol> ifblock
%type<symbol> elifstmt
%type<symbol> elsestmt
%type<symbol> elif_list
%type<symbol> elifblock
%type<symbol> if_stmt
%type<symbol> elseblock
%type<symbol> list_display
%type<symbol> enclosure
%type<symbol> return_stmt
%type<symbol> import_stmt
%type<symbol> slicing
%type<symbol> simple_slicing

%type<symbol> funcname
%type<symbol> classname

%left '='
%left '-' '+'
%left '*' '/'
%left EQUAL_EQUAL
%left LESS LESS_EQUAL
%left GREATER GREATER_EQUAL
%right '^'
%nonassoc UMINUS

%%

declarations: /* epsilon */
            | declarations decl
            {
	      if ($2 != NULL)
		dot_pass_manager_ProcessDecl ($2);
	    }
            ;

decl: NEWLINE
    { $$ = NULL; }
    | statement
    ;

while_stmt: WHILE expression ':' suite
          { $$ = dot_build_decl2 (D_STRUCT_WHILE, $2, $4); }

ifblock: IF expression ':' suite
       { $$ = dot_build_decl2 (D_STRUCT_IF, $2, $4); }

elifstmt: ELIF expression ':' suite
        { $$ = dot_build_decl2 (D_STRUCT_ELIF, $2, $4); }

elsestmt: ELSE ':' suite
        { $$ = dot_build_decl1 (D_STRUCT_ELSE, $3); }
        ;

elseblock:
         { $$ = NULL; }
         | elsestmt
         ;

elif_list: elif_list elifstmt
         {
	   DOT_CHAIN($1) = $2;
	   $$ = $2;
	 }
         | elifstmt
         {
	   vec_safe_push (gpy_symbol_stack, $1);
	   $$ = $1;
	 }
         ;

elifblock:
         { $$ = NULL; }
         | elif_list
         { $$ = gpy_symbol_stack->pop (); }
         ;

if_stmt: ifblock elifblock elseblock
       { $$ = dot_build_conditional_struct ($1, $2, $3); }
       ;

compound_stmt: funcdef
             | classdef
             | while_stmt
             | if_stmt
             | for_stmt
             ;

for_stmt: FOR ident IN expression ':' suite
        { $$ = dot_build_for ($2, $4, $6); }
        ;

classdef: CLASS classname ':' suite
        {
	  gpy_dot_tree_t *dot = dot_build_class_decl ($2, $4);
	  $$ = dot;
	}
        ;

classname: IDENTIFIER
         { $$ = dot_build_identifier ($1); }
         ;

funcname: IDENTIFIER
        { $$ = dot_build_identifier ($1); }
        ;

parameter_list_stmt:
                   { $$ = NULL; }
                   | parameter_list
                   { $$ = gpy_symbol_stack->pop (); }
		   ;

parameter_list: parameter_list ',' ident
              {
		DOT_CHAIN($1) = $3;
		$$ = $3;
	      }
              | ident
              {
		vec_safe_push (gpy_symbol_stack, $1);
		$$ = $1;
	      }
              ;

funcdef: DEF funcname '(' parameter_list_stmt ')' ':' suite
       {
	 gpy_dot_tree_t *dot = dot_build_func_decl ($2, $4, $7);
	 $$ = dot;
       }
       ;

suite: stmt_list NEWLINE
     | NEWLINE INDENT suite_statement_list DEDENT
     {
       $$ = gpy_symbol_stack->pop ();
     }
     ;

suite_statement_list: suite_statement_list indent_stmt
                   {
		     DOT_CHAIN($1) = $2;
		     $$ = $2;
		   }
                   | indent_stmt
                   {
		     vec_safe_push (gpy_symbol_stack, $1);
		     $$ = $1;
		   }
                   ;

indent_stmt: statement
           ;

statement: stmt_list NEWLINE
         | compound_stmt
         ;

stmt_list: simple_stmt
         ;

simple_stmt: expression
           | print_stmt
           | return_stmt
           | import_stmt
           ;

import_stmt: IMPORT IDENTIFIER
           {
	     $$ = dot_build_decl1 (D_KEY_IMPORT,
				   dot_build_identifier ($2));
	   }
           ;

return_stmt: RETURN expression
           {
             $$ = dot_build_decl1 (D_KEY_RETURN, $2);
	   }
           | RETURN
	   {
             $$ = dot_build_decl1 (D_KEY_RETURN, NULL);
	   }
           ;

argument_list_stmt:
                  { $$ = NULL; }
                  | argument_list
                  { $$ = gpy_symbol_stack->pop (); }
		  ;

argument_list: argument_list ',' expression
             {
	       DOT_CHAIN($1) = $3;
	       $$ = $3;
	     }
             | expression
             {
	       vec_safe_push ( gpy_symbol_stack, $1);
	       $$ = $1;
	     }
             ;

print_stmt: PRINT argument_list_stmt
          {
	    gpy_dot_tree_t *dot = dot_build_decl1 (D_PRINT_STMT, $2);
	    $$ = dot;
	  }
	  ;

expression: expression_stmt
          ;

target_list: target
           ;

target: IDENTIFIER
      {
	gpy_dot_tree_t *dot = dot_build_identifier ($1);
	$$ = dot;
      }
      | slicing
      | attributeref
      ;

ident: IDENTIFIER
     { $$ = dot_build_identifier ($1); }
     ;

attributeref: primary '.' ident
            {
	      $$ = dot_build_decl2 (D_ATTRIB_REF, $1, $3);
            }
	    ;

expression_stmt: target_list '=' expression_stmt
          { $$ = dot_build_decl2 (D_MODIFY_EXPR, $1, $3); }
          | expression_stmt '+' expression_stmt
          { $$ = dot_build_decl2 (D_ADD_EXPR, $1, $3); }
          | expression_stmt '-' expression_stmt
          { $$ = dot_build_decl2 (D_MINUS_EXPR, $1, $3); }
          | expression_stmt '*' expression_stmt
          { $$ = dot_build_decl2 (D_MULT_EXPR, $1, $3); }
          | expression_stmt LESS expression_stmt
          { $$ = dot_build_decl2 (D_LESS_EXPR, $1, $3); }
          | expression_stmt GREATER expression_stmt
          { $$ = dot_build_decl2 (D_GREATER_EXPR, $1, $3); }
          | expression_stmt EQUAL_EQUAL expression_stmt
          { $$ = dot_build_decl2 (D_EQ_EQ_EXPR, $1, $3); }
          | expression_stmt NOT_EQUAL expression_stmt
          { $$ = dot_build_decl2 (D_NOT_EQ_EXPR, $1, $3); }
          | '(' expression_stmt ')'
          { $$ = $2; }
          | primary
          ;

literal: INTEGER
       {
	 gpy_dot_tree_t *dot = dot_build_integer ($1);
	 $$ = dot;
       }
       | STRING
       {
	 gpy_dot_tree_t *dot = dot_build_string ($1);
	 $$ = dot;
       }
       ;

atom: ident
    | literal
    | enclosure
    ;

call: primary '(' argument_list_stmt ')'
    {
      gpy_dot_tree_t *dot = dot_build_decl2 (D_CALL_EXPR, $1, $3);
      $$ = dot;
    }
    ;

list_display: '[' argument_list_stmt ']'
            {
	      $$ = dot_build_decl1 (D_T_LIST, $2);
            }
            ;

enclosure: list_display
         ;

slicing: simple_slicing
       ;

simple_slicing: primary '[' expression ']'
              {
                $$ = dot_build_decl2 (D_SLICE, $1, $3);
              }
              ;

primary: atom
       | call
       | attributeref
       | slicing
       ;

%%

void yyerror (const char *msg)
{
  fatal_error ("%s at line %i\n", msg, yylineno);
}
