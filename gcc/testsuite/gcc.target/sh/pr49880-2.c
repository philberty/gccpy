/* Check that the option -mdiv=call-fp works.  */
/* { dg-do link { target "sh*-*-*" } } */
/* { dg-options "-mdiv=call-fp" }  */
/* { dg-skip-if "" { "sh*-*-*" } { "-m5*"} { "" } }  */

int
test00 (int a, int b)
{
  return a / b;
}

unsigned int
test01 (unsigned int a, unsigned b)
{
  return a / b;
}

int
main (int argc, char** argv)
{
  return test00 (argc, 123) + test01 (argc, 123);
}
