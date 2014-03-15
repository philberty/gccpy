/* FPU-related code for x86 and x86_64 processors.
   Copyright (C) 2005-2013 Free Software Foundation, Inc.
   Contributed by Francois-Xavier Coudert <coudert@clipper.ens.fr>

This file is part of the GNU Fortran 95 runtime library (libgfortran).

Libgfortran is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

Libgfortran is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

#ifndef __x86_64__
#include "cpuid.h"
#endif

#if defined(__sun__) && defined(__svr4__)
#include <signal.h>
#include <ucontext.h>

static volatile sig_atomic_t sigill_caught;

static void
sigill_hdlr (int sig __attribute((unused)),
	     siginfo_t *sip __attribute__((unused)),
	     ucontext_t *ucp)
{
  sigill_caught = 1;
  /* Set PC to the instruction after the faulting one to skip over it,
     otherwise we enter an infinite loop.  3 is the size of the movaps
     instruction.  */
  ucp->uc_mcontext.gregs[EIP] += 3;
  setcontext (ucp);
}
#endif

static int
has_sse (void)
{
#ifndef __x86_64__
  unsigned int eax, ebx, ecx, edx;

  if (!__get_cpuid (1, &eax, &ebx, &ecx, &edx))
    return 0;

#if defined(__sun__) && defined(__svr4__)
  /* Solaris 2 before Solaris 9 4/04 cannot execute SSE instructions even
     if the CPU supports them.  Programs receive SIGILL instead, so check
     for that at runtime.  */

  if (edx & bit_SSE)
    {
      struct sigaction act, oact;

      act.sa_handler = sigill_hdlr;
      sigemptyset (&act.sa_mask);
      /* Need to set SA_SIGINFO so a ucontext_t * is passed to the handler.  */
      act.sa_flags = SA_SIGINFO;
      sigaction (SIGILL, &act, &oact);

      /* We need a single SSE instruction here so the handler can safely skip
	 over it.  */
      __asm__ __volatile__ ("movaps\t%xmm0,%xmm0");

      sigaction (SIGILL, &oact, NULL);

      if (sigill_caught)
	return 0;
    }
#endif /* __sun__ && __svr4__ */

  return edx & bit_SSE;
#else
  return 1;
#endif
}

/* i387 -- see linux <fpu_control.h> header file for details.  */
#define _FPU_MASK_IM  0x01
#define _FPU_MASK_DM  0x02
#define _FPU_MASK_ZM  0x04
#define _FPU_MASK_OM  0x08
#define _FPU_MASK_UM  0x10
#define _FPU_MASK_PM  0x20
#define _FPU_MASK_ALL 0x3f

#define _FPU_EX_ALL   0x3f

void set_fpu (void)
{
  int excepts = 0;
  unsigned short cw;

  __asm__ __volatile__ ("fstcw\t%0" : "=m" (cw));

  if (options.fpe & GFC_FPE_INVALID) excepts |= _FPU_MASK_IM;
  if (options.fpe & GFC_FPE_DENORMAL) excepts |= _FPU_MASK_DM;
  if (options.fpe & GFC_FPE_ZERO) excepts |= _FPU_MASK_ZM;
  if (options.fpe & GFC_FPE_OVERFLOW) excepts |= _FPU_MASK_OM;
  if (options.fpe & GFC_FPE_UNDERFLOW) excepts |= _FPU_MASK_UM;
  if (options.fpe & GFC_FPE_INEXACT) excepts |= _FPU_MASK_PM;

  cw |= _FPU_MASK_ALL;
  cw &= ~excepts;

  __asm__ __volatile__ ("fnclex\n\tfldcw\t%0" : : "m" (cw));

  if (has_sse())
    {
      unsigned int cw_sse;

      __asm__ __volatile__ ("%vstmxcsr\t%0" : "=m" (cw_sse));

      /* The SSE exception masks are shifted by 7 bits.  */
      cw_sse |= _FPU_MASK_ALL << 7;
      cw_sse &= ~(excepts << 7);

      /* Clear stalled exception flags.  */
      cw_sse &= ~_FPU_EX_ALL;

      __asm__ __volatile__ ("%vldmxcsr\t%0" : : "m" (cw_sse));
    }
}
