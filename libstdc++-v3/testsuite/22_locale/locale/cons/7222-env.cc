// 2000-09-13 Benjamin Kosnik <bkoz@redhat.com>

// Copyright (C) 2000, 2001, 2002, 2003 Free Software Foundation
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// 22.1.1.2 locale constructors and destructors [lib.locale.cons]

#include <cwchar> // for mbstate_t
#include <locale>
#include <stdexcept>
#include <testsuite_hooks.h>

// libstdc++/7222
void test02()
{
  bool test = true;

  std::locale loc_1("");
  std::locale loc_2("");
  VERIFY( loc_1 == loc_2 );
}
 
int main()
{
  __gnu_cxx_test::run_test_wrapped_generic_locale_exception_catcher(test02);
  return 0;
}
