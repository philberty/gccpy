/* { dg-do run } */
/* { dg-require-weak "" } */
/* { dg-skip-if "No undefined" { *-*-mingw* } { "*" } { "" } } */
/* { dg-skip-if "No undefined weak" { hppa*-*-hpux* && { ! lp64 } } { "*" } { "" } } */
/* { dg-options "-Wl,-undefined,dynamic_lookup" { target *-*-darwin* } } */

int x(int a)
{
  return a;
}
int y(int a) __attribute__ ((weak));
int g = 0;
int main()
{
  int (*scan_func)(int);
  if (g)
    scan_func = x;
  else
    scan_func = y;

  if (scan_func)
    g = scan_func(10);

  return 0;
}
