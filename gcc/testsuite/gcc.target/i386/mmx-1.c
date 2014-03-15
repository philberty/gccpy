/* { dg-do compile } */
/* { dg-do compile { target { nonpic } } } */
/* { dg-options "-O2 -Werror-implicit-function-declaration -mmmx" } */

/* Test that the intrinsics compile with optimization.  All of them are
   defined as inline functions in mmintrin.h that reference the proper
   builtin functions.  Defining away "extern" and "__inline" results in
   all of them being compiled as proper functions.  */

#define extern
#define __inline

#include <mmintrin.h>
