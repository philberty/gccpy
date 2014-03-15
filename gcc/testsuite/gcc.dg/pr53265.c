/* PR tree-optimization/53265 */
/* { dg-do compile } */
/* { dg-options "-O2 -Wall" } */

void bar (void *);
int baz (int);

void
fn1 (void)
{
  unsigned int a[128];
  int i;

  for (i = 0; i < 128; ++i)	/* { dg-message "note: containing loop" } */
    a[i] = i * 0x02000001;	/* { dg-warning "invokes undefined behavior" } */
  bar (a);
}

void
fn2 (void)
{
  unsigned long long a[128];
  int i;

  for (i = 0; i < 128; i++)			/* { dg-message "note: containing loop" } */
    a[i] = (i + 1LL) * 0x0123456789ABCDEFLL;	/* { dg-warning "invokes undefined behavior" } */
  bar (a);
}

void
fn3 (void)
{
  unsigned char a[16], b[16], c[16];
  int i;

  bar (b);
  for (i = 0; i < (int) (sizeof (a) / sizeof (a[0])); i++)	/* { dg-message "note: containing loop" } */
    {
      c[i + 8] = b[i];	/* { dg-warning "invokes undefined behavior" } */
      a[i + 8] = b[i + 8];
    }
  bar (a);
  bar (c);
}

void
fn4 (void)
{
  unsigned int *a[32], *o, i;

  bar (a);
  for (i = 0; i <= sizeof (a) / sizeof (a[0]); i++)	/* { dg-message "note: containing loop" "" { xfail *-*-* } } */
    {
      o = a[i];	/* { dg-warning "invokes undefined behavior" "" { xfail *-*-* } } */
      bar (o);
    }
}

void
fn5 (void)
{
  unsigned short a[23940];
  unsigned int b[1140];
  int j;

  bar (b);
  for (j = 0; j < 1140; j++)	/* { dg-message "note: containing loop" } */
    a[23940 + j - 950] = b[j];	/* { dg-warning "invokes undefined behavior" } */
  bar (a);
}

void
fn6 (void)
{
  double a[4][3], b[12];
  int i;
  bar (b);
  for (i = 0; i < 12; i++)	/* { dg-message "note: containing loop" } */
    a[0][i] = b[i] / 10000.0;	/* { dg-warning "invokes undefined behavior" } */
  bar (a);
}

void
fn7 (void)
{
  int a[16], b, c;
  bar (a);
  for (b = a[c = 0]; c < 16; b = a[++c])	/* { dg-warning "invokes undefined behavior" "" { xfail *-*-* } } */
    baz (b);
}

/* { dg-message "note: containing loop" "" { xfail *-*-* } 88 } */

const void *va, *vb, *vc, *vd, *ve;
const void *vf[4];
void
fn8 (void)
{
  unsigned long i;
  vf[0] = va; vf[1] = vb; vf[2] = vc; vf[3] = vd;
  for (i = 0; i < (sizeof (vf) / sizeof (vf[0])); i++)
    if (!vf[i])
      vf[i] = ve;
}

int wa, wb[53][5], wc[53][5];

void
fn9 (void)
{
  int i, j, k;
  for (i = 0; i < 53; i++)
    for (j = 16 / (((wa & 1) != 0) ? 8 : 4); j > 0; j--)
      {
	int d = 1;
	if (wb[i][j] == 0 || wc[i][1] != 0)
	  continue;
	for (k = 0; k < j; k++)
	  if (wc[i + k][1])
	    {
	      d = 0;
	      break;
	    }
	if (!d)
	  continue;
	wc[i][j] = baz (0);
      }
}

int xa[18];

void
fn10 (void)
{
  int i;
  for (i = 16; i < 32; i++)	/* { dg-message "note: containing loop" } */
    xa[i] = 26;			/* { dg-warning "invokes undefined behavior" } */
}

__attribute__((noinline)) static void
fn11 (int x)
{
  int i = 1;
  if (x > 1)
    do
      baz (i);
    while (++i != x);		/* { dg-bogus "invokes undefined behavior" } */
}

void
fn12 (void)
{
  fn11 (1);
  fn11 (1);
  fn11 (1);
}
