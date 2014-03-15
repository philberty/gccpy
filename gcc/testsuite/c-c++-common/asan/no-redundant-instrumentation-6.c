/* { dg-options "-fdump-tree-asan0" } */
/* { dg-do compile } */
/* { dg-skip-if "" { *-*-* } { "*" } { "-O0" } } */

void
foo  (int *a, char *b, char *c)
{
  __builtin_memmove (c, b, a[c[0]]);
  __builtin_memmove (c, b, a[b[0]]);
}

/* { dg-final { scan-tree-dump-times "__builtin___asan_report_load1" 5 "asan0" } } */
/* { dg-final { scan-tree-dump-times "__builtin___asan_report_store1" 2 "asan0" } } */
/* { dg-final { cleanup-tree-dump "asan0" } } */
