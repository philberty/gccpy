## Process this file with automake to produce Makefile.in
LIBFFI = @LIBFFI@
LIBFFIINCS = @LIBFFIINCS@

lib_LTLIBRARIES = libgpython.la

AM_CPPFLAGS = -I$(top_srcdir)/include
AM_YFLAGS = -d --verbose

library_includedir = $(includedir)/gpython
library_include_HEADERS = \
	include/gpython/gpython.h \
	include/gpython/runtime.h \
	include/gpython/vectors.h \
	include/gpython/objects.h

libgpython_la_CFLAGS = -DGPYHON -DSHARED  $(LIBFFIINCS) $(PTHREAD_CFLAGS)
libgpython_la_LDFLAGS = -release 0.0.0 $(PTHREAD_CFLAGS) $(AM_LDFLAGS)
libgpython_la_LIBADD = $(LIBFFI) $(PTHREAD_LIBS) $(MATH_LIBS)
libgpython_la_SOURCES = \
	runtime/gpy-gogo.c \
	runtime/gpy-module-stack.c \
	runtime/gpy-object-class.c \
	runtime/gpy-object-classmethod.c \
	runtime/gpy-object-integer.c \
	runtime/gpy-object-list.c \
	runtime/gpy-object-staticmethod.c \
	runtime/gpy-object-func.c \
	runtime/gpy-osdep-routines.c \
	runtime/gpy-vector-helper.c \
	runtime/gpy-type-utils.c \
	runtime/gpy-object-heap.c
