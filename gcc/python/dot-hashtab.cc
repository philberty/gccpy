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

#define threshold_alloc(x) (((x)+16)*3/2)

void gpy_vec_push (gpy_vector_t * const v,  void * s)
{
  if (v->size == 0)
    {
      v->size = threshold_alloc (0);
      v->vector = (void **) xcalloc (v->size, sizeof (void *));
    }
  else if (v->length >= (v->size - 1))
    {
      v->size = threshold_alloc (v->size);
      v->vector = (void **) xrealloc (v->vector, v->size * sizeof (void *));
    }

  v->vector [v->length] = s;
  v->length++;
}

void * gpy_vec_pop (gpy_vector_t * const v)
{
  void * retval = NULL;
  int idx = v->length - 1;
  if (idx >= 0)
    {
      retval = v->vector [v->length - 1];
      v->length--;
    }
  return retval;
}

void * gpy_vec_index_diag (gpy_vector_t * const v, size_t i,
			   const char * file, unsigned int line)
{
  void * retval = NULL;

  // size_t or unisnged types so >=0 is negligable!
  if (i < v->length)
    return (v->vector [i]);
  else
    {
      fprintf (stderr, "fatal: <%s:%u> -> index <%lu> out of bounds on vector <%p>!\n",
	       file, line, i, (void*)v);
      exit (EXIT_FAILURE);
    }
  return retval;
}

gpy_hashval_t gpy_dd_hash_string (const char * s)
{
  const unsigned char *str = (const unsigned char *) s;
  gpy_hashval_t r = 0;
  unsigned char c;

  while ((c = *str++) != 0)
    r = r * 67 + c - 113;

  return r;
}

gpy_hash_entry_t *
gpy_dd_hash_lookup_table (gpy_hash_tab_t * tbl, gpy_hashval_t h)
{
  gpy_hash_entry_t * retval = NULL;
  if (tbl->array)
    {
      int size = tbl->size, idx = (h % size);
      gpy_hash_entry_t *array = tbl->array;

      while (array[idx].data)
        {
          if (array[idx].hash == h)
            break;
          idx++;
          if (idx >= size)
            idx = 0;
        }
      retval = (array+idx);
    }
  else
    retval = NULL;

  return retval;
}

void ** gpy_dd_hash_insert (gpy_hashval_t h, void * obj,
			    gpy_hash_tab_t *tbl)
{
  void ** retval = NULL;
  gpy_hash_entry_t * entry = NULL;
  if (tbl->length >= tbl->size)
    gpy_dd_hash_grow_table (tbl);

  entry = gpy_dd_hash_lookup_table (tbl, h);
  if (entry->data)
    retval = &(entry->data);
  else
    {
      entry->data = obj;
      entry->hash = h;
      tbl->length++;
    }
  return retval;
}

void gpy_dd_hash_grow_table (gpy_hash_tab_t * tbl)
{
  unsigned int prev_size = tbl->size, size = 0, i;
  gpy_hash_entry_t *prev_array = tbl->array, *array;

  size = threshold_alloc (prev_size);
  array = (gpy_hash_entry_t*)
    xcalloc (size, sizeof (gpy_hash_entry_t));

  tbl->length = 0;
  tbl->size= size;
  tbl->array= array;

  for (i = 0; i < prev_size; ++i)
    {
      gpy_hashval_t h = prev_array [i].hash;
      void *s = prev_array [i].data;

      if (s)
        gpy_dd_hash_insert (h, s, tbl);
    }
  if (prev_array)
    free (prev_array);
}

void gpy_dd_hash_init_table (gpy_hash_tab_t * tb)
{
  tb->size = 0;
  tb->length = 0;
  tb->array = NULL;
}

