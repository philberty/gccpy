// types.cc -- Go frontend types.

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "go-system.h"

#include "toplev.h"
#include "intl.h"
#include "tree.h"
#include "gimple.h"
#include "real.h"
#include "convert.h"

#include "go-c.h"
#include "gogo.h"
#include "operator.h"
#include "expressions.h"
#include "statements.h"
#include "export.h"
#include "import.h"
#include "backend.h"
#include "types.h"

// Forward declarations so that we don't have to make types.h #include
// backend.h.

static void
get_backend_struct_fields(Gogo* gogo, const Struct_field_list* fields,
			  bool use_placeholder,
			  std::vector<Backend::Btyped_identifier>* bfields);

static void
get_backend_slice_fields(Gogo* gogo, Array_type* type, bool use_placeholder,
			 std::vector<Backend::Btyped_identifier>* bfields);

static void
get_backend_interface_fields(Gogo* gogo, Interface_type* type,
			     bool use_placeholder,
			     std::vector<Backend::Btyped_identifier>* bfields);

// Class Type.

Type::Type(Type_classification classification)
  : classification_(classification), btype_(NULL), type_descriptor_var_(NULL)
{
}

Type::~Type()
{
}

// Get the base type for a type--skip names and forward declarations.

Type*
Type::base()
{
  switch (this->classification_)
    {
    case TYPE_NAMED:
      return this->named_type()->named_base();
    case TYPE_FORWARD:
      return this->forward_declaration_type()->real_type()->base();
    default:
      return this;
    }
}

const Type*
Type::base() const
{
  switch (this->classification_)
    {
    case TYPE_NAMED:
      return this->named_type()->named_base();
    case TYPE_FORWARD:
      return this->forward_declaration_type()->real_type()->base();
    default:
      return this;
    }
}

// Skip defined forward declarations.

Type*
Type::forwarded()
{
  Type* t = this;
  Forward_declaration_type* ftype = t->forward_declaration_type();
  while (ftype != NULL && ftype->is_defined())
    {
      t = ftype->real_type();
      ftype = t->forward_declaration_type();
    }
  return t;
}

const Type*
Type::forwarded() const
{
  const Type* t = this;
  const Forward_declaration_type* ftype = t->forward_declaration_type();
  while (ftype != NULL && ftype->is_defined())
    {
      t = ftype->real_type();
      ftype = t->forward_declaration_type();
    }
  return t;
}

// If this is a named type, return it.  Otherwise, return NULL.

Named_type*
Type::named_type()
{
  return this->forwarded()->convert_no_base<Named_type, TYPE_NAMED>();
}

const Named_type*
Type::named_type() const
{
  return this->forwarded()->convert_no_base<const Named_type, TYPE_NAMED>();
}

// Return true if this type is not defined.

bool
Type::is_undefined() const
{
  return this->forwarded()->forward_declaration_type() != NULL;
}

// Return true if this is a basic type: a type which is not composed
// of other types, and is not void.

bool
Type::is_basic_type() const
{
  switch (this->classification_)
    {
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_COMPLEX:
    case TYPE_BOOLEAN:
    case TYPE_STRING:
    case TYPE_NIL:
      return true;

    case TYPE_ERROR:
    case TYPE_VOID:
    case TYPE_FUNCTION:
    case TYPE_POINTER:
    case TYPE_STRUCT:
    case TYPE_ARRAY:
    case TYPE_MAP:
    case TYPE_CHANNEL:
    case TYPE_INTERFACE:
      return false;

    case TYPE_NAMED:
    case TYPE_FORWARD:
      return this->base()->is_basic_type();

    default:
      go_unreachable();
    }
}

// Return true if this is an abstract type.

bool
Type::is_abstract() const
{
  switch (this->classification())
    {
    case TYPE_INTEGER:
      return this->integer_type()->is_abstract();
    case TYPE_FLOAT:
      return this->float_type()->is_abstract();
    case TYPE_COMPLEX:
      return this->complex_type()->is_abstract();
    case TYPE_STRING:
      return this->is_abstract_string_type();
    case TYPE_BOOLEAN:
      return this->is_abstract_boolean_type();
    default:
      return false;
    }
}

// Return a non-abstract version of an abstract type.

Type*
Type::make_non_abstract_type()
{
  go_assert(this->is_abstract());
  switch (this->classification())
    {
    case TYPE_INTEGER:
      if (this->integer_type()->is_rune())
	return Type::lookup_integer_type("int32");
      else
	return Type::lookup_integer_type("int");
    case TYPE_FLOAT:
      return Type::lookup_float_type("float64");
    case TYPE_COMPLEX:
      return Type::lookup_complex_type("complex128");
    case TYPE_STRING:
      return Type::lookup_string_type();
    case TYPE_BOOLEAN:
      return Type::lookup_bool_type();
    default:
      go_unreachable();
    }
}

// Return true if this is an error type.  Don't give an error if we
// try to dereference an undefined forwarding type, as this is called
// in the parser when the type may legitimately be undefined.

bool
Type::is_error_type() const
{
  const Type* t = this->forwarded();
  // Note that we return false for an undefined forward type.
  switch (t->classification_)
    {
    case TYPE_ERROR:
      return true;
    case TYPE_NAMED:
      return t->named_type()->is_named_error_type();
    default:
      return false;
    }
}

// If this is a pointer type, return the type to which it points.
// Otherwise, return NULL.

Type*
Type::points_to() const
{
  const Pointer_type* ptype = this->convert<const Pointer_type,
					    TYPE_POINTER>();
  return ptype == NULL ? NULL : ptype->points_to();
}

// Return whether this is an open array type.

bool
Type::is_slice_type() const
{
  return this->array_type() != NULL && this->array_type()->length() == NULL;
}

// Return whether this is the predeclared constant nil being used as a
// type.

bool
Type::is_nil_constant_as_type() const
{
  const Type* t = this->forwarded();
  if (t->forward_declaration_type() != NULL)
    {
      const Named_object* no = t->forward_declaration_type()->named_object();
      if (no->is_unknown())
	no = no->unknown_value()->real_named_object();
      if (no != NULL
	  && no->is_const()
	  && no->const_value()->expr()->is_nil_expression())
	return true;
    }
  return false;
}

// Traverse a type.

int
Type::traverse(Type* type, Traverse* traverse)
{
  go_assert((traverse->traverse_mask() & Traverse::traverse_types) != 0
	     || (traverse->traverse_mask()
		 & Traverse::traverse_expressions) != 0);
  if (traverse->remember_type(type))
    {
      // We have already traversed this type.
      return TRAVERSE_CONTINUE;
    }
  if ((traverse->traverse_mask() & Traverse::traverse_types) != 0)
    {
      int t = traverse->type(type);
      if (t == TRAVERSE_EXIT)
	return TRAVERSE_EXIT;
      else if (t == TRAVERSE_SKIP_COMPONENTS)
	return TRAVERSE_CONTINUE;
    }
  // An array type has an expression which we need to traverse if
  // traverse_expressions is set.
  if (type->do_traverse(traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  return TRAVERSE_CONTINUE;
}

// Default implementation for do_traverse for child class.

int
Type::do_traverse(Traverse*)
{
  return TRAVERSE_CONTINUE;
}

// Return whether two types are identical.  If ERRORS_ARE_IDENTICAL,
// then return true for all erroneous types; this is used to avoid
// cascading errors.  If REASON is not NULL, optionally set *REASON to
// the reason the types are not identical.

bool
Type::are_identical(const Type* t1, const Type* t2, bool errors_are_identical,
		    std::string* reason)
{
  if (t1 == NULL || t2 == NULL)
    {
      // Something is wrong.
      return errors_are_identical ? true : t1 == t2;
    }

  // Skip defined forward declarations.
  t1 = t1->forwarded();
  t2 = t2->forwarded();

  // Ignore aliases for purposes of type identity.
  if (t1->named_type() != NULL && t1->named_type()->is_alias())
    t1 = t1->named_type()->real_type();
  if (t2->named_type() != NULL && t2->named_type()->is_alias())
    t2 = t2->named_type()->real_type();

  if (t1 == t2)
    return true;

  // An undefined forward declaration is an error.
  if (t1->forward_declaration_type() != NULL
      || t2->forward_declaration_type() != NULL)
    return errors_are_identical;

  // Avoid cascading errors with error types.
  if (t1->is_error_type() || t2->is_error_type())
    {
      if (errors_are_identical)
	return true;
      return t1->is_error_type() && t2->is_error_type();
    }

  // Get a good reason for the sink type.  Note that the sink type on
  // the left hand side of an assignment is handled in are_assignable.
  if (t1->is_sink_type() || t2->is_sink_type())
    {
      if (reason != NULL)
	*reason = "invalid use of _";
      return false;
    }

  // A named type is only identical to itself.
  if (t1->named_type() != NULL || t2->named_type() != NULL)
    return false;

  // Check type shapes.
  if (t1->classification() != t2->classification())
    return false;

  switch (t1->classification())
    {
    case TYPE_VOID:
    case TYPE_BOOLEAN:
    case TYPE_STRING:
    case TYPE_NIL:
      // These types are always identical.
      return true;

    case TYPE_INTEGER:
      return t1->integer_type()->is_identical(t2->integer_type());

    case TYPE_FLOAT:
      return t1->float_type()->is_identical(t2->float_type());

    case TYPE_COMPLEX:
      return t1->complex_type()->is_identical(t2->complex_type());

    case TYPE_FUNCTION:
      return t1->function_type()->is_identical(t2->function_type(),
					       false,
					       errors_are_identical,
					       reason);

    case TYPE_POINTER:
      return Type::are_identical(t1->points_to(), t2->points_to(),
				 errors_are_identical, reason);

    case TYPE_STRUCT:
      return t1->struct_type()->is_identical(t2->struct_type(),
					     errors_are_identical);

    case TYPE_ARRAY:
      return t1->array_type()->is_identical(t2->array_type(),
					    errors_are_identical);

    case TYPE_MAP:
      return t1->map_type()->is_identical(t2->map_type(),
					  errors_are_identical);

    case TYPE_CHANNEL:
      return t1->channel_type()->is_identical(t2->channel_type(),
					      errors_are_identical);

    case TYPE_INTERFACE:
      return t1->interface_type()->is_identical(t2->interface_type(),
						errors_are_identical);

    case TYPE_CALL_MULTIPLE_RESULT:
      if (reason != NULL)
	*reason = "invalid use of multiple-value function call";
      return false;

    default:
      go_unreachable();
    }
}

// Return true if it's OK to have a binary operation with types LHS
// and RHS.  This is not used for shifts or comparisons.

bool
Type::are_compatible_for_binop(const Type* lhs, const Type* rhs)
{
  if (Type::are_identical(lhs, rhs, true, NULL))
    return true;

  // A constant of abstract bool type may be mixed with any bool type.
  if ((rhs->is_abstract_boolean_type() && lhs->is_boolean_type())
      || (lhs->is_abstract_boolean_type() && rhs->is_boolean_type()))
    return true;

  // A constant of abstract string type may be mixed with any string
  // type.
  if ((rhs->is_abstract_string_type() && lhs->is_string_type())
      || (lhs->is_abstract_string_type() && rhs->is_string_type()))
    return true;

  lhs = lhs->base();
  rhs = rhs->base();

  // A constant of abstract integer, float, or complex type may be
  // mixed with an integer, float, or complex type.
  if ((rhs->is_abstract()
       && (rhs->integer_type() != NULL
	   || rhs->float_type() != NULL
	   || rhs->complex_type() != NULL)
       && (lhs->integer_type() != NULL
	   || lhs->float_type() != NULL
	   || lhs->complex_type() != NULL))
      || (lhs->is_abstract()
	  && (lhs->integer_type() != NULL
	      || lhs->float_type() != NULL
	      || lhs->complex_type() != NULL)
	  && (rhs->integer_type() != NULL
	      || rhs->float_type() != NULL
	      || rhs->complex_type() != NULL)))
    return true;

  // The nil type may be compared to a pointer, an interface type, a
  // slice type, a channel type, a map type, or a function type.
  if (lhs->is_nil_type()
      && (rhs->points_to() != NULL
	  || rhs->interface_type() != NULL
	  || rhs->is_slice_type()
	  || rhs->map_type() != NULL
	  || rhs->channel_type() != NULL
	  || rhs->function_type() != NULL))
    return true;
  if (rhs->is_nil_type()
      && (lhs->points_to() != NULL
	  || lhs->interface_type() != NULL
	  || lhs->is_slice_type()
	  || lhs->map_type() != NULL
	  || lhs->channel_type() != NULL
	  || lhs->function_type() != NULL))
    return true;

  return false;
}

// Return true if a value with type T1 may be compared with a value of
// type T2.  IS_EQUALITY_OP is true for == or !=, false for <, etc.

bool
Type::are_compatible_for_comparison(bool is_equality_op, const Type *t1,
				    const Type *t2, std::string *reason)
{
  if (t1 != t2
      && !Type::are_assignable(t1, t2, NULL)
      && !Type::are_assignable(t2, t1, NULL))
    {
      if (reason != NULL)
	*reason = "incompatible types in binary expression";
      return false;
    }

  if (!is_equality_op)
    {
      if (t1->integer_type() == NULL
	  && t1->float_type() == NULL
	  && !t1->is_string_type())
	{
	  if (reason != NULL)
	    *reason = _("invalid comparison of non-ordered type");
	  return false;
	}
    }
  else if (t1->is_slice_type()
	   || t1->map_type() != NULL
	   || t1->function_type() != NULL
	   || t2->is_slice_type()
	   || t2->map_type() != NULL
	   || t2->function_type() != NULL)
    {
      if (!t1->is_nil_type() && !t2->is_nil_type())
	{
	  if (reason != NULL)
	    {
	      if (t1->is_slice_type() || t2->is_slice_type())
		*reason = _("slice can only be compared to nil");
	      else if (t1->map_type() != NULL || t2->map_type() != NULL)
		*reason = _("map can only be compared to nil");
	      else
		*reason = _("func can only be compared to nil");

	      // Match 6g error messages.
	      if (t1->interface_type() != NULL || t2->interface_type() != NULL)
		{
		  char buf[200];
		  snprintf(buf, sizeof buf, _("invalid operation (%s)"),
			   reason->c_str());
		  *reason = buf;
		}
	    }
	  return false;
	}
    }
  else
    {
      if (!t1->is_boolean_type()
	  && t1->integer_type() == NULL
	  && t1->float_type() == NULL
	  && t1->complex_type() == NULL
	  && !t1->is_string_type()
	  && t1->points_to() == NULL
	  && t1->channel_type() == NULL
	  && t1->interface_type() == NULL
	  && t1->struct_type() == NULL
	  && t1->array_type() == NULL
	  && !t1->is_nil_type())
	{
	  if (reason != NULL)
	    *reason = _("invalid comparison of non-comparable type");
	  return false;
	}

      if (t1->named_type() != NULL)
	return t1->named_type()->named_type_is_comparable(reason);
      else if (t2->named_type() != NULL)
	return t2->named_type()->named_type_is_comparable(reason);
      else if (t1->struct_type() != NULL)
	{
	  const Struct_field_list* fields = t1->struct_type()->fields();
	  for (Struct_field_list::const_iterator p = fields->begin();
	       p != fields->end();
	       ++p)
	    {
	      if (Gogo::is_sink_name(p->field_name()))
		continue;

	      if (!p->type()->is_comparable())
		{
		  if (reason != NULL)
		    *reason = _("invalid comparison of non-comparable struct");
		  return false;
		}
	    }
	}
      else if (t1->array_type() != NULL)
	{
	  if (t1->array_type()->length()->is_nil_expression()
	      || !t1->array_type()->element_type()->is_comparable())
	    {
	      if (reason != NULL)
		*reason = _("invalid comparison of non-comparable array");
	      return false;
	    }
	}
    }

  return true;
}

// Return true if a value with type RHS may be assigned to a variable
// with type LHS.  If CHECK_HIDDEN_FIELDS is true, check whether any
// hidden fields are modified.  If REASON is not NULL, set *REASON to
// the reason the types are not assignable.

bool
Type::are_assignable_check_hidden(const Type* lhs, const Type* rhs,
				  bool check_hidden_fields,
				  std::string* reason)
{
  // Do some checks first.  Make sure the types are defined.
  if (rhs != NULL && !rhs->is_undefined())
    {
      if (rhs->is_void_type())
	{
	  if (reason != NULL)
	    *reason = "non-value used as value";
	  return false;
	}
      if (rhs->is_call_multiple_result_type())
	{
	  if (reason != NULL)
	    reason->assign(_("multiple-value function call in "
			     "single-value context"));
	  return false;
	}
    }

  if (lhs != NULL && !lhs->is_undefined())
    {
      // Any value may be assigned to the blank identifier.
      if (lhs->is_sink_type())
	return true;

      // All fields of a struct must be exported, or the assignment
      // must be in the same package.
      if (check_hidden_fields && rhs != NULL && !rhs->is_undefined())
	{
	  if (lhs->has_hidden_fields(NULL, reason)
	      || rhs->has_hidden_fields(NULL, reason))
	    return false;
	}
    }

  // Identical types are assignable.
  if (Type::are_identical(lhs, rhs, true, reason))
    return true;

  // The types are assignable if they have identical underlying types
  // and either LHS or RHS is not a named type.
  if (((lhs->named_type() != NULL && rhs->named_type() == NULL)
       || (rhs->named_type() != NULL && lhs->named_type() == NULL))
      && Type::are_identical(lhs->base(), rhs->base(), true, reason))
    return true;

  // The types are assignable if LHS is an interface type and RHS
  // implements the required methods.
  const Interface_type* lhs_interface_type = lhs->interface_type();
  if (lhs_interface_type != NULL)
    {
      if (lhs_interface_type->implements_interface(rhs, reason))
	return true;
      const Interface_type* rhs_interface_type = rhs->interface_type();
      if (rhs_interface_type != NULL
	  && lhs_interface_type->is_compatible_for_assign(rhs_interface_type,
							  reason))
	return true;
    }

  // The type are assignable if RHS is a bidirectional channel type,
  // LHS is a channel type, they have identical element types, and
  // either LHS or RHS is not a named type.
  if (lhs->channel_type() != NULL
      && rhs->channel_type() != NULL
      && rhs->channel_type()->may_send()
      && rhs->channel_type()->may_receive()
      && (lhs->named_type() == NULL || rhs->named_type() == NULL)
      && Type::are_identical(lhs->channel_type()->element_type(),
			     rhs->channel_type()->element_type(),
			     true,
			     reason))
    return true;

  // The nil type may be assigned to a pointer, function, slice, map,
  // channel, or interface type.
  if (rhs->is_nil_type()
      && (lhs->points_to() != NULL
	  || lhs->function_type() != NULL
	  || lhs->is_slice_type()
	  || lhs->map_type() != NULL
	  || lhs->channel_type() != NULL
	  || lhs->interface_type() != NULL))
    return true;

  // An untyped numeric constant may be assigned to a numeric type if
  // it is representable in that type.
  if ((rhs->is_abstract()
       && (rhs->integer_type() != NULL
	   || rhs->float_type() != NULL
	   || rhs->complex_type() != NULL))
      && (lhs->integer_type() != NULL
	  || lhs->float_type() != NULL
	  || lhs->complex_type() != NULL))
    return true;

  // Give some better error messages.
  if (reason != NULL && reason->empty())
    {
      if (rhs->interface_type() != NULL)
	reason->assign(_("need explicit conversion"));
      else if (lhs->named_type() != NULL && rhs->named_type() != NULL)
	{
	  size_t len = (lhs->named_type()->name().length()
			+ rhs->named_type()->name().length()
			+ 100);
	  char* buf = new char[len];
	  snprintf(buf, len, _("cannot use type %s as type %s"),
		   rhs->named_type()->message_name().c_str(),
		   lhs->named_type()->message_name().c_str());
	  reason->assign(buf);
	  delete[] buf;
	}
    }

  return false;
}

// Return true if a value with type RHS may be assigned to a variable
// with type LHS.  If REASON is not NULL, set *REASON to the reason
// the types are not assignable.

bool
Type::are_assignable(const Type* lhs, const Type* rhs, std::string* reason)
{
  return Type::are_assignable_check_hidden(lhs, rhs, false, reason);
}

// Like are_assignable but don't check for hidden fields.

bool
Type::are_assignable_hidden_ok(const Type* lhs, const Type* rhs,
			       std::string* reason)
{
  return Type::are_assignable_check_hidden(lhs, rhs, false, reason);
}

// Return true if a value with type RHS may be converted to type LHS.
// If REASON is not NULL, set *REASON to the reason the types are not
// convertible.

bool
Type::are_convertible(const Type* lhs, const Type* rhs, std::string* reason)
{
  // The types are convertible if they are assignable.
  if (Type::are_assignable(lhs, rhs, reason))
    return true;

  // The types are convertible if they have identical underlying
  // types.
  if ((lhs->named_type() != NULL || rhs->named_type() != NULL)
      && Type::are_identical(lhs->base(), rhs->base(), true, reason))
    return true;

  // The types are convertible if they are both unnamed pointer types
  // and their pointer base types have identical underlying types.
  if (lhs->named_type() == NULL
      && rhs->named_type() == NULL
      && lhs->points_to() != NULL
      && rhs->points_to() != NULL
      && (lhs->points_to()->named_type() != NULL
	  || rhs->points_to()->named_type() != NULL)
      && Type::are_identical(lhs->points_to()->base(),
			     rhs->points_to()->base(),
			     true,
			     reason))
    return true;

  // Integer and floating point types are convertible to each other.
  if ((lhs->integer_type() != NULL || lhs->float_type() != NULL)
      && (rhs->integer_type() != NULL || rhs->float_type() != NULL))
    return true;

  // Complex types are convertible to each other.
  if (lhs->complex_type() != NULL && rhs->complex_type() != NULL)
    return true;

  // An integer, or []byte, or []rune, may be converted to a string.
  if (lhs->is_string_type())
    {
      if (rhs->integer_type() != NULL)
	return true;
      if (rhs->is_slice_type())
	{
	  const Type* e = rhs->array_type()->element_type()->forwarded();
	  if (e->integer_type() != NULL
	      && (e->integer_type()->is_byte()
		  || e->integer_type()->is_rune()))
	    return true;
	}
    }

  // A string may be converted to []byte or []rune.
  if (rhs->is_string_type() && lhs->is_slice_type())
    {
      const Type* e = lhs->array_type()->element_type()->forwarded();
      if (e->integer_type() != NULL
	  && (e->integer_type()->is_byte() || e->integer_type()->is_rune()))
	return true;
    }

  // An unsafe.Pointer type may be converted to any pointer type or to
  // uintptr, and vice-versa.
  if (lhs->is_unsafe_pointer_type()
      && (rhs->points_to() != NULL
	  || (rhs->integer_type() != NULL
	      && rhs->forwarded() == Type::lookup_integer_type("uintptr"))))
    return true;
  if (rhs->is_unsafe_pointer_type()
      && (lhs->points_to() != NULL
	  || (lhs->integer_type() != NULL
	      && lhs->forwarded() == Type::lookup_integer_type("uintptr"))))
    return true;

  // Give a better error message.
  if (reason != NULL)
    {
      if (reason->empty())
	*reason = "invalid type conversion";
      else
	{
	  std::string s = "invalid type conversion (";
	  s += *reason;
	  s += ')';
	  *reason = s;
	}
    }

  return false;
}

// Return whether this type has any hidden fields.  This is only a
// possibility for a few types.

bool
Type::has_hidden_fields(const Named_type* within, std::string* reason) const
{
  switch (this->forwarded()->classification_)
    {
    case TYPE_NAMED:
      return this->named_type()->named_type_has_hidden_fields(reason);
    case TYPE_STRUCT:
      return this->struct_type()->struct_has_hidden_fields(within, reason);
    case TYPE_ARRAY:
      return this->array_type()->array_has_hidden_fields(within, reason);
    default:
      return false;
    }
}

// Return a hash code for the type to be used for method lookup.

unsigned int
Type::hash_for_method(Gogo* gogo) const
{
  unsigned int ret = 0;
  if (this->classification_ != TYPE_FORWARD)
    ret += this->classification_;
  return ret + this->do_hash_for_method(gogo);
}

// Default implementation of do_hash_for_method.  This is appropriate
// for types with no subfields.

unsigned int
Type::do_hash_for_method(Gogo*) const
{
  return 0;
}

// Return a hash code for a string, given a starting hash.

unsigned int
Type::hash_string(const std::string& s, unsigned int h)
{
  const char* p = s.data();
  size_t len = s.length();
  for (; len > 0; --len)
    {
      h ^= *p++;
      h*= 16777619;
    }
  return h;
}

// A hash table mapping unnamed types to the backend representation of
// those types.

Type::Type_btypes Type::type_btypes;

// Return a tree representing this type.

Btype*
Type::get_backend(Gogo* gogo)
{
  if (this->btype_ != NULL)
    return this->btype_;

  if (this->forward_declaration_type() != NULL
      || this->named_type() != NULL)
    return this->get_btype_without_hash(gogo);

  if (this->is_error_type())
    return gogo->backend()->error_type();

  // To avoid confusing the backend, translate all identical Go types
  // to the same backend representation.  We use a hash table to do
  // that.  There is no need to use the hash table for named types, as
  // named types are only identical to themselves.

  std::pair<Type*, Type_btype_entry> val;
  val.first = this;
  val.second.btype = NULL;
  val.second.is_placeholder = false;
  std::pair<Type_btypes::iterator, bool> ins =
    Type::type_btypes.insert(val);
  if (!ins.second && ins.first->second.btype != NULL)
    {
      // Note that GOGO can be NULL here, but only when the GCC
      // middle-end is asking for a frontend type.  That will only
      // happen for simple types, which should never require
      // placeholders.
      if (!ins.first->second.is_placeholder)
	this->btype_ = ins.first->second.btype;
      else if (gogo->named_types_are_converted())
	{
	  this->finish_backend(gogo, ins.first->second.btype);
	  ins.first->second.is_placeholder = false;
	}

      return ins.first->second.btype;
    }

  Btype* bt = this->get_btype_without_hash(gogo);

  if (ins.first->second.btype == NULL)
    {
      ins.first->second.btype = bt;
      ins.first->second.is_placeholder = false;
    }
  else
    {
      // We have already created a backend representation for this
      // type.  This can happen when an unnamed type is defined using
      // a named type which in turns uses an identical unnamed type.
      // Use the tree we created earlier and ignore the one we just
      // built.
      if (this->btype_ == bt)
	this->btype_ = ins.first->second.btype;
      bt = ins.first->second.btype;
    }

  return bt;
}

// Return the backend representation for a type without looking in the
// hash table for identical types.  This is used for named types,
// since a named type is never identical to any other type.

Btype*
Type::get_btype_without_hash(Gogo* gogo)
{
  if (this->btype_ == NULL)
    {
      Btype* bt = this->do_get_backend(gogo);

      // For a recursive function or pointer type, we will temporarily
      // return a circular pointer type during the recursion.  We
      // don't want to record that for a forwarding type, as it may
      // confuse us later.
      if (this->forward_declaration_type() != NULL
	  && gogo->backend()->is_circular_pointer_type(bt))
	return bt;

      if (gogo == NULL || !gogo->named_types_are_converted())
	return bt;

      this->btype_ = bt;
    }
  return this->btype_;
}

// Get the backend representation of a type without forcing the
// creation of the backend representation of all supporting types.
// This will return a backend type that has the correct size but may
// be incomplete.  E.g., a pointer will just be a placeholder pointer,
// and will not contain the final representation of the type to which
// it points.  This is used while converting all named types to the
// backend representation, to avoid problems with indirect references
// to types which are not yet complete.  When this is called, the
// sizes of all direct references (e.g., a struct field) should be
// known, but the sizes of indirect references (e.g., the type to
// which a pointer points) may not.

Btype*
Type::get_backend_placeholder(Gogo* gogo)
{
  if (gogo->named_types_are_converted())
    return this->get_backend(gogo);
  if (this->btype_ != NULL)
    return this->btype_;

  Btype* bt;
  switch (this->classification_)
    {
    case TYPE_ERROR:
    case TYPE_VOID:
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_COMPLEX:
    case TYPE_STRING:
    case TYPE_NIL:
      // These are simple types that can just be created directly.
      return this->get_backend(gogo);

    case TYPE_MAP:
    case TYPE_CHANNEL:
      // All maps and channels have the same backend representation.
      return this->get_backend(gogo);

    case TYPE_NAMED:
    case TYPE_FORWARD:
      // Named types keep track of their own dependencies and manage
      // their own placeholders.
      return this->get_backend(gogo);

    case TYPE_INTERFACE:
      if (this->interface_type()->is_empty())
	return Interface_type::get_backend_empty_interface_type(gogo);
      break;

    default:
      break;
    }

  std::pair<Type*, Type_btype_entry> val;
  val.first = this;
  val.second.btype = NULL;
  val.second.is_placeholder = false;
  std::pair<Type_btypes::iterator, bool> ins =
    Type::type_btypes.insert(val);
  if (!ins.second && ins.first->second.btype != NULL)
    return ins.first->second.btype;

  switch (this->classification_)
    {
    case TYPE_FUNCTION:
      {
	Location loc = this->function_type()->location();
	bt = gogo->backend()->placeholder_pointer_type("", loc, true);
      }
      break;

    case TYPE_POINTER:
      {
	Location loc = Linemap::unknown_location();
	bt = gogo->backend()->placeholder_pointer_type("", loc, false);
      }
      break;

    case TYPE_STRUCT:
      // We don't have to make the struct itself be a placeholder.  We
      // are promised that we know the sizes of the struct fields.
      // But we may have to use a placeholder for any particular
      // struct field.
      {
	std::vector<Backend::Btyped_identifier> bfields;
	get_backend_struct_fields(gogo, this->struct_type()->fields(),
				  true, &bfields);
	bt = gogo->backend()->struct_type(bfields);
      }
      break;

    case TYPE_ARRAY:
      if (this->is_slice_type())
	{
	  std::vector<Backend::Btyped_identifier> bfields;
	  get_backend_slice_fields(gogo, this->array_type(), true, &bfields);
	  bt = gogo->backend()->struct_type(bfields);
	}
      else
	{
	  Btype* element = this->array_type()->get_backend_element(gogo, true);
	  Bexpression* len = this->array_type()->get_backend_length(gogo);
	  bt = gogo->backend()->array_type(element, len);
	}
      break;
	
    case TYPE_INTERFACE:
      {
	go_assert(!this->interface_type()->is_empty());
	std::vector<Backend::Btyped_identifier> bfields;
	get_backend_interface_fields(gogo, this->interface_type(), true,
				     &bfields);
	bt = gogo->backend()->struct_type(bfields);
      }
      break;

    case TYPE_SINK:
    case TYPE_CALL_MULTIPLE_RESULT:
      /* Note that various classifications were handled in the earlier
	 switch.  */
    default:
      go_unreachable();
    }

  if (ins.first->second.btype == NULL)
    {
      ins.first->second.btype = bt;
      ins.first->second.is_placeholder = true;
    }
  else
    {
      // A placeholder for this type got created along the way.  Use
      // that one and ignore the one we just built.
      bt = ins.first->second.btype;
    }

  return bt;
}

// Complete the backend representation.  This is called for a type
// using a placeholder type.

void
Type::finish_backend(Gogo* gogo, Btype *placeholder)
{
  switch (this->classification_)
    {
    case TYPE_ERROR:
    case TYPE_VOID:
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_COMPLEX:
    case TYPE_STRING:
    case TYPE_NIL:
      go_unreachable();

    case TYPE_FUNCTION:
      {
	Btype* bt = this->do_get_backend(gogo);
	if (!gogo->backend()->set_placeholder_function_type(placeholder, bt))
	  go_assert(saw_errors());
      }
      break;

    case TYPE_POINTER:
      {
	Btype* bt = this->do_get_backend(gogo);
	if (!gogo->backend()->set_placeholder_pointer_type(placeholder, bt))
	  go_assert(saw_errors());
      }
      break;

    case TYPE_STRUCT:
      // The struct type itself is done, but we have to make sure that
      // all the field types are converted.
      this->struct_type()->finish_backend_fields(gogo);
      break;

    case TYPE_ARRAY:
      // The array type itself is done, but make sure the element type
      // is converted.
      this->array_type()->finish_backend_element(gogo);
      break;
	
    case TYPE_MAP:
    case TYPE_CHANNEL:
      go_unreachable();

    case TYPE_INTERFACE:
      // The interface type itself is done, but make sure the method
      // types are converted.
      this->interface_type()->finish_backend_methods(gogo);
      break;

    case TYPE_NAMED:
    case TYPE_FORWARD:
      go_unreachable();

    case TYPE_SINK:
    case TYPE_CALL_MULTIPLE_RESULT:
    default:
      go_unreachable();
    }

  this->btype_ = placeholder;
}

// Return a pointer to the type descriptor for this type.

tree
Type::type_descriptor_pointer(Gogo* gogo, Location location)
{
  Type* t = this->forwarded();
  if (t->named_type() != NULL && t->named_type()->is_alias())
    t = t->named_type()->real_type();
  if (t->type_descriptor_var_ == NULL)
    {
      t->make_type_descriptor_var(gogo);
      go_assert(t->type_descriptor_var_ != NULL);
    }
  tree var_tree = var_to_tree(t->type_descriptor_var_);
  if (var_tree == error_mark_node)
    return error_mark_node;
  return build_fold_addr_expr_loc(location.gcc_location(), var_tree);
}

// A mapping from unnamed types to type descriptor variables.

Type::Type_descriptor_vars Type::type_descriptor_vars;

// Build the type descriptor for this type.

void
Type::make_type_descriptor_var(Gogo* gogo)
{
  go_assert(this->type_descriptor_var_ == NULL);

  Named_type* nt = this->named_type();

  // We can have multiple instances of unnamed types, but we only want
  // to emit the type descriptor once.  We use a hash table.  This is
  // not necessary for named types, as they are unique, and we store
  // the type descriptor in the type itself.
  Bvariable** phash = NULL;
  if (nt == NULL)
    {
      Bvariable* bvnull = NULL;
      std::pair<Type_descriptor_vars::iterator, bool> ins =
	Type::type_descriptor_vars.insert(std::make_pair(this, bvnull));
      if (!ins.second)
	{
	  // We've already build a type descriptor for this type.
	  this->type_descriptor_var_ = ins.first->second;
	  return;
	}
      phash = &ins.first->second;
    }

  std::string var_name = this->type_descriptor_var_name(gogo, nt);

  // Build the contents of the type descriptor.
  Expression* initializer = this->do_type_descriptor(gogo, NULL);

  Btype* initializer_btype = initializer->type()->get_backend(gogo);

  Location loc = nt == NULL ? Linemap::predeclared_location() : nt->location();

  const Package* dummy;
  if (this->type_descriptor_defined_elsewhere(nt, &dummy))
    {
      this->type_descriptor_var_ =
	gogo->backend()->immutable_struct_reference(var_name,
						    initializer_btype,
						    loc);
      if (phash != NULL)
	*phash = this->type_descriptor_var_;
      return;
    }

  // See if this type descriptor can appear in multiple packages.
  bool is_common = false;
  if (nt != NULL)
    {
      // We create the descriptor for a builtin type whenever we need
      // it.
      is_common = nt->is_builtin();
    }
  else
    {
      // This is an unnamed type.  The descriptor could be defined in
      // any package where it is needed, and the linker will pick one
      // descriptor to keep.
      is_common = true;
    }

  // We are going to build the type descriptor in this package.  We
  // must create the variable before we convert the initializer to the
  // backend representation, because the initializer may refer to the
  // type descriptor of this type.  By setting type_descriptor_var_ we
  // ensure that type_descriptor_pointer will work if called while
  // converting INITIALIZER.

  this->type_descriptor_var_ =
    gogo->backend()->immutable_struct(var_name, false, is_common,
				      initializer_btype, loc);
  if (phash != NULL)
    *phash = this->type_descriptor_var_;

  Translate_context context(gogo, NULL, NULL, NULL);
  context.set_is_const();
  Bexpression* binitializer = tree_to_expr(initializer->get_tree(&context));

  gogo->backend()->immutable_struct_set_init(this->type_descriptor_var_,
					     var_name, false, is_common,
					     initializer_btype, loc,
					     binitializer);
}

// Return the name of the type descriptor variable.  If NT is not
// NULL, use it to get the name.  Otherwise this is an unnamed type.

std::string
Type::type_descriptor_var_name(Gogo* gogo, Named_type* nt)
{
  if (nt == NULL)
    return "__go_td_" + this->mangled_name(gogo);

  Named_object* no = nt->named_object();
  unsigned int index;
  const Named_object* in_function = nt->in_function(&index);
  std::string ret = "__go_tdn_";
  if (nt->is_builtin())
    go_assert(in_function == NULL);
  else
    {
      const std::string& pkgpath(no->package() == NULL
				 ? gogo->pkgpath_symbol()
				 : no->package()->pkgpath_symbol());
      ret.append(pkgpath);
      ret.append(1, '.');
      if (in_function != NULL)
	{
	  ret.append(Gogo::unpack_hidden_name(in_function->name()));
	  ret.append(1, '.');
	  if (index > 0)
	    {
	      char buf[30];
	      snprintf(buf, sizeof buf, "%u", index);
	      ret.append(buf);
	      ret.append(1, '.');
	    }
	}
    }

  // FIXME: This adds in pkgpath twice for hidden symbols, which is
  // pointless.
  const std::string& name(no->name());
  if (!Gogo::is_hidden_name(name))
    ret.append(name);
  else
    {
      ret.append(1, '.');
      ret.append(Gogo::pkgpath_for_symbol(Gogo::hidden_name_pkgpath(name)));
      ret.append(1, '.');
      ret.append(Gogo::unpack_hidden_name(name));
    }

  return ret;
}

// Return true if this type descriptor is defined in a different
// package.  If this returns true it sets *PACKAGE to the package.

bool
Type::type_descriptor_defined_elsewhere(Named_type* nt,
					const Package** package)
{
  if (nt != NULL)
    {
      if (nt->named_object()->package() != NULL)
	{
	  // This is a named type defined in a different package.  The
	  // type descriptor should be defined in that package.
	  *package = nt->named_object()->package();
	  return true;
	}
    }
  else
    {
      if (this->points_to() != NULL
	  && this->points_to()->named_type() != NULL
	  && this->points_to()->named_type()->named_object()->package() != NULL)
	{
	  // This is an unnamed pointer to a named type defined in a
	  // different package.  The descriptor should be defined in
	  // that package.
	  *package = this->points_to()->named_type()->named_object()->package();
	  return true;
	}
    }
  return false;
}

// Return a composite literal for a type descriptor.

Expression*
Type::type_descriptor(Gogo* gogo, Type* type)
{
  return type->do_type_descriptor(gogo, NULL);
}

// Return a composite literal for a type descriptor with a name.

Expression*
Type::named_type_descriptor(Gogo* gogo, Type* type, Named_type* name)
{
  go_assert(name != NULL && type->named_type() != name);
  return type->do_type_descriptor(gogo, name);
}

// Make a builtin struct type from a list of fields.  The fields are
// pairs of a name and a type.

Struct_type*
Type::make_builtin_struct_type(int nfields, ...)
{
  va_list ap;
  va_start(ap, nfields);

  Location bloc = Linemap::predeclared_location();
  Struct_field_list* sfl = new Struct_field_list();
  for (int i = 0; i < nfields; i++)
    {
      const char* field_name = va_arg(ap, const char *);
      Type* type = va_arg(ap, Type*);
      sfl->push_back(Struct_field(Typed_identifier(field_name, type, bloc)));
    }

  va_end(ap);

  return Type::make_struct_type(sfl, bloc);
}

// A list of builtin named types.

std::vector<Named_type*> Type::named_builtin_types;

// Make a builtin named type.

Named_type*
Type::make_builtin_named_type(const char* name, Type* type)
{
  Location bloc = Linemap::predeclared_location();
  Named_object* no = Named_object::make_type(name, NULL, type, bloc);
  Named_type* ret = no->type_value();
  Type::named_builtin_types.push_back(ret);
  return ret;
}

// Convert the named builtin types.

void
Type::convert_builtin_named_types(Gogo* gogo)
{
  for (std::vector<Named_type*>::const_iterator p =
	 Type::named_builtin_types.begin();
       p != Type::named_builtin_types.end();
       ++p)
    {
      bool r = (*p)->verify();
      go_assert(r);
      (*p)->convert(gogo);
    }
}

// Return the type of a type descriptor.  We should really tie this to
// runtime.Type rather than copying it.  This must match commonType in
// libgo/go/runtime/type.go.

Type*
Type::make_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Location bloc = Linemap::predeclared_location();

      Type* uint8_type = Type::lookup_integer_type("uint8");
      Type* uint32_type = Type::lookup_integer_type("uint32");
      Type* uintptr_type = Type::lookup_integer_type("uintptr");
      Type* string_type = Type::lookup_string_type();
      Type* pointer_string_type = Type::make_pointer_type(string_type);

      // This is an unnamed version of unsafe.Pointer.  Perhaps we
      // should use the named version instead, although that would
      // require us to create the unsafe package if it has not been
      // imported.  It probably doesn't matter.
      Type* void_type = Type::make_void_type();
      Type* unsafe_pointer_type = Type::make_pointer_type(void_type);

      // Forward declaration for the type descriptor type.
      Named_object* named_type_descriptor_type =
	Named_object::make_type_declaration("commonType", NULL, bloc);
      Type* ft = Type::make_forward_declaration(named_type_descriptor_type);
      Type* pointer_type_descriptor_type = Type::make_pointer_type(ft);

      // The type of a method on a concrete type.
      Struct_type* method_type =
	Type::make_builtin_struct_type(5,
				       "name", pointer_string_type,
				       "pkgPath", pointer_string_type,
				       "mtyp", pointer_type_descriptor_type,
				       "typ", pointer_type_descriptor_type,
				       "tfn", unsafe_pointer_type);
      Named_type* named_method_type =
	Type::make_builtin_named_type("method", method_type);

      // Information for types with a name or methods.
      Type* slice_named_method_type =
	Type::make_array_type(named_method_type, NULL);
      Struct_type* uncommon_type =
	Type::make_builtin_struct_type(3,
				       "name", pointer_string_type,
				       "pkgPath", pointer_string_type,
				       "methods", slice_named_method_type);
      Named_type* named_uncommon_type =
	Type::make_builtin_named_type("uncommonType", uncommon_type);

      Type* pointer_uncommon_type =
	Type::make_pointer_type(named_uncommon_type);

      // The type descriptor type.

      Struct_type* type_descriptor_type =
	Type::make_builtin_struct_type(10,
				       "Kind", uint8_type,
				       "align", uint8_type,
				       "fieldAlign", uint8_type,
				       "size", uintptr_type,
				       "hash", uint32_type,
				       "hashfn", uintptr_type,
				       "equalfn", uintptr_type,
				       "string", pointer_string_type,
				       "", pointer_uncommon_type,
				       "ptrToThis",
				       pointer_type_descriptor_type);

      Named_type* named = Type::make_builtin_named_type("commonType",
							type_descriptor_type);

      named_type_descriptor_type->set_type_value(named);

      ret = named;
    }

  return ret;
}

// Make the type of a pointer to a type descriptor as represented in
// Go.

Type*
Type::make_type_descriptor_ptr_type()
{
  static Type* ret;
  if (ret == NULL)
    ret = Type::make_pointer_type(Type::make_type_descriptor_type());
  return ret;
}

// Set *HASH_FN and *EQUAL_FN to the runtime functions which compute a
// hash code for this type and which compare whether two values of
// this type are equal.  If NAME is not NULL it is the name of this
// type.  HASH_FNTYPE and EQUAL_FNTYPE are the types of these
// functions, for convenience; they may be NULL.

void
Type::type_functions(Gogo* gogo, Named_type* name, Function_type* hash_fntype,
		     Function_type* equal_fntype, Named_object** hash_fn,
		     Named_object** equal_fn)
{
  if (hash_fntype == NULL || equal_fntype == NULL)
    {
      Location bloc = Linemap::predeclared_location();

      Type* uintptr_type = Type::lookup_integer_type("uintptr");
      Type* void_type = Type::make_void_type();
      Type* unsafe_pointer_type = Type::make_pointer_type(void_type);

      if (hash_fntype == NULL)
	{
	  Typed_identifier_list* params = new Typed_identifier_list();
	  params->push_back(Typed_identifier("key", unsafe_pointer_type,
					     bloc));
	  params->push_back(Typed_identifier("key_size", uintptr_type, bloc));

	  Typed_identifier_list* results = new Typed_identifier_list();
	  results->push_back(Typed_identifier("", uintptr_type, bloc));

	  hash_fntype = Type::make_function_type(NULL, params, results, bloc);
	}
      if (equal_fntype == NULL)
	{
	  Typed_identifier_list* params = new Typed_identifier_list();
	  params->push_back(Typed_identifier("key1", unsafe_pointer_type,
					     bloc));
	  params->push_back(Typed_identifier("key2", unsafe_pointer_type,
					     bloc));
	  params->push_back(Typed_identifier("key_size", uintptr_type, bloc));

	  Typed_identifier_list* results = new Typed_identifier_list();
	  results->push_back(Typed_identifier("", Type::lookup_bool_type(),
					      bloc));

	  equal_fntype = Type::make_function_type(NULL, params, results, bloc);
	}
    }

  const char* hash_fnname;
  const char* equal_fnname;
  if (this->compare_is_identity(gogo))
    {
      hash_fnname = "__go_type_hash_identity";
      equal_fnname = "__go_type_equal_identity";
    }
  else if (!this->is_comparable())
    {
      hash_fnname = "__go_type_hash_error";
      equal_fnname = "__go_type_equal_error";
    }
  else
    {
      switch (this->base()->classification())
	{
	case Type::TYPE_ERROR:
	case Type::TYPE_VOID:
	case Type::TYPE_NIL:
	case Type::TYPE_FUNCTION:
	case Type::TYPE_MAP:
	  // For these types is_comparable should have returned false.
	  go_unreachable();

	case Type::TYPE_BOOLEAN:
	case Type::TYPE_INTEGER:
	case Type::TYPE_POINTER:
	case Type::TYPE_CHANNEL:
	  // For these types compare_is_identity should have returned true.
	  go_unreachable();

	case Type::TYPE_FLOAT:
	  hash_fnname = "__go_type_hash_float";
	  equal_fnname = "__go_type_equal_float";
	  break;

	case Type::TYPE_COMPLEX:
	  hash_fnname = "__go_type_hash_complex";
	  equal_fnname = "__go_type_equal_complex";
	  break;

	case Type::TYPE_STRING:
	  hash_fnname = "__go_type_hash_string";
	  equal_fnname = "__go_type_equal_string";
	  break;

	case Type::TYPE_STRUCT:
	  {
	    // This is a struct which can not be compared using a
	    // simple identity function.  We need to build a function
	    // for comparison.
	    this->specific_type_functions(gogo, name, hash_fntype,
					  equal_fntype, hash_fn, equal_fn);
	    return;
	  }

	case Type::TYPE_ARRAY:
	  if (this->is_slice_type())
	    {
	      // Type::is_compatible_for_comparison should have
	      // returned false.
	      go_unreachable();
	    }
	  else
	    {
	      // This is an array which can not be compared using a
	      // simple identity function.  We need to build a
	      // function for comparison.
	      this->specific_type_functions(gogo, name, hash_fntype,
					    equal_fntype, hash_fn, equal_fn);
	      return;
	    }
	  break;

	case Type::TYPE_INTERFACE:
	  if (this->interface_type()->is_empty())
	    {
	      hash_fnname = "__go_type_hash_empty_interface";
	      equal_fnname = "__go_type_equal_empty_interface";
	    }
	  else
	    {
	      hash_fnname = "__go_type_hash_interface";
	      equal_fnname = "__go_type_equal_interface";
	    }
	  break;

	case Type::TYPE_NAMED:
	case Type::TYPE_FORWARD:
	  go_unreachable();

	default:
	  go_unreachable();
	}
    }


  Location bloc = Linemap::predeclared_location();
  *hash_fn = Named_object::make_function_declaration(hash_fnname, NULL,
						     hash_fntype, bloc);
  (*hash_fn)->func_declaration_value()->set_asm_name(hash_fnname);
  *equal_fn = Named_object::make_function_declaration(equal_fnname, NULL,
						      equal_fntype, bloc);
  (*equal_fn)->func_declaration_value()->set_asm_name(equal_fnname);
}

// A hash table mapping types to the specific hash functions.

Type::Type_functions Type::type_functions_table;

// Handle a type function which is specific to a type: a struct or
// array which can not use an identity comparison.

void
Type::specific_type_functions(Gogo* gogo, Named_type* name,
			      Function_type* hash_fntype,
			      Function_type* equal_fntype,
			      Named_object** hash_fn,
			      Named_object** equal_fn)
{
  Hash_equal_fn fnull(NULL, NULL);
  std::pair<Type*, Hash_equal_fn> val(name != NULL ? name : this, fnull);
  std::pair<Type_functions::iterator, bool> ins =
    Type::type_functions_table.insert(val);
  if (!ins.second)
    {
      // We already have functions for this type
      *hash_fn = ins.first->second.first;
      *equal_fn = ins.first->second.second;
      return;
    }

  std::string base_name;
  if (name == NULL)
    {
      // Mangled names can have '.' if they happen to refer to named
      // types in some way.  That's fine if this is simply a named
      // type, but otherwise it will confuse the code that builds
      // function identifiers.  Remove '.' when necessary.
      base_name = this->mangled_name(gogo);
      size_t i;
      while ((i = base_name.find('.')) != std::string::npos)
	base_name[i] = '$';
      base_name = gogo->pack_hidden_name(base_name, false);
    }
  else
    {
      // This name is already hidden or not as appropriate.
      base_name = name->name();
      unsigned int index;
      const Named_object* in_function = name->in_function(&index);
      if (in_function != NULL)
	{
	  base_name += '$' + Gogo::unpack_hidden_name(in_function->name());
	  if (index > 0)
	    {
	      char buf[30];
	      snprintf(buf, sizeof buf, "%u", index);
	      base_name += '$';
	      base_name += buf;
	    }
	}
    }
  std::string hash_name = base_name + "$hash";
  std::string equal_name = base_name + "$equal";

  Location bloc = Linemap::predeclared_location();

  const Package* package = NULL;
  bool is_defined_elsewhere =
    this->type_descriptor_defined_elsewhere(name, &package);
  if (is_defined_elsewhere)
    {
      *hash_fn = Named_object::make_function_declaration(hash_name, package,
							 hash_fntype, bloc);
      *equal_fn = Named_object::make_function_declaration(equal_name, package,
							  equal_fntype, bloc);
    }
  else
    {
      *hash_fn = gogo->declare_package_function(hash_name, hash_fntype, bloc);
      *equal_fn = gogo->declare_package_function(equal_name, equal_fntype,
						 bloc);
    }

  ins.first->second.first = *hash_fn;
  ins.first->second.second = *equal_fn;

  if (!is_defined_elsewhere)
    {
      if (gogo->in_global_scope())
	this->write_specific_type_functions(gogo, name, hash_name, hash_fntype,
					    equal_name, equal_fntype);
      else
	gogo->queue_specific_type_function(this, name, hash_name, hash_fntype,
					   equal_name, equal_fntype);
    }
}

// Write the hash and equality functions for a type which needs to be
// written specially.

void
Type::write_specific_type_functions(Gogo* gogo, Named_type* name,
				    const std::string& hash_name,
				    Function_type* hash_fntype,
				    const std::string& equal_name,
				    Function_type* equal_fntype)
{
  Location bloc = Linemap::predeclared_location();

  if (gogo->specific_type_functions_are_written())
    {
      go_assert(saw_errors());
      return;
    }

  Named_object* hash_fn = gogo->start_function(hash_name, hash_fntype, false,
					       bloc);
  gogo->start_block(bloc);

  if (this->struct_type() != NULL)
    this->struct_type()->write_hash_function(gogo, name, hash_fntype,
					     equal_fntype);
  else if (this->array_type() != NULL)
    this->array_type()->write_hash_function(gogo, name, hash_fntype,
					    equal_fntype);
  else
    go_unreachable();

  Block* b = gogo->finish_block(bloc);
  gogo->add_block(b, bloc);
  gogo->lower_block(hash_fn, b);
  gogo->finish_function(bloc);

  Named_object *equal_fn = gogo->start_function(equal_name, equal_fntype,
						false, bloc);
  gogo->start_block(bloc);

  if (this->struct_type() != NULL)
    this->struct_type()->write_equal_function(gogo, name);
  else if (this->array_type() != NULL)
    this->array_type()->write_equal_function(gogo, name);
  else
    go_unreachable();

  b = gogo->finish_block(bloc);
  gogo->add_block(b, bloc);
  gogo->lower_block(equal_fn, b);
  gogo->finish_function(bloc);
}

// Return a composite literal for the type descriptor for a plain type
// of kind RUNTIME_TYPE_KIND named NAME.

Expression*
Type::type_descriptor_constructor(Gogo* gogo, int runtime_type_kind,
				  Named_type* name, const Methods* methods,
				  bool only_value_methods)
{
  Location bloc = Linemap::predeclared_location();

  Type* td_type = Type::make_type_descriptor_type();
  const Struct_field_list* fields = td_type->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(9);

  if (!this->has_pointer())
    runtime_type_kind |= RUNTIME_TYPE_KIND_NO_POINTERS;
  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("Kind"));
  mpz_t iv;
  mpz_init_set_ui(iv, runtime_type_kind);
  vals->push_back(Expression::make_integer(&iv, p->type(), bloc));

  ++p;
  go_assert(p->is_field_name("align"));
  Expression::Type_info type_info = Expression::TYPE_INFO_ALIGNMENT;
  vals->push_back(Expression::make_type_info(this, type_info));

  ++p;
  go_assert(p->is_field_name("fieldAlign"));
  type_info = Expression::TYPE_INFO_FIELD_ALIGNMENT;
  vals->push_back(Expression::make_type_info(this, type_info));

  ++p;
  go_assert(p->is_field_name("size"));
  type_info = Expression::TYPE_INFO_SIZE;
  vals->push_back(Expression::make_type_info(this, type_info));

  ++p;
  go_assert(p->is_field_name("hash"));
  unsigned int h;
  if (name != NULL)
    h = name->hash_for_method(gogo);
  else
    h = this->hash_for_method(gogo);
  mpz_set_ui(iv, h);
  vals->push_back(Expression::make_integer(&iv, p->type(), bloc));

  ++p;
  go_assert(p->is_field_name("hashfn"));
  Function_type* hash_fntype = p->type()->function_type();

  ++p;
  go_assert(p->is_field_name("equalfn"));
  Function_type* equal_fntype = p->type()->function_type();

  Named_object* hash_fn;
  Named_object* equal_fn;
  this->type_functions(gogo, name, hash_fntype, equal_fntype, &hash_fn,
		       &equal_fn);
  vals->push_back(Expression::make_func_code_reference(hash_fn, bloc));
  vals->push_back(Expression::make_func_code_reference(equal_fn, bloc));

  ++p;
  go_assert(p->is_field_name("string"));
  Expression* s = Expression::make_string((name != NULL
					   ? name->reflection(gogo)
					   : this->reflection(gogo)),
					  bloc);
  vals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));

  ++p;
  go_assert(p->is_field_name("uncommonType"));
  if (name == NULL && methods == NULL)
    vals->push_back(Expression::make_nil(bloc));
  else
    {
      if (methods == NULL)
	methods = name->methods();
      vals->push_back(this->uncommon_type_constructor(gogo,
						      p->type()->deref(),
						      name, methods,
						      only_value_methods));
    }

  ++p;
  go_assert(p->is_field_name("ptrToThis"));
  if (name == NULL)
    vals->push_back(Expression::make_nil(bloc));
  else
    {
      Type* pt = Type::make_pointer_type(name);
      vals->push_back(Expression::make_type_descriptor(pt, bloc));
    }

  ++p;
  go_assert(p == fields->end());

  mpz_clear(iv);

  return Expression::make_struct_composite_literal(td_type, vals, bloc);
}

// Return a composite literal for the uncommon type information for
// this type.  UNCOMMON_STRUCT_TYPE is the type of the uncommon type
// struct.  If name is not NULL, it is the name of the type.  If
// METHODS is not NULL, it is the list of methods.  ONLY_VALUE_METHODS
// is true if only value methods should be included.  At least one of
// NAME and METHODS must not be NULL.

Expression*
Type::uncommon_type_constructor(Gogo* gogo, Type* uncommon_type,
				Named_type* name, const Methods* methods,
				bool only_value_methods) const
{
  Location bloc = Linemap::predeclared_location();

  const Struct_field_list* fields = uncommon_type->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(3);

  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("name"));

  ++p;
  go_assert(p->is_field_name("pkgPath"));

  if (name == NULL)
    {
      vals->push_back(Expression::make_nil(bloc));
      vals->push_back(Expression::make_nil(bloc));
    }
  else
    {
      Named_object* no = name->named_object();
      std::string n = Gogo::unpack_hidden_name(no->name());
      Expression* s = Expression::make_string(n, bloc);
      vals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));

      if (name->is_builtin())
	vals->push_back(Expression::make_nil(bloc));
      else
	{
	  const Package* package = no->package();
	  const std::string& pkgpath(package == NULL
				     ? gogo->pkgpath()
				     : package->pkgpath());
	  n.assign(pkgpath);
	  unsigned int index;
	  const Named_object* in_function = name->in_function(&index);
	  if (in_function != NULL)
	    {
	      n.append(1, '.');
	      n.append(Gogo::unpack_hidden_name(in_function->name()));
	      if (index > 0)
		{
		  char buf[30];
		  snprintf(buf, sizeof buf, "%u", index);
		  n.append(1, '.');
		  n.append(buf);
		}
	    }
	  s = Expression::make_string(n, bloc);
	  vals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));
	}
    }

  ++p;
  go_assert(p->is_field_name("methods"));
  vals->push_back(this->methods_constructor(gogo, p->type(), methods,
					    only_value_methods));

  ++p;
  go_assert(p == fields->end());

  Expression* r = Expression::make_struct_composite_literal(uncommon_type,
							    vals, bloc);
  return Expression::make_unary(OPERATOR_AND, r, bloc);
}

// Sort methods by name.

class Sort_methods
{
 public:
  bool
  operator()(const std::pair<std::string, const Method*>& m1,
	     const std::pair<std::string, const Method*>& m2) const
  { return m1.first < m2.first; }
};

// Return a composite literal for the type method table for this type.
// METHODS_TYPE is the type of the table, and is a slice type.
// METHODS is the list of methods.  If ONLY_VALUE_METHODS is true,
// then only value methods are used.

Expression*
Type::methods_constructor(Gogo* gogo, Type* methods_type,
			  const Methods* methods,
			  bool only_value_methods) const
{
  Location bloc = Linemap::predeclared_location();

  std::vector<std::pair<std::string, const Method*> > smethods;
  if (methods != NULL)
    {
      smethods.reserve(methods->count());
      for (Methods::const_iterator p = methods->begin();
	   p != methods->end();
	   ++p)
	{
	  if (p->second->is_ambiguous())
	    continue;
	  if (only_value_methods && !p->second->is_value_method())
	    continue;

	  // This is where we implement the magic //go:nointerface
	  // comment.  If we saw that comment, we don't add this
	  // method to the type descriptor.
	  if (p->second->nointerface())
	    continue;

	  smethods.push_back(std::make_pair(p->first, p->second));
	}
    }

  if (smethods.empty())
    return Expression::make_slice_composite_literal(methods_type, NULL, bloc);

  std::sort(smethods.begin(), smethods.end(), Sort_methods());

  Type* method_type = methods_type->array_type()->element_type();

  Expression_list* vals = new Expression_list();
  vals->reserve(smethods.size());
  for (std::vector<std::pair<std::string, const Method*> >::const_iterator p
	 = smethods.begin();
       p != smethods.end();
       ++p)
    vals->push_back(this->method_constructor(gogo, method_type, p->first,
					     p->second, only_value_methods));

  return Expression::make_slice_composite_literal(methods_type, vals, bloc);
}

// Return a composite literal for a single method.  METHOD_TYPE is the
// type of the entry.  METHOD_NAME is the name of the method and M is
// the method information.

Expression*
Type::method_constructor(Gogo*, Type* method_type,
			 const std::string& method_name,
			 const Method* m,
			 bool only_value_methods) const
{
  Location bloc = Linemap::predeclared_location();

  const Struct_field_list* fields = method_type->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(5);

  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("name"));
  const std::string n = Gogo::unpack_hidden_name(method_name);
  Expression* s = Expression::make_string(n, bloc);
  vals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));

  ++p;
  go_assert(p->is_field_name("pkgPath"));
  if (!Gogo::is_hidden_name(method_name))
    vals->push_back(Expression::make_nil(bloc));
  else
    {
      s = Expression::make_string(Gogo::hidden_name_pkgpath(method_name),
				  bloc);
      vals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));
    }

  Named_object* no = (m->needs_stub_method()
		      ? m->stub_object()
		      : m->named_object());

  Function_type* mtype;
  if (no->is_function())
    mtype = no->func_value()->type();
  else
    mtype = no->func_declaration_value()->type();
  go_assert(mtype->is_method());
  Type* nonmethod_type = mtype->copy_without_receiver();

  ++p;
  go_assert(p->is_field_name("mtyp"));
  vals->push_back(Expression::make_type_descriptor(nonmethod_type, bloc));

  ++p;
  go_assert(p->is_field_name("typ"));
  if (!only_value_methods && m->is_value_method())
    {
      // This is a value method on a pointer type.  Change the type of
      // the method to use a pointer receiver.  The implementation
      // always uses a pointer receiver anyhow.
      Type* rtype = mtype->receiver()->type();
      Type* prtype = Type::make_pointer_type(rtype);
      Typed_identifier* receiver =
	new Typed_identifier(mtype->receiver()->name(), prtype,
			     mtype->receiver()->location());
      mtype = Type::make_function_type(receiver,
				       (mtype->parameters() == NULL
					? NULL
					: mtype->parameters()->copy()),
				       (mtype->results() == NULL
					? NULL
					: mtype->results()->copy()),
				       mtype->location());
    }
  vals->push_back(Expression::make_type_descriptor(mtype, bloc));

  ++p;
  go_assert(p->is_field_name("tfn"));
  vals->push_back(Expression::make_func_code_reference(no, bloc));

  ++p;
  go_assert(p == fields->end());

  return Expression::make_struct_composite_literal(method_type, vals, bloc);
}

// Return a composite literal for the type descriptor of a plain type.
// RUNTIME_TYPE_KIND is the value of the kind field.  If NAME is not
// NULL, it is the name to use as well as the list of methods.

Expression*
Type::plain_type_descriptor(Gogo* gogo, int runtime_type_kind,
			    Named_type* name)
{
  return this->type_descriptor_constructor(gogo, runtime_type_kind,
					   name, NULL, true);
}

// Return the type reflection string for this type.

std::string
Type::reflection(Gogo* gogo) const
{
  std::string ret;

  // The do_reflection virtual function should set RET to the
  // reflection string.
  this->do_reflection(gogo, &ret);

  return ret;
}

// Return a mangled name for the type.

std::string
Type::mangled_name(Gogo* gogo) const
{
  std::string ret;

  // The do_mangled_name virtual function should set RET to the
  // mangled name.  For a composite type it should append a code for
  // the composition and then call do_mangled_name on the components.
  this->do_mangled_name(gogo, &ret);

  return ret;
}

// Return whether the backend size of the type is known.

bool
Type::is_backend_type_size_known(Gogo* gogo)
{
  switch (this->classification_)
    {
    case TYPE_ERROR:
    case TYPE_VOID:
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_COMPLEX:
    case TYPE_STRING:
    case TYPE_FUNCTION:
    case TYPE_POINTER:
    case TYPE_NIL:
    case TYPE_MAP:
    case TYPE_CHANNEL:
    case TYPE_INTERFACE:
      return true;

    case TYPE_STRUCT:
      {
	const Struct_field_list* fields = this->struct_type()->fields();
	for (Struct_field_list::const_iterator pf = fields->begin();
	     pf != fields->end();
	     ++pf)
	  if (!pf->type()->is_backend_type_size_known(gogo))
	    return false;
	return true;
      }

    case TYPE_ARRAY:
      {
	const Array_type* at = this->array_type();
	if (at->length() == NULL)
	  return true;
	else
	  {
	    Numeric_constant nc;
	    if (!at->length()->numeric_constant_value(&nc))
	      return false;
	    mpz_t ival;
	    if (!nc.to_int(&ival))
	      return false;
	    mpz_clear(ival);
	    return at->element_type()->is_backend_type_size_known(gogo);
	  }
      }

    case TYPE_NAMED:
      this->named_type()->convert(gogo);
      return this->named_type()->is_named_backend_type_size_known();

    case TYPE_FORWARD:
      {
	Forward_declaration_type* fdt = this->forward_declaration_type();
	return fdt->real_type()->is_backend_type_size_known(gogo);
      }

    case TYPE_SINK:
    case TYPE_CALL_MULTIPLE_RESULT:
      go_unreachable();

    default:
      go_unreachable();
    }
}

// If the size of the type can be determined, set *PSIZE to the size
// in bytes and return true.  Otherwise, return false.  This queries
// the backend.

bool
Type::backend_type_size(Gogo* gogo, unsigned int *psize)
{
  if (!this->is_backend_type_size_known(gogo))
    return false;
  Btype* bt = this->get_backend_placeholder(gogo);
  size_t size = gogo->backend()->type_size(bt);
  *psize = static_cast<unsigned int>(size);
  if (*psize != size)
    return false;
  return true;
}

// If the alignment of the type can be determined, set *PALIGN to
// the alignment in bytes and return true.  Otherwise, return false.

bool
Type::backend_type_align(Gogo* gogo, unsigned int *palign)
{
  if (!this->is_backend_type_size_known(gogo))
    return false;
  Btype* bt = this->get_backend_placeholder(gogo);
  size_t align = gogo->backend()->type_alignment(bt);
  *palign = static_cast<unsigned int>(align);
  if (*palign != align)
    return false;
  return true;
}

// Like backend_type_align, but return the alignment when used as a
// field.

bool
Type::backend_type_field_align(Gogo* gogo, unsigned int *palign)
{
  if (!this->is_backend_type_size_known(gogo))
    return false;
  Btype* bt = this->get_backend_placeholder(gogo);
  size_t a = gogo->backend()->type_field_alignment(bt);
  *palign = static_cast<unsigned int>(a);
  if (*palign != a)
    return false;
  return true;
}

// Default function to export a type.

void
Type::do_export(Export*) const
{
  go_unreachable();
}

// Import a type.

Type*
Type::import_type(Import* imp)
{
  if (imp->match_c_string("("))
    return Function_type::do_import(imp);
  else if (imp->match_c_string("*"))
    return Pointer_type::do_import(imp);
  else if (imp->match_c_string("struct "))
    return Struct_type::do_import(imp);
  else if (imp->match_c_string("["))
    return Array_type::do_import(imp);
  else if (imp->match_c_string("map "))
    return Map_type::do_import(imp);
  else if (imp->match_c_string("chan "))
    return Channel_type::do_import(imp);
  else if (imp->match_c_string("interface"))
    return Interface_type::do_import(imp);
  else
    {
      error_at(imp->location(), "import error: expected type");
      return Type::make_error_type();
    }
}

// A type used to indicate a parsing error.  This exists to simplify
// later error detection.

class Error_type : public Type
{
 public:
  Error_type()
    : Type(TYPE_ERROR)
  { }

 protected:
  bool
  do_compare_is_identity(Gogo*)
  { return false; }

  Btype*
  do_get_backend(Gogo* gogo)
  { return gogo->backend()->error_type(); }

  Expression*
  do_type_descriptor(Gogo*, Named_type*)
  { return Expression::make_error(Linemap::predeclared_location()); }

  void
  do_reflection(Gogo*, std::string*) const
  { go_assert(saw_errors()); }

  void
  do_mangled_name(Gogo*, std::string* ret) const
  { ret->push_back('E'); }
};

Type*
Type::make_error_type()
{
  static Error_type singleton_error_type;
  return &singleton_error_type;
}

// The void type.

class Void_type : public Type
{
 public:
  Void_type()
    : Type(TYPE_VOID)
  { }

 protected:
  bool
  do_compare_is_identity(Gogo*)
  { return false; }

  Btype*
  do_get_backend(Gogo* gogo)
  { return gogo->backend()->void_type(); }

  Expression*
  do_type_descriptor(Gogo*, Named_type*)
  { go_unreachable(); }

  void
  do_reflection(Gogo*, std::string*) const
  { }

  void
  do_mangled_name(Gogo*, std::string* ret) const
  { ret->push_back('v'); }
};

Type*
Type::make_void_type()
{
  static Void_type singleton_void_type;
  return &singleton_void_type;
}

// The boolean type.

class Boolean_type : public Type
{
 public:
  Boolean_type()
    : Type(TYPE_BOOLEAN)
  { }

 protected:
  bool
  do_compare_is_identity(Gogo*)
  { return true; }

  Btype*
  do_get_backend(Gogo* gogo)
  { return gogo->backend()->bool_type(); }

  Expression*
  do_type_descriptor(Gogo*, Named_type* name);

  // We should not be asked for the reflection string of a basic type.
  void
  do_reflection(Gogo*, std::string* ret) const
  { ret->append("bool"); }

  void
  do_mangled_name(Gogo*, std::string* ret) const
  { ret->push_back('b'); }
};

// Make the type descriptor.

Expression*
Boolean_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  if (name != NULL)
    return this->plain_type_descriptor(gogo, RUNTIME_TYPE_KIND_BOOL, name);
  else
    {
      Named_object* no = gogo->lookup_global("bool");
      go_assert(no != NULL);
      return Type::type_descriptor(gogo, no->type_value());
    }
}

Type*
Type::make_boolean_type()
{
  static Boolean_type boolean_type;
  return &boolean_type;
}

// The named type "bool".

static Named_type* named_bool_type;

// Get the named type "bool".

Named_type*
Type::lookup_bool_type()
{
  return named_bool_type;
}

// Make the named type "bool".

Named_type*
Type::make_named_bool_type()
{
  Type* bool_type = Type::make_boolean_type();
  Named_object* named_object =
    Named_object::make_type("bool", NULL, bool_type,
                            Linemap::predeclared_location());
  Named_type* named_type = named_object->type_value();
  named_bool_type = named_type;
  return named_type;
}

// Class Integer_type.

Integer_type::Named_integer_types Integer_type::named_integer_types;

// Create a new integer type.  Non-abstract integer types always have
// names.

Named_type*
Integer_type::create_integer_type(const char* name, bool is_unsigned,
				  int bits, int runtime_type_kind)
{
  Integer_type* integer_type = new Integer_type(false, is_unsigned, bits,
						runtime_type_kind);
  std::string sname(name);
  Named_object* named_object =
    Named_object::make_type(sname, NULL, integer_type,
                            Linemap::predeclared_location());
  Named_type* named_type = named_object->type_value();
  std::pair<Named_integer_types::iterator, bool> ins =
    Integer_type::named_integer_types.insert(std::make_pair(sname, named_type));
  go_assert(ins.second);
  return named_type;
}

// Look up an existing integer type.

Named_type*
Integer_type::lookup_integer_type(const char* name)
{
  Named_integer_types::const_iterator p =
    Integer_type::named_integer_types.find(name);
  go_assert(p != Integer_type::named_integer_types.end());
  return p->second;
}

// Create a new abstract integer type.

Integer_type*
Integer_type::create_abstract_integer_type()
{
  static Integer_type* abstract_type;
  if (abstract_type == NULL)
    {
      Type* int_type = Type::lookup_integer_type("int");
      abstract_type = new Integer_type(true, false,
				       int_type->integer_type()->bits(),
				       RUNTIME_TYPE_KIND_INT);
    }
  return abstract_type;
}

// Create a new abstract character type.

Integer_type*
Integer_type::create_abstract_character_type()
{
  static Integer_type* abstract_type;
  if (abstract_type == NULL)
    {
      abstract_type = new Integer_type(true, false, 32,
				       RUNTIME_TYPE_KIND_INT32);
      abstract_type->set_is_rune();
    }
  return abstract_type;
}

// Integer type compatibility.

bool
Integer_type::is_identical(const Integer_type* t) const
{
  if (this->is_unsigned_ != t->is_unsigned_ || this->bits_ != t->bits_)
    return false;
  return this->is_abstract_ == t->is_abstract_;
}

// Hash code.

unsigned int
Integer_type::do_hash_for_method(Gogo*) const
{
  return ((this->bits_ << 4)
	  + ((this->is_unsigned_ ? 1 : 0) << 8)
	  + ((this->is_abstract_ ? 1 : 0) << 9));
}

// Convert an Integer_type to the backend representation.

Btype*
Integer_type::do_get_backend(Gogo* gogo)
{
  if (this->is_abstract_)
    {
      go_assert(saw_errors());
      return gogo->backend()->error_type();
    }
  return gogo->backend()->integer_type(this->is_unsigned_, this->bits_);
}

// The type descriptor for an integer type.  Integer types are always
// named.

Expression*
Integer_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  go_assert(name != NULL || saw_errors());
  return this->plain_type_descriptor(gogo, this->runtime_type_kind_, name);
}

// We should not be asked for the reflection string of a basic type.

void
Integer_type::do_reflection(Gogo*, std::string*) const
{
  go_assert(saw_errors());
}

// Mangled name.

void
Integer_type::do_mangled_name(Gogo*, std::string* ret) const
{
  char buf[100];
  snprintf(buf, sizeof buf, "i%s%s%de",
	   this->is_abstract_ ? "a" : "",
	   this->is_unsigned_ ? "u" : "",
	   this->bits_);
  ret->append(buf);
}

// Make an integer type.

Named_type*
Type::make_integer_type(const char* name, bool is_unsigned, int bits,
			int runtime_type_kind)
{
  return Integer_type::create_integer_type(name, is_unsigned, bits,
					   runtime_type_kind);
}

// Make an abstract integer type.

Integer_type*
Type::make_abstract_integer_type()
{
  return Integer_type::create_abstract_integer_type();
}

// Make an abstract character type.

Integer_type*
Type::make_abstract_character_type()
{
  return Integer_type::create_abstract_character_type();
}

// Look up an integer type.

Named_type*
Type::lookup_integer_type(const char* name)
{
  return Integer_type::lookup_integer_type(name);
}

// Class Float_type.

Float_type::Named_float_types Float_type::named_float_types;

// Create a new float type.  Non-abstract float types always have
// names.

Named_type*
Float_type::create_float_type(const char* name, int bits,
			      int runtime_type_kind)
{
  Float_type* float_type = new Float_type(false, bits, runtime_type_kind);
  std::string sname(name);
  Named_object* named_object =
    Named_object::make_type(sname, NULL, float_type,
                            Linemap::predeclared_location());
  Named_type* named_type = named_object->type_value();
  std::pair<Named_float_types::iterator, bool> ins =
    Float_type::named_float_types.insert(std::make_pair(sname, named_type));
  go_assert(ins.second);
  return named_type;
}

// Look up an existing float type.

Named_type*
Float_type::lookup_float_type(const char* name)
{
  Named_float_types::const_iterator p =
    Float_type::named_float_types.find(name);
  go_assert(p != Float_type::named_float_types.end());
  return p->second;
}

// Create a new abstract float type.

Float_type*
Float_type::create_abstract_float_type()
{
  static Float_type* abstract_type;
  if (abstract_type == NULL)
    abstract_type = new Float_type(true, 64, RUNTIME_TYPE_KIND_FLOAT64);
  return abstract_type;
}

// Whether this type is identical with T.

bool
Float_type::is_identical(const Float_type* t) const
{
  if (this->bits_ != t->bits_)
    return false;
  return this->is_abstract_ == t->is_abstract_;
}

// Hash code.

unsigned int
Float_type::do_hash_for_method(Gogo*) const
{
  return (this->bits_ << 4) + ((this->is_abstract_ ? 1 : 0) << 8);
}

// Convert to the backend representation.

Btype*
Float_type::do_get_backend(Gogo* gogo)
{
  return gogo->backend()->float_type(this->bits_);
}

// The type descriptor for a float type.  Float types are always named.

Expression*
Float_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  go_assert(name != NULL || saw_errors());
  return this->plain_type_descriptor(gogo, this->runtime_type_kind_, name);
}

// We should not be asked for the reflection string of a basic type.

void
Float_type::do_reflection(Gogo*, std::string*) const
{
  go_assert(saw_errors());
}

// Mangled name.

void
Float_type::do_mangled_name(Gogo*, std::string* ret) const
{
  char buf[100];
  snprintf(buf, sizeof buf, "f%s%de",
	   this->is_abstract_ ? "a" : "",
	   this->bits_);
  ret->append(buf);
}

// Make a floating point type.

Named_type*
Type::make_float_type(const char* name, int bits, int runtime_type_kind)
{
  return Float_type::create_float_type(name, bits, runtime_type_kind);
}

// Make an abstract float type.

Float_type*
Type::make_abstract_float_type()
{
  return Float_type::create_abstract_float_type();
}

// Look up a float type.

Named_type*
Type::lookup_float_type(const char* name)
{
  return Float_type::lookup_float_type(name);
}

// Class Complex_type.

Complex_type::Named_complex_types Complex_type::named_complex_types;

// Create a new complex type.  Non-abstract complex types always have
// names.

Named_type*
Complex_type::create_complex_type(const char* name, int bits,
				  int runtime_type_kind)
{
  Complex_type* complex_type = new Complex_type(false, bits,
						runtime_type_kind);
  std::string sname(name);
  Named_object* named_object =
    Named_object::make_type(sname, NULL, complex_type,
                            Linemap::predeclared_location());
  Named_type* named_type = named_object->type_value();
  std::pair<Named_complex_types::iterator, bool> ins =
    Complex_type::named_complex_types.insert(std::make_pair(sname,
							    named_type));
  go_assert(ins.second);
  return named_type;
}

// Look up an existing complex type.

Named_type*
Complex_type::lookup_complex_type(const char* name)
{
  Named_complex_types::const_iterator p =
    Complex_type::named_complex_types.find(name);
  go_assert(p != Complex_type::named_complex_types.end());
  return p->second;
}

// Create a new abstract complex type.

Complex_type*
Complex_type::create_abstract_complex_type()
{
  static Complex_type* abstract_type;
  if (abstract_type == NULL)
    abstract_type = new Complex_type(true, 128, RUNTIME_TYPE_KIND_COMPLEX128);
  return abstract_type;
}

// Whether this type is identical with T.

bool
Complex_type::is_identical(const Complex_type *t) const
{
  if (this->bits_ != t->bits_)
    return false;
  return this->is_abstract_ == t->is_abstract_;
}

// Hash code.

unsigned int
Complex_type::do_hash_for_method(Gogo*) const
{
  return (this->bits_ << 4) + ((this->is_abstract_ ? 1 : 0) << 8);
}

// Convert to the backend representation.

Btype*
Complex_type::do_get_backend(Gogo* gogo)
{
  return gogo->backend()->complex_type(this->bits_);
}

// The type descriptor for a complex type.  Complex types are always
// named.

Expression*
Complex_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  go_assert(name != NULL || saw_errors());
  return this->plain_type_descriptor(gogo, this->runtime_type_kind_, name);
}

// We should not be asked for the reflection string of a basic type.

void
Complex_type::do_reflection(Gogo*, std::string*) const
{
  go_assert(saw_errors());
}

// Mangled name.

void
Complex_type::do_mangled_name(Gogo*, std::string* ret) const
{
  char buf[100];
  snprintf(buf, sizeof buf, "c%s%de",
	   this->is_abstract_ ? "a" : "",
	   this->bits_);
  ret->append(buf);
}

// Make a complex type.

Named_type*
Type::make_complex_type(const char* name, int bits, int runtime_type_kind)
{
  return Complex_type::create_complex_type(name, bits, runtime_type_kind);
}

// Make an abstract complex type.

Complex_type*
Type::make_abstract_complex_type()
{
  return Complex_type::create_abstract_complex_type();
}

// Look up a complex type.

Named_type*
Type::lookup_complex_type(const char* name)
{
  return Complex_type::lookup_complex_type(name);
}

// Class String_type.

// Convert String_type to the backend representation.  A string is a
// struct with two fields: a pointer to the characters and a length.

Btype*
String_type::do_get_backend(Gogo* gogo)
{
  static Btype* backend_string_type;
  if (backend_string_type == NULL)
    {
      std::vector<Backend::Btyped_identifier> fields(2);

      Type* b = gogo->lookup_global("byte")->type_value();
      Type* pb = Type::make_pointer_type(b);

      // We aren't going to get back to this field to finish the
      // backend representation, so force it to be finished now.
      if (!gogo->named_types_are_converted())
	{
	  Btype* bt = pb->get_backend_placeholder(gogo);
	  pb->finish_backend(gogo, bt);
	}

      fields[0].name = "__data";
      fields[0].btype = pb->get_backend(gogo);
      fields[0].location = Linemap::predeclared_location();

      Type* int_type = Type::lookup_integer_type("int");
      fields[1].name = "__length";
      fields[1].btype = int_type->get_backend(gogo);
      fields[1].location = fields[0].location;

      backend_string_type = gogo->backend()->struct_type(fields);
    }
  return backend_string_type;
}

// Return a tree for the length of STRING.

tree
String_type::length_tree(Gogo*, tree string)
{
  tree string_type = TREE_TYPE(string);
  go_assert(TREE_CODE(string_type) == RECORD_TYPE);
  tree length_field = DECL_CHAIN(TYPE_FIELDS(string_type));
  go_assert(strcmp(IDENTIFIER_POINTER(DECL_NAME(length_field)),
		    "__length") == 0);
  return fold_build3(COMPONENT_REF, TREE_TYPE(length_field), string,
		     length_field, NULL_TREE);
}

// Return a tree for a pointer to the bytes of STRING.

tree
String_type::bytes_tree(Gogo*, tree string)
{
  tree string_type = TREE_TYPE(string);
  go_assert(TREE_CODE(string_type) == RECORD_TYPE);
  tree bytes_field = TYPE_FIELDS(string_type);
  go_assert(strcmp(IDENTIFIER_POINTER(DECL_NAME(bytes_field)),
		    "__data") == 0);
  return fold_build3(COMPONENT_REF, TREE_TYPE(bytes_field), string,
		     bytes_field, NULL_TREE);
}

// The type descriptor for the string type.

Expression*
String_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  if (name != NULL)
    return this->plain_type_descriptor(gogo, RUNTIME_TYPE_KIND_STRING, name);
  else
    {
      Named_object* no = gogo->lookup_global("string");
      go_assert(no != NULL);
      return Type::type_descriptor(gogo, no->type_value());
    }
}

// We should not be asked for the reflection string of a basic type.

void
String_type::do_reflection(Gogo*, std::string* ret) const
{
  ret->append("string");
}

// Mangled name of a string type.

void
String_type::do_mangled_name(Gogo*, std::string* ret) const
{
  ret->push_back('z');
}

// Make a string type.

Type*
Type::make_string_type()
{
  static String_type string_type;
  return &string_type;
}

// The named type "string".

static Named_type* named_string_type;

// Get the named type "string".

Named_type*
Type::lookup_string_type()
{
  return named_string_type;
}

// Make the named type string.

Named_type*
Type::make_named_string_type()
{
  Type* string_type = Type::make_string_type();
  Named_object* named_object =
    Named_object::make_type("string", NULL, string_type,
                            Linemap::predeclared_location());
  Named_type* named_type = named_object->type_value();
  named_string_type = named_type;
  return named_type;
}

// The sink type.  This is the type of the blank identifier _.  Any
// type may be assigned to it.

class Sink_type : public Type
{
 public:
  Sink_type()
    : Type(TYPE_SINK)
  { }

 protected:
  bool
  do_compare_is_identity(Gogo*)
  { return false; }

  Btype*
  do_get_backend(Gogo*)
  { go_unreachable(); }

  Expression*
  do_type_descriptor(Gogo*, Named_type*)
  { go_unreachable(); }

  void
  do_reflection(Gogo*, std::string*) const
  { go_unreachable(); }

  void
  do_mangled_name(Gogo*, std::string*) const
  { go_unreachable(); }
};

// Make the sink type.

Type*
Type::make_sink_type()
{
  static Sink_type sink_type;
  return &sink_type;
}

// Class Function_type.

// Traversal.

int
Function_type::do_traverse(Traverse* traverse)
{
  if (this->receiver_ != NULL
      && Type::traverse(this->receiver_->type(), traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  if (this->parameters_ != NULL
      && this->parameters_->traverse(traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  if (this->results_ != NULL
      && this->results_->traverse(traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  return TRAVERSE_CONTINUE;
}

// Returns whether T is a valid redeclaration of this type.  If this
// returns false, and REASON is not NULL, *REASON may be set to a
// brief explanation of why it returned false.

bool
Function_type::is_valid_redeclaration(const Function_type* t,
				      std::string* reason) const
{
  if (!this->is_identical(t, false, true, reason))
    return false;

  // A redeclaration of a function is required to use the same names
  // for the receiver and parameters.
  if (this->receiver() != NULL
      && this->receiver()->name() != t->receiver()->name())
    {
      if (reason != NULL)
	*reason = "receiver name changed";
      return false;
    }

  const Typed_identifier_list* parms1 = this->parameters();
  const Typed_identifier_list* parms2 = t->parameters();
  if (parms1 != NULL)
    {
      Typed_identifier_list::const_iterator p1 = parms1->begin();
      for (Typed_identifier_list::const_iterator p2 = parms2->begin();
	   p2 != parms2->end();
	   ++p2, ++p1)
	{
	  if (p1->name() != p2->name())
	    {
	      if (reason != NULL)
		*reason = "parameter name changed";
	      return false;
	    }

	  // This is called at parse time, so we may have unknown
	  // types.
	  Type* t1 = p1->type()->forwarded();
	  Type* t2 = p2->type()->forwarded();
	  if (t1 != t2
	      && t1->forward_declaration_type() != NULL
	      && (t2->forward_declaration_type() == NULL
		  || (t1->forward_declaration_type()->named_object()
		      != t2->forward_declaration_type()->named_object())))
	    return false;
	}
    }

  const Typed_identifier_list* results1 = this->results();
  const Typed_identifier_list* results2 = t->results();
  if (results1 != NULL)
    {
      Typed_identifier_list::const_iterator res1 = results1->begin();
      for (Typed_identifier_list::const_iterator res2 = results2->begin();
	   res2 != results2->end();
	   ++res2, ++res1)
	{
	  if (res1->name() != res2->name())
	    {
	      if (reason != NULL)
		*reason = "result name changed";
	      return false;
	    }

	  // This is called at parse time, so we may have unknown
	  // types.
	  Type* t1 = res1->type()->forwarded();
	  Type* t2 = res2->type()->forwarded();
	  if (t1 != t2
	      && t1->forward_declaration_type() != NULL
	      && (t2->forward_declaration_type() == NULL
		  || (t1->forward_declaration_type()->named_object()
		      != t2->forward_declaration_type()->named_object())))
	    return false;
	}
    }

  return true;
}

// Check whether T is the same as this type.

bool
Function_type::is_identical(const Function_type* t, bool ignore_receiver,
			    bool errors_are_identical,
			    std::string* reason) const
{
  if (!ignore_receiver)
    {
      const Typed_identifier* r1 = this->receiver();
      const Typed_identifier* r2 = t->receiver();
      if ((r1 != NULL) != (r2 != NULL))
	{
	  if (reason != NULL)
	    *reason = _("different receiver types");
	  return false;
	}
      if (r1 != NULL)
	{
	  if (!Type::are_identical(r1->type(), r2->type(), errors_are_identical,
				   reason))
	    {
	      if (reason != NULL && !reason->empty())
		*reason = "receiver: " + *reason;
	      return false;
	    }
	}
    }

  const Typed_identifier_list* parms1 = this->parameters();
  const Typed_identifier_list* parms2 = t->parameters();
  if ((parms1 != NULL) != (parms2 != NULL))
    {
      if (reason != NULL)
	*reason = _("different number of parameters");
      return false;
    }
  if (parms1 != NULL)
    {
      Typed_identifier_list::const_iterator p1 = parms1->begin();
      for (Typed_identifier_list::const_iterator p2 = parms2->begin();
	   p2 != parms2->end();
	   ++p2, ++p1)
	{
	  if (p1 == parms1->end())
	    {
	      if (reason != NULL)
		*reason = _("different number of parameters");
	      return false;
	    }

	  if (!Type::are_identical(p1->type(), p2->type(),
				   errors_are_identical, NULL))
	    {
	      if (reason != NULL)
		*reason = _("different parameter types");
	      return false;
	    }
	}
      if (p1 != parms1->end())
	{
	  if (reason != NULL)
	    *reason = _("different number of parameters");
	return false;
	}
    }

  if (this->is_varargs() != t->is_varargs())
    {
      if (reason != NULL)
	*reason = _("different varargs");
      return false;
    }

  const Typed_identifier_list* results1 = this->results();
  const Typed_identifier_list* results2 = t->results();
  if ((results1 != NULL) != (results2 != NULL))
    {
      if (reason != NULL)
	*reason = _("different number of results");
      return false;
    }
  if (results1 != NULL)
    {
      Typed_identifier_list::const_iterator res1 = results1->begin();
      for (Typed_identifier_list::const_iterator res2 = results2->begin();
	   res2 != results2->end();
	   ++res2, ++res1)
	{
	  if (res1 == results1->end())
	    {
	      if (reason != NULL)
		*reason = _("different number of results");
	      return false;
	    }

	  if (!Type::are_identical(res1->type(), res2->type(),
				   errors_are_identical, NULL))
	    {
	      if (reason != NULL)
		*reason = _("different result types");
	      return false;
	    }
	}
      if (res1 != results1->end())
	{
	  if (reason != NULL)
	    *reason = _("different number of results");
	  return false;
	}
    }

  return true;
}

// Hash code.

unsigned int
Function_type::do_hash_for_method(Gogo* gogo) const
{
  unsigned int ret = 0;
  // We ignore the receiver type for hash codes, because we need to
  // get the same hash code for a method in an interface and a method
  // declared for a type.  The former will not have a receiver.
  if (this->parameters_ != NULL)
    {
      int shift = 1;
      for (Typed_identifier_list::const_iterator p = this->parameters_->begin();
	   p != this->parameters_->end();
	   ++p, ++shift)
	ret += p->type()->hash_for_method(gogo) << shift;
    }
  if (this->results_ != NULL)
    {
      int shift = 2;
      for (Typed_identifier_list::const_iterator p = this->results_->begin();
	   p != this->results_->end();
	   ++p, ++shift)
	ret += p->type()->hash_for_method(gogo) << shift;
    }
  if (this->is_varargs_)
    ret += 1;
  ret <<= 4;
  return ret;
}

// Get the backend representation for a function type.

Btype*
Function_type::do_get_backend(Gogo* gogo)
{
  // When we do anything with a function value other than call it, it
  // is represented as a pointer to a struct whose first field is the
  // actual function.  So that is what we return as the type of a Go
  // function.

  Location loc = this->location();
  Btype* struct_type =
    gogo->backend()->placeholder_struct_type("__go_descriptor", loc);
  Btype* ptr_struct_type = gogo->backend()->pointer_type(struct_type);

  Backend::Btyped_identifier breceiver;
  if (this->receiver_ != NULL)
    {
      breceiver.name = Gogo::unpack_hidden_name(this->receiver_->name());

      // We always pass the address of the receiver parameter, in
      // order to make interface calls work with unknown types.
      Type* rtype = this->receiver_->type();
      if (rtype->points_to() == NULL)
	rtype = Type::make_pointer_type(rtype);
      breceiver.btype = rtype->get_backend(gogo);
      breceiver.location = this->receiver_->location();
    }

  std::vector<Backend::Btyped_identifier> bparameters;
  if (this->parameters_ != NULL)
    {
      bparameters.resize(this->parameters_->size());
      size_t i = 0;
      for (Typed_identifier_list::const_iterator p = this->parameters_->begin();
	   p != this->parameters_->end();
	   ++p, ++i)
	{
	  bparameters[i].name = Gogo::unpack_hidden_name(p->name());
	  bparameters[i].btype = p->type()->get_backend(gogo);
	  bparameters[i].location = p->location();
	}
      go_assert(i == bparameters.size());
    }

  std::vector<Backend::Btyped_identifier> bresults;
  if (this->results_ != NULL)
    {
      bresults.resize(this->results_->size());
      size_t i = 0;
      for (Typed_identifier_list::const_iterator p = this->results_->begin();
	   p != this->results_->end();
	   ++p, ++i)
	{
	  bresults[i].name = Gogo::unpack_hidden_name(p->name());
	  bresults[i].btype = p->type()->get_backend(gogo);
	  bresults[i].location = p->location();
	}
      go_assert(i == bresults.size());
    }

  Btype* fntype = gogo->backend()->function_type(breceiver, bparameters,
						 bresults, loc);
  std::vector<Backend::Btyped_identifier> fields(1);
  fields[0].name = "code";
  fields[0].btype = fntype;
  fields[0].location = loc;
  if (!gogo->backend()->set_placeholder_struct_type(struct_type, fields))
    return gogo->backend()->error_type();
  return ptr_struct_type;
}

// The type of a function type descriptor.

Type*
Function_type::make_function_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Type* bool_type = Type::lookup_bool_type();

      Type* slice_type = Type::make_array_type(ptdt, NULL);

      Struct_type* s = Type::make_builtin_struct_type(4,
						      "", tdt,
						      "dotdotdot", bool_type,
						      "in", slice_type,
						      "out", slice_type);

      ret = Type::make_builtin_named_type("FuncType", s);
    }

  return ret;
}

// The type descriptor for a function type.

Expression*
Function_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  Type* ftdt = Function_type::make_function_type_descriptor_type();

  const Struct_field_list* fields = ftdt->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(4);

  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("commonType"));
  vals->push_back(this->type_descriptor_constructor(gogo,
						    RUNTIME_TYPE_KIND_FUNC,
						    name, NULL, true));

  ++p;
  go_assert(p->is_field_name("dotdotdot"));
  vals->push_back(Expression::make_boolean(this->is_varargs(), bloc));

  ++p;
  go_assert(p->is_field_name("in"));
  vals->push_back(this->type_descriptor_params(p->type(), this->receiver(),
					       this->parameters()));

  ++p;
  go_assert(p->is_field_name("out"));
  vals->push_back(this->type_descriptor_params(p->type(), NULL,
					       this->results()));

  ++p;
  go_assert(p == fields->end());

  return Expression::make_struct_composite_literal(ftdt, vals, bloc);
}

// Return a composite literal for the parameters or results of a type
// descriptor.

Expression*
Function_type::type_descriptor_params(Type* params_type,
				      const Typed_identifier* receiver,
				      const Typed_identifier_list* params)
{
  Location bloc = Linemap::predeclared_location();

  if (receiver == NULL && params == NULL)
    return Expression::make_slice_composite_literal(params_type, NULL, bloc);

  Expression_list* vals = new Expression_list();
  vals->reserve((params == NULL ? 0 : params->size())
		+ (receiver != NULL ? 1 : 0));

  if (receiver != NULL)
    vals->push_back(Expression::make_type_descriptor(receiver->type(), bloc));

  if (params != NULL)
    {
      for (Typed_identifier_list::const_iterator p = params->begin();
	   p != params->end();
	   ++p)
	vals->push_back(Expression::make_type_descriptor(p->type(), bloc));
    }

  return Expression::make_slice_composite_literal(params_type, vals, bloc);
}

// The reflection string.

void
Function_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  // FIXME: Turn this off until we straighten out the type of the
  // struct field used in a go statement which calls a method.
  // go_assert(this->receiver_ == NULL);

  ret->append("func");

  if (this->receiver_ != NULL)
    {
      ret->push_back('(');
      this->append_reflection(this->receiver_->type(), gogo, ret);
      ret->push_back(')');
    }

  ret->push_back('(');
  const Typed_identifier_list* params = this->parameters();
  if (params != NULL)
    {
      bool is_varargs = this->is_varargs_;
      for (Typed_identifier_list::const_iterator p = params->begin();
	   p != params->end();
	   ++p)
	{
	  if (p != params->begin())
	    ret->append(", ");
	  if (!is_varargs || p + 1 != params->end())
	    this->append_reflection(p->type(), gogo, ret);
	  else
	    {
	      ret->append("...");
	      this->append_reflection(p->type()->array_type()->element_type(),
				      gogo, ret);
	    }
	}
    }
  ret->push_back(')');

  const Typed_identifier_list* results = this->results();
  if (results != NULL && !results->empty())
    {
      if (results->size() == 1)
	ret->push_back(' ');
      else
	ret->append(" (");
      for (Typed_identifier_list::const_iterator p = results->begin();
	   p != results->end();
	   ++p)
	{
	  if (p != results->begin())
	    ret->append(", ");
	  this->append_reflection(p->type(), gogo, ret);
	}
      if (results->size() > 1)
	ret->push_back(')');
    }
}

// Mangled name.

void
Function_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  ret->push_back('F');

  if (this->receiver_ != NULL)
    {
      ret->push_back('m');
      this->append_mangled_name(this->receiver_->type(), gogo, ret);
    }

  const Typed_identifier_list* params = this->parameters();
  if (params != NULL)
    {
      ret->push_back('p');
      for (Typed_identifier_list::const_iterator p = params->begin();
	   p != params->end();
	   ++p)
	this->append_mangled_name(p->type(), gogo, ret);
      if (this->is_varargs_)
	ret->push_back('V');
      ret->push_back('e');
    }

  const Typed_identifier_list* results = this->results();
  if (results != NULL)
    {
      ret->push_back('r');
      for (Typed_identifier_list::const_iterator p = results->begin();
	   p != results->end();
	   ++p)
	this->append_mangled_name(p->type(), gogo, ret);
      ret->push_back('e');
    }

  ret->push_back('e');
}

// Export a function type.

void
Function_type::do_export(Export* exp) const
{
  // We don't write out the receiver.  The only function types which
  // should have a receiver are the ones associated with explicitly
  // defined methods.  For those the receiver type is written out by
  // Function::export_func.

  exp->write_c_string("(");
  bool first = true;
  if (this->parameters_ != NULL)
    {
      bool is_varargs = this->is_varargs_;
      for (Typed_identifier_list::const_iterator p =
	     this->parameters_->begin();
	   p != this->parameters_->end();
	   ++p)
	{
	  if (first)
	    first = false;
	  else
	    exp->write_c_string(", ");
	  exp->write_name(p->name());
	  exp->write_c_string(" ");
	  if (!is_varargs || p + 1 != this->parameters_->end())
	    exp->write_type(p->type());
	  else
	    {
	      exp->write_c_string("...");
	      exp->write_type(p->type()->array_type()->element_type());
	    }
	}
    }
  exp->write_c_string(")");

  const Typed_identifier_list* results = this->results_;
  if (results != NULL)
    {
      exp->write_c_string(" ");
      if (results->size() == 1 && results->begin()->name().empty())
	exp->write_type(results->begin()->type());
      else
	{
	  first = true;
	  exp->write_c_string("(");
	  for (Typed_identifier_list::const_iterator p = results->begin();
	       p != results->end();
	       ++p)
	    {
	      if (first)
		first = false;
	      else
		exp->write_c_string(", ");
	      exp->write_name(p->name());
	      exp->write_c_string(" ");
	      exp->write_type(p->type());
	    }
	  exp->write_c_string(")");
	}
    }
}

// Import a function type.

Function_type*
Function_type::do_import(Import* imp)
{
  imp->require_c_string("(");
  Typed_identifier_list* parameters;
  bool is_varargs = false;
  if (imp->peek_char() == ')')
    parameters = NULL;
  else
    {
      parameters = new Typed_identifier_list();
      while (true)
	{
	  std::string name = imp->read_name();
	  imp->require_c_string(" ");

	  if (imp->match_c_string("..."))
	    {
	      imp->advance(3);
	      is_varargs = true;
	    }

	  Type* ptype = imp->read_type();
	  if (is_varargs)
	    ptype = Type::make_array_type(ptype, NULL);
	  parameters->push_back(Typed_identifier(name, ptype,
						 imp->location()));
	  if (imp->peek_char() != ',')
	    break;
	  go_assert(!is_varargs);
	  imp->require_c_string(", ");
	}
    }
  imp->require_c_string(")");

  Typed_identifier_list* results;
  if (imp->peek_char() != ' ')
    results = NULL;
  else
    {
      imp->advance(1);
      results = new Typed_identifier_list;
      if (imp->peek_char() != '(')
	{
	  Type* rtype = imp->read_type();
	  results->push_back(Typed_identifier("", rtype, imp->location()));
	}
      else
	{
	  imp->advance(1);
	  while (true)
	    {
	      std::string name = imp->read_name();
	      imp->require_c_string(" ");
	      Type* rtype = imp->read_type();
	      results->push_back(Typed_identifier(name, rtype,
						  imp->location()));
	      if (imp->peek_char() != ',')
		break;
	      imp->require_c_string(", ");
	    }
	  imp->require_c_string(")");
	}
    }

  Function_type* ret = Type::make_function_type(NULL, parameters, results,
						imp->location());
  if (is_varargs)
    ret->set_is_varargs();
  return ret;
}

// Make a copy of a function type without a receiver.

Function_type*
Function_type::copy_without_receiver() const
{
  go_assert(this->is_method());
  Function_type *ret = Type::make_function_type(NULL, this->parameters_,
						this->results_,
						this->location_);
  if (this->is_varargs())
    ret->set_is_varargs();
  if (this->is_builtin())
    ret->set_is_builtin();
  return ret;
}

// Make a copy of a function type with a receiver.

Function_type*
Function_type::copy_with_receiver(Type* receiver_type) const
{
  go_assert(!this->is_method());
  Typed_identifier* receiver = new Typed_identifier("", receiver_type,
						    this->location_);
  Function_type* ret = Type::make_function_type(receiver, this->parameters_,
						this->results_,
						this->location_);
  if (this->is_varargs_)
    ret->set_is_varargs();
  return ret;
}

// Make a copy of a function type ignoring any receiver and adding a
// closure parameter.

Function_type*
Function_type::copy_with_names() const
{
  Typed_identifier_list* new_params = new Typed_identifier_list();
  const Typed_identifier_list* orig_params = this->parameters_;
  if (orig_params != NULL && !orig_params->empty())
    {
      static int count;
      char buf[50];
      for (Typed_identifier_list::const_iterator p = orig_params->begin();
	   p != orig_params->end();
	   ++p)
	{
	  snprintf(buf, sizeof buf, "pt.%u", count);
	  ++count;
	  new_params->push_back(Typed_identifier(buf, p->type(),
						 p->location()));
	}
    }

  const Typed_identifier_list* orig_results = this->results_;
  Typed_identifier_list* new_results;
  if (orig_results == NULL || orig_results->empty())
    new_results = NULL;
  else
    {
      new_results = new Typed_identifier_list();
      for (Typed_identifier_list::const_iterator p = orig_results->begin();
	   p != orig_results->end();
	   ++p)
	new_results->push_back(Typed_identifier("", p->type(),
						p->location()));
    }

  return Type::make_function_type(NULL, new_params, new_results,
				  this->location());
}

// Make a function type.

Function_type*
Type::make_function_type(Typed_identifier* receiver,
			 Typed_identifier_list* parameters,
			 Typed_identifier_list* results,
			 Location location)
{
  return new Function_type(receiver, parameters, results, location);
}

// Class Pointer_type.

// Traversal.

int
Pointer_type::do_traverse(Traverse* traverse)
{
  return Type::traverse(this->to_type_, traverse);
}

// Hash code.

unsigned int
Pointer_type::do_hash_for_method(Gogo* gogo) const
{
  return this->to_type_->hash_for_method(gogo) << 4;
}

// Get the backend representation for a pointer type.

Btype*
Pointer_type::do_get_backend(Gogo* gogo)
{
  Btype* to_btype = this->to_type_->get_backend(gogo);
  return gogo->backend()->pointer_type(to_btype);
}

// The type of a pointer type descriptor.

Type*
Pointer_type::make_pointer_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Struct_type* s = Type::make_builtin_struct_type(2,
						      "", tdt,
						      "elem", ptdt);

      ret = Type::make_builtin_named_type("PtrType", s);
    }

  return ret;
}

// The type descriptor for a pointer type.

Expression*
Pointer_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  if (this->is_unsafe_pointer_type())
    {
      go_assert(name != NULL);
      return this->plain_type_descriptor(gogo,
					 RUNTIME_TYPE_KIND_UNSAFE_POINTER,
					 name);
    }
  else
    {
      Location bloc = Linemap::predeclared_location();

      const Methods* methods;
      Type* deref = this->points_to();
      if (deref->named_type() != NULL)
	methods = deref->named_type()->methods();
      else if (deref->struct_type() != NULL)
	methods = deref->struct_type()->methods();
      else
	methods = NULL;

      Type* ptr_tdt = Pointer_type::make_pointer_type_descriptor_type();

      const Struct_field_list* fields = ptr_tdt->struct_type()->fields();

      Expression_list* vals = new Expression_list();
      vals->reserve(2);

      Struct_field_list::const_iterator p = fields->begin();
      go_assert(p->is_field_name("commonType"));
      vals->push_back(this->type_descriptor_constructor(gogo,
							RUNTIME_TYPE_KIND_PTR,
							name, methods, false));

      ++p;
      go_assert(p->is_field_name("elem"));
      vals->push_back(Expression::make_type_descriptor(deref, bloc));

      return Expression::make_struct_composite_literal(ptr_tdt, vals, bloc);
    }
}

// Reflection string.

void
Pointer_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  ret->push_back('*');
  this->append_reflection(this->to_type_, gogo, ret);
}

// Mangled name.

void
Pointer_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  ret->push_back('p');
  this->append_mangled_name(this->to_type_, gogo, ret);
}

// Export.

void
Pointer_type::do_export(Export* exp) const
{
  exp->write_c_string("*");
  if (this->is_unsafe_pointer_type())
    exp->write_c_string("any");
  else
    exp->write_type(this->to_type_);
}

// Import.

Pointer_type*
Pointer_type::do_import(Import* imp)
{
  imp->require_c_string("*");
  if (imp->match_c_string("any"))
    {
      imp->advance(3);
      return Type::make_pointer_type(Type::make_void_type());
    }
  Type* to = imp->read_type();
  return Type::make_pointer_type(to);
}

// Make a pointer type.

Pointer_type*
Type::make_pointer_type(Type* to_type)
{
  typedef Unordered_map(Type*, Pointer_type*) Hashtable;
  static Hashtable pointer_types;
  Hashtable::const_iterator p = pointer_types.find(to_type);
  if (p != pointer_types.end())
    return p->second;
  Pointer_type* ret = new Pointer_type(to_type);
  pointer_types[to_type] = ret;
  return ret;
}

// The nil type.  We use a special type for nil because it is not the
// same as any other type.  In C term nil has type void*, but there is
// no such type in Go.

class Nil_type : public Type
{
 public:
  Nil_type()
    : Type(TYPE_NIL)
  { }

 protected:
  bool
  do_compare_is_identity(Gogo*)
  { return false; }

  Btype*
  do_get_backend(Gogo* gogo)
  { return gogo->backend()->pointer_type(gogo->backend()->void_type()); }

  Expression*
  do_type_descriptor(Gogo*, Named_type*)
  { go_unreachable(); }

  void
  do_reflection(Gogo*, std::string*) const
  { go_unreachable(); }

  void
  do_mangled_name(Gogo*, std::string* ret) const
  { ret->push_back('n'); }
};

// Make the nil type.

Type*
Type::make_nil_type()
{
  static Nil_type singleton_nil_type;
  return &singleton_nil_type;
}

// The type of a function call which returns multiple values.  This is
// really a struct, but we don't want to confuse a function call which
// returns a struct with a function call which returns multiple
// values.

class Call_multiple_result_type : public Type
{
 public:
  Call_multiple_result_type(Call_expression* call)
    : Type(TYPE_CALL_MULTIPLE_RESULT),
      call_(call)
  { }

 protected:
  bool
  do_has_pointer() const
  {
    go_assert(saw_errors());
    return false;
  }

  bool
  do_compare_is_identity(Gogo*)
  { return false; }

  Btype*
  do_get_backend(Gogo* gogo)
  {
    go_assert(saw_errors());
    return gogo->backend()->error_type();
  }

  Expression*
  do_type_descriptor(Gogo*, Named_type*)
  {
    go_assert(saw_errors());
    return Expression::make_error(Linemap::unknown_location());
  }

  void
  do_reflection(Gogo*, std::string*) const
  { go_assert(saw_errors()); }

  void
  do_mangled_name(Gogo*, std::string*) const
  { go_assert(saw_errors()); }

 private:
  // The expression being called.
  Call_expression* call_;
};

// Make a call result type.

Type*
Type::make_call_multiple_result_type(Call_expression* call)
{
  return new Call_multiple_result_type(call);
}

// Class Struct_field.

// Get the name of a field.

const std::string&
Struct_field::field_name() const
{
  const std::string& name(this->typed_identifier_.name());
  if (!name.empty())
    return name;
  else
    {
      // This is called during parsing, before anything is lowered, so
      // we have to be pretty careful to avoid dereferencing an
      // unknown type name.
      Type* t = this->typed_identifier_.type();
      Type* dt = t;
      if (t->classification() == Type::TYPE_POINTER)
	{
	  // Very ugly.
	  Pointer_type* ptype = static_cast<Pointer_type*>(t);
	  dt = ptype->points_to();
	}
      if (dt->forward_declaration_type() != NULL)
	return dt->forward_declaration_type()->name();
      else if (dt->named_type() != NULL)
	return dt->named_type()->name();
      else if (t->is_error_type() || dt->is_error_type())
	{
	  static const std::string error_string = "*error*";
	  return error_string;
	}
      else
	{
	  // Avoid crashing in the erroneous case where T is named but
	  // DT is not.
	  go_assert(t != dt);
	  if (t->forward_declaration_type() != NULL)
	    return t->forward_declaration_type()->name();
	  else if (t->named_type() != NULL)
	    return t->named_type()->name();
	  else
	    go_unreachable();
	}
    }
}

// Return whether this field is named NAME.

bool
Struct_field::is_field_name(const std::string& name) const
{
  const std::string& me(this->typed_identifier_.name());
  if (!me.empty())
    return me == name;
  else
    {
      Type* t = this->typed_identifier_.type();
      if (t->points_to() != NULL)
	t = t->points_to();
      Named_type* nt = t->named_type();
      if (nt != NULL && nt->name() == name)
	return true;

      // This is a horrible hack caused by the fact that we don't pack
      // the names of builtin types.  FIXME.
      if (nt != NULL
	  && nt->is_builtin()
	  && nt->name() == Gogo::unpack_hidden_name(name))
	return true;

      return false;
    }
}

// Return whether this field is an embedded built-in type.

bool
Struct_field::is_embedded_builtin(Gogo* gogo) const
{
  const std::string& name(this->field_name());
  // We know that a field is an embedded type if it is anonymous.
  // We can decide if it is a built-in type by checking to see if it is
  // registered globally under the field's name.
  // This allows us to distinguish between embedded built-in types and
  // embedded types that are aliases to built-in types.
  return (this->is_anonymous()
          && !Gogo::is_hidden_name(name)
          && gogo->lookup_global(name.c_str()) != NULL);
}

// Class Struct_type.

// A hash table used to find identical unnamed structs so that they
// share method tables.

Struct_type::Identical_structs Struct_type::identical_structs;

// A hash table used to merge method sets for identical unnamed
// structs.

Struct_type::Struct_method_tables Struct_type::struct_method_tables;

// Traversal.

int
Struct_type::do_traverse(Traverse* traverse)
{
  Struct_field_list* fields = this->fields_;
  if (fields != NULL)
    {
      for (Struct_field_list::iterator p = fields->begin();
	   p != fields->end();
	   ++p)
	{
	  if (Type::traverse(p->type(), traverse) == TRAVERSE_EXIT)
	    return TRAVERSE_EXIT;
	}
    }
  return TRAVERSE_CONTINUE;
}

// Verify that the struct type is complete and valid.

bool
Struct_type::do_verify()
{
  Struct_field_list* fields = this->fields_;
  if (fields == NULL)
    return true;
  for (Struct_field_list::iterator p = fields->begin();
       p != fields->end();
       ++p)
    {
      Type* t = p->type();
      if (t->is_undefined())
	{
	  error_at(p->location(), "struct field type is incomplete");
	  p->set_type(Type::make_error_type());
	}
      else if (p->is_anonymous())
	{
	  if (t->named_type() != NULL && t->points_to() != NULL)
	    {
	      error_at(p->location(), "embedded type may not be a pointer");
	      p->set_type(Type::make_error_type());
	    }
	  else if (t->points_to() != NULL
		   && t->points_to()->interface_type() != NULL)
	    {
	      error_at(p->location(),
		       "embedded type may not be pointer to interface");
	      p->set_type(Type::make_error_type());
	    }
	}
    }
  return true;
}

// Whether this contains a pointer.

bool
Struct_type::do_has_pointer() const
{
  const Struct_field_list* fields = this->fields();
  if (fields == NULL)
    return false;
  for (Struct_field_list::const_iterator p = fields->begin();
       p != fields->end();
       ++p)
    {
      if (p->type()->has_pointer())
	return true;
    }
  return false;
}

// Whether this type is identical to T.

bool
Struct_type::is_identical(const Struct_type* t,
			  bool errors_are_identical) const
{
  const Struct_field_list* fields1 = this->fields();
  const Struct_field_list* fields2 = t->fields();
  if (fields1 == NULL || fields2 == NULL)
    return fields1 == fields2;
  Struct_field_list::const_iterator pf2 = fields2->begin();
  for (Struct_field_list::const_iterator pf1 = fields1->begin();
       pf1 != fields1->end();
       ++pf1, ++pf2)
    {
      if (pf2 == fields2->end())
	return false;
      if (pf1->field_name() != pf2->field_name())
	return false;
      if (pf1->is_anonymous() != pf2->is_anonymous()
	  || !Type::are_identical(pf1->type(), pf2->type(),
				  errors_are_identical, NULL))
	return false;
      if (!pf1->has_tag())
	{
	  if (pf2->has_tag())
	    return false;
	}
      else
	{
	  if (!pf2->has_tag())
	    return false;
	  if (pf1->tag() != pf2->tag())
	    return false;
	}
    }
  if (pf2 != fields2->end())
    return false;
  return true;
}

// Whether this struct type has any hidden fields.

bool
Struct_type::struct_has_hidden_fields(const Named_type* within,
				      std::string* reason) const
{
  const Struct_field_list* fields = this->fields();
  if (fields == NULL)
    return false;
  const Package* within_package = (within == NULL
				   ? NULL
				   : within->named_object()->package());
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf)
    {
      if (within_package != NULL
	  && !pf->is_anonymous()
	  && Gogo::is_hidden_name(pf->field_name()))
	{
	  if (reason != NULL)
	    {
	      std::string within_name = within->named_object()->message_name();
	      std::string name = Gogo::message_name(pf->field_name());
	      size_t bufsize = 200 + within_name.length() + name.length();
	      char* buf = new char[bufsize];
	      snprintf(buf, bufsize,
		       _("implicit assignment of %s%s%s hidden field %s%s%s"),
		       open_quote, within_name.c_str(), close_quote,
		       open_quote, name.c_str(), close_quote);
	      reason->assign(buf);
	      delete[] buf;
	    }
	  return true;
	}

      if (pf->type()->has_hidden_fields(within, reason))
	return true;
    }

  return false;
}

// Whether comparisons of this struct type are simple identity
// comparisons.

bool
Struct_type::do_compare_is_identity(Gogo* gogo)
{
  const Struct_field_list* fields = this->fields_;
  if (fields == NULL)
    return true;
  unsigned int offset = 0;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf)
    {
      if (Gogo::is_sink_name(pf->field_name()))
	return false;

      if (!pf->type()->compare_is_identity(gogo))
	return false;

      unsigned int field_align;
      if (!pf->type()->backend_type_align(gogo, &field_align))
	return false;
      if ((offset & (field_align - 1)) != 0)
	{
	  // This struct has padding.  We don't guarantee that that
	  // padding is zero-initialized for a stack variable, so we
	  // can't use memcmp to compare struct values.
	  return false;
	}

      unsigned int field_size;
      if (!pf->type()->backend_type_size(gogo, &field_size))
	return false;
      offset += field_size;
    }

  unsigned int struct_size;
  if (!this->backend_type_size(gogo, &struct_size))
    return false;
  if (offset != struct_size)
    {
      // Trailing padding may not be zero when on the stack.
      return false;
    }

  return true;
}

// Build identity and hash functions for this struct.

// Hash code.

unsigned int
Struct_type::do_hash_for_method(Gogo* gogo) const
{
  unsigned int ret = 0;
  if (this->fields() != NULL)
    {
      for (Struct_field_list::const_iterator pf = this->fields()->begin();
	   pf != this->fields()->end();
	   ++pf)
	ret = (ret << 1) + pf->type()->hash_for_method(gogo);
    }
  return ret <<= 2;
}

// Find the local field NAME.

const Struct_field*
Struct_type::find_local_field(const std::string& name,
			      unsigned int *pindex) const
{
  const Struct_field_list* fields = this->fields_;
  if (fields == NULL)
    return NULL;
  unsigned int i = 0;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf, ++i)
    {
      if (pf->is_field_name(name))
	{
	  if (pindex != NULL)
	    *pindex = i;
	  return &*pf;
	}
    }
  return NULL;
}

// Return an expression for field NAME in STRUCT_EXPR, or NULL.

Field_reference_expression*
Struct_type::field_reference(Expression* struct_expr, const std::string& name,
			     Location location) const
{
  unsigned int depth;
  return this->field_reference_depth(struct_expr, name, location, NULL,
				     &depth);
}

// Return an expression for a field, along with the depth at which it
// was found.

Field_reference_expression*
Struct_type::field_reference_depth(Expression* struct_expr,
				   const std::string& name,
				   Location location,
				   Saw_named_type* saw,
				   unsigned int* depth) const
{
  const Struct_field_list* fields = this->fields_;
  if (fields == NULL)
    return NULL;

  // Look for a field with this name.
  unsigned int i = 0;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf, ++i)
    {
      if (pf->is_field_name(name))
	{
	  *depth = 0;
	  return Expression::make_field_reference(struct_expr, i, location);
	}
    }

  // Look for an anonymous field which contains a field with this
  // name.
  unsigned int found_depth = 0;
  Field_reference_expression* ret = NULL;
  i = 0;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf, ++i)
    {
      if (!pf->is_anonymous())
	continue;

      Struct_type* st = pf->type()->deref()->struct_type();
      if (st == NULL)
	continue;

      Saw_named_type* hold_saw = saw;
      Saw_named_type saw_here;
      Named_type* nt = pf->type()->named_type();
      if (nt == NULL)
	nt = pf->type()->deref()->named_type();
      if (nt != NULL)
	{
	  Saw_named_type* q;
	  for (q = saw; q != NULL; q = q->next)
	    {
	      if (q->nt == nt)
		{
		  // If this is an error, it will be reported
		  // elsewhere.
		  break;
		}
	    }
	  if (q != NULL)
	    continue;
	  saw_here.next = saw;
	  saw_here.nt = nt;
	  saw = &saw_here;
	}

      // Look for a reference using a NULL struct expression.  If we
      // find one, fill in the struct expression with a reference to
      // this field.
      unsigned int subdepth;
      Field_reference_expression* sub = st->field_reference_depth(NULL, name,
								  location,
								  saw,
								  &subdepth);

      saw = hold_saw;

      if (sub == NULL)
	continue;

      if (ret == NULL || subdepth < found_depth)
	{
	  if (ret != NULL)
	    delete ret;
	  ret = sub;
	  found_depth = subdepth;
	  Expression* here = Expression::make_field_reference(struct_expr, i,
							      location);
	  if (pf->type()->points_to() != NULL)
	    here = Expression::make_unary(OPERATOR_MULT, here, location);
	  while (sub->expr() != NULL)
	    {
	      sub = sub->expr()->deref()->field_reference_expression();
	      go_assert(sub != NULL);
	    }
	  sub->set_struct_expression(here);
          sub->set_implicit(true);
	}
      else if (subdepth > found_depth)
	delete sub;
      else
	{
	  // We do not handle ambiguity here--it should be handled by
	  // Type::bind_field_or_method.
	  delete sub;
	  found_depth = 0;
	  ret = NULL;
	}
    }

  if (ret != NULL)
    *depth = found_depth + 1;

  return ret;
}

// Return the total number of fields, including embedded fields.

unsigned int
Struct_type::total_field_count() const
{
  if (this->fields_ == NULL)
    return 0;
  unsigned int ret = 0;
  for (Struct_field_list::const_iterator pf = this->fields_->begin();
       pf != this->fields_->end();
       ++pf)
    {
      if (!pf->is_anonymous() || pf->type()->struct_type() == NULL)
	++ret;
      else
	ret += pf->type()->struct_type()->total_field_count();
    }
  return ret;
}

// Return whether NAME is an unexported field, for better error reporting.

bool
Struct_type::is_unexported_local_field(Gogo* gogo,
				       const std::string& name) const
{
  const Struct_field_list* fields = this->fields_;
  if (fields != NULL)
    {
      for (Struct_field_list::const_iterator pf = fields->begin();
	   pf != fields->end();
	   ++pf)
	{
	  const std::string& field_name(pf->field_name());
	  if (Gogo::is_hidden_name(field_name)
	      && name == Gogo::unpack_hidden_name(field_name)
	      && gogo->pack_hidden_name(name, false) != field_name)
	    return true;
	}
    }
  return false;
}

// Finalize the methods of an unnamed struct.

void
Struct_type::finalize_methods(Gogo* gogo)
{
  if (this->all_methods_ != NULL)
    return;

  // It is possible to have multiple identical structs that have
  // methods.  We want them to share method tables.  Otherwise we will
  // emit identical methods more than once, which is bad since they
  // will even have the same names.
  std::pair<Identical_structs::iterator, bool> ins =
    Struct_type::identical_structs.insert(std::make_pair(this, this));
  if (!ins.second)
    {
      // An identical struct was already entered into the hash table.
      // Note that finalize_methods is, fortunately, not recursive.
      this->all_methods_ = ins.first->second->all_methods_;
      return;
    }

  Type::finalize_methods(gogo, this, this->location_, &this->all_methods_);
}

// Return the method NAME, or NULL if there isn't one or if it is
// ambiguous.  Set *IS_AMBIGUOUS if the method exists but is
// ambiguous.

Method*
Struct_type::method_function(const std::string& name, bool* is_ambiguous) const
{
  return Type::method_function(this->all_methods_, name, is_ambiguous);
}

// Return a pointer to the interface method table for this type for
// the interface INTERFACE.  IS_POINTER is true if this is for a
// pointer to THIS.

tree
Struct_type::interface_method_table(Gogo* gogo,
				    const Interface_type* interface,
				    bool is_pointer)
{
  std::pair<Struct_type*, Struct_type::Struct_method_table_pair*>
    val(this, NULL);
  std::pair<Struct_type::Struct_method_tables::iterator, bool> ins =
    Struct_type::struct_method_tables.insert(val);

  Struct_method_table_pair* smtp;
  if (!ins.second)
    smtp = ins.first->second;
  else
    {
      smtp = new Struct_method_table_pair();
      smtp->first = NULL;
      smtp->second = NULL;
      ins.first->second = smtp;
    }

  return Type::interface_method_table(gogo, this, interface, is_pointer,
				      &smtp->first, &smtp->second);
}

// Convert struct fields to the backend representation.  This is not
// declared in types.h so that types.h doesn't have to #include
// backend.h.

static void
get_backend_struct_fields(Gogo* gogo, const Struct_field_list* fields,
			  bool use_placeholder,
			  std::vector<Backend::Btyped_identifier>* bfields)
{
  bfields->resize(fields->size());
  size_t i = 0;
  for (Struct_field_list::const_iterator p = fields->begin();
       p != fields->end();
       ++p, ++i)
    {
      (*bfields)[i].name = Gogo::unpack_hidden_name(p->field_name());
      (*bfields)[i].btype = (use_placeholder
			     ? p->type()->get_backend_placeholder(gogo)
			     : p->type()->get_backend(gogo));
      (*bfields)[i].location = p->location();
    }
  go_assert(i == fields->size());
}

// Get the tree for a struct type.

Btype*
Struct_type::do_get_backend(Gogo* gogo)
{
  std::vector<Backend::Btyped_identifier> bfields;
  get_backend_struct_fields(gogo, this->fields_, false, &bfields);
  return gogo->backend()->struct_type(bfields);
}

// Finish the backend representation of the fields of a struct.

void
Struct_type::finish_backend_fields(Gogo* gogo)
{
  const Struct_field_list* fields = this->fields_;
  if (fields != NULL)
    {
      for (Struct_field_list::const_iterator p = fields->begin();
	   p != fields->end();
	   ++p)
	p->type()->get_backend(gogo);
    }
}

// The type of a struct type descriptor.

Type*
Struct_type::make_struct_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Type* uintptr_type = Type::lookup_integer_type("uintptr");
      Type* string_type = Type::lookup_string_type();
      Type* pointer_string_type = Type::make_pointer_type(string_type);

      Struct_type* sf =
	Type::make_builtin_struct_type(5,
				       "name", pointer_string_type,
				       "pkgPath", pointer_string_type,
				       "typ", ptdt,
				       "tag", pointer_string_type,
				       "offset", uintptr_type);
      Type* nsf = Type::make_builtin_named_type("structField", sf);

      Type* slice_type = Type::make_array_type(nsf, NULL);

      Struct_type* s = Type::make_builtin_struct_type(2,
						      "", tdt,
						      "fields", slice_type);

      ret = Type::make_builtin_named_type("StructType", s);
    }

  return ret;
}

// Build a type descriptor for a struct type.

Expression*
Struct_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  Type* stdt = Struct_type::make_struct_type_descriptor_type();

  const Struct_field_list* fields = stdt->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(2);

  const Methods* methods = this->methods();
  // A named struct should not have methods--the methods should attach
  // to the named type.
  go_assert(methods == NULL || name == NULL);

  Struct_field_list::const_iterator ps = fields->begin();
  go_assert(ps->is_field_name("commonType"));
  vals->push_back(this->type_descriptor_constructor(gogo,
						    RUNTIME_TYPE_KIND_STRUCT,
						    name, methods, true));

  ++ps;
  go_assert(ps->is_field_name("fields"));

  Expression_list* elements = new Expression_list();
  elements->reserve(this->fields_->size());
  Type* element_type = ps->type()->array_type()->element_type();
  for (Struct_field_list::const_iterator pf = this->fields_->begin();
       pf != this->fields_->end();
       ++pf)
    {
      const Struct_field_list* f = element_type->struct_type()->fields();

      Expression_list* fvals = new Expression_list();
      fvals->reserve(5);

      Struct_field_list::const_iterator q = f->begin();
      go_assert(q->is_field_name("name"));
      if (pf->is_anonymous())
	fvals->push_back(Expression::make_nil(bloc));
      else
	{
	  std::string n = Gogo::unpack_hidden_name(pf->field_name());
	  Expression* s = Expression::make_string(n, bloc);
	  fvals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));
	}

      ++q;
      go_assert(q->is_field_name("pkgPath"));
      bool is_embedded_builtin = pf->is_embedded_builtin(gogo);
      if (!Gogo::is_hidden_name(pf->field_name()) && !is_embedded_builtin)
        fvals->push_back(Expression::make_nil(bloc));
      else
	{
	  std::string n;
          if (is_embedded_builtin)
            n = gogo->package_name();
          else
            n = Gogo::hidden_name_pkgpath(pf->field_name());
	  Expression* s = Expression::make_string(n, bloc);
	  fvals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));
	}

      ++q;
      go_assert(q->is_field_name("typ"));
      fvals->push_back(Expression::make_type_descriptor(pf->type(), bloc));

      ++q;
      go_assert(q->is_field_name("tag"));
      if (!pf->has_tag())
	fvals->push_back(Expression::make_nil(bloc));
      else
	{
	  Expression* s = Expression::make_string(pf->tag(), bloc);
	  fvals->push_back(Expression::make_unary(OPERATOR_AND, s, bloc));
	}

      ++q;
      go_assert(q->is_field_name("offset"));
      fvals->push_back(Expression::make_struct_field_offset(this, &*pf));

      Expression* v = Expression::make_struct_composite_literal(element_type,
								fvals, bloc);
      elements->push_back(v);
    }

  vals->push_back(Expression::make_slice_composite_literal(ps->type(),
							   elements, bloc));

  return Expression::make_struct_composite_literal(stdt, vals, bloc);
}

// Write the hash function for a struct which can not use the identity
// function.

void
Struct_type::write_hash_function(Gogo* gogo, Named_type*,
				 Function_type* hash_fntype,
				 Function_type* equal_fntype)
{
  Location bloc = Linemap::predeclared_location();

  // The pointer to the struct that we are going to hash.  This is an
  // argument to the hash function we are implementing here.
  Named_object* key_arg = gogo->lookup("key", NULL);
  go_assert(key_arg != NULL);
  Type* key_arg_type = key_arg->var_value()->type();

  Type* uintptr_type = Type::lookup_integer_type("uintptr");

  // Get a 0.
  mpz_t ival;
  mpz_init_set_ui(ival, 0);
  Expression* zero = Expression::make_integer(&ival, uintptr_type, bloc);
  mpz_clear(ival);

  // Make a temporary to hold the return value, initialized to 0.
  Temporary_statement* retval = Statement::make_temporary(uintptr_type, zero,
							  bloc);
  gogo->add_statement(retval);

  // Make a temporary to hold the key as a uintptr.
  Expression* ref = Expression::make_var_reference(key_arg, bloc);
  ref = Expression::make_cast(uintptr_type, ref, bloc);
  Temporary_statement* key = Statement::make_temporary(uintptr_type, ref,
						       bloc);
  gogo->add_statement(key);

  // Loop over the struct fields.
  bool first = true;
  const Struct_field_list* fields = this->fields_;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf)
    {
      if (Gogo::is_sink_name(pf->field_name()))
	continue;

      if (first)
	first = false;
      else
	{
	  // Multiply retval by 33.
	  mpz_init_set_ui(ival, 33);
	  Expression* i33 = Expression::make_integer(&ival, uintptr_type,
						     bloc);
	  mpz_clear(ival);

	  ref = Expression::make_temporary_reference(retval, bloc);
	  Statement* s = Statement::make_assignment_operation(OPERATOR_MULTEQ,
							      ref, i33, bloc);
	  gogo->add_statement(s);
	}

      // Get a pointer to the value of this field.
      Expression* offset = Expression::make_struct_field_offset(this, &*pf);
      ref = Expression::make_temporary_reference(key, bloc);
      Expression* subkey = Expression::make_binary(OPERATOR_PLUS, ref, offset,
						   bloc);
      subkey = Expression::make_cast(key_arg_type, subkey, bloc);

      // Get the size of this field.
      Expression* size = Expression::make_type_info(pf->type(),
						    Expression::TYPE_INFO_SIZE);

      // Get the hash function to use for the type of this field.
      Named_object* hash_fn;
      Named_object* equal_fn;
      pf->type()->type_functions(gogo, pf->type()->named_type(), hash_fntype,
				 equal_fntype, &hash_fn, &equal_fn);

      // Call the hash function for the field.
      Expression_list* args = new Expression_list();
      args->push_back(subkey);
      args->push_back(size);
      Expression* func = Expression::make_func_reference(hash_fn, NULL, bloc);
      Expression* call = Expression::make_call(func, args, false, bloc);

      // Add the field's hash value to retval.
      Temporary_reference_expression* tref =
	Expression::make_temporary_reference(retval, bloc);
      tref->set_is_lvalue();
      Statement* s = Statement::make_assignment_operation(OPERATOR_PLUSEQ,
							  tref, call, bloc);
      gogo->add_statement(s);
    }

  // Return retval to the caller of the hash function.
  Expression_list* vals = new Expression_list();
  ref = Expression::make_temporary_reference(retval, bloc);
  vals->push_back(ref);
  Statement* s = Statement::make_return_statement(vals, bloc);
  gogo->add_statement(s);
}

// Write the equality function for a struct which can not use the
// identity function.

void
Struct_type::write_equal_function(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  // The pointers to the structs we are going to compare.
  Named_object* key1_arg = gogo->lookup("key1", NULL);
  Named_object* key2_arg = gogo->lookup("key2", NULL);
  go_assert(key1_arg != NULL && key2_arg != NULL);

  // Build temporaries with the right types.
  Type* pt = Type::make_pointer_type(name != NULL
				     ? static_cast<Type*>(name)
				     : static_cast<Type*>(this));

  Expression* ref = Expression::make_var_reference(key1_arg, bloc);
  ref = Expression::make_unsafe_cast(pt, ref, bloc);
  Temporary_statement* p1 = Statement::make_temporary(pt, ref, bloc);
  gogo->add_statement(p1);

  ref = Expression::make_var_reference(key2_arg, bloc);
  ref = Expression::make_unsafe_cast(pt, ref, bloc);
  Temporary_statement* p2 = Statement::make_temporary(pt, ref, bloc);
  gogo->add_statement(p2);

  const Struct_field_list* fields = this->fields_;
  unsigned int field_index = 0;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf, ++field_index)
    {
      if (Gogo::is_sink_name(pf->field_name()))
	continue;

      // Compare one field in both P1 and P2.
      Expression* f1 = Expression::make_temporary_reference(p1, bloc);
      f1 = Expression::make_unary(OPERATOR_MULT, f1, bloc);
      f1 = Expression::make_field_reference(f1, field_index, bloc);

      Expression* f2 = Expression::make_temporary_reference(p2, bloc);
      f2 = Expression::make_unary(OPERATOR_MULT, f2, bloc);
      f2 = Expression::make_field_reference(f2, field_index, bloc);

      Expression* cond = Expression::make_binary(OPERATOR_NOTEQ, f1, f2, bloc);

      // If the values are not equal, return false.
      gogo->start_block(bloc);
      Expression_list* vals = new Expression_list();
      vals->push_back(Expression::make_boolean(false, bloc));
      Statement* s = Statement::make_return_statement(vals, bloc);
      gogo->add_statement(s);
      Block* then_block = gogo->finish_block(bloc);

      s = Statement::make_if_statement(cond, then_block, NULL, bloc);
      gogo->add_statement(s);
    }

  // All the fields are equal, so return true.
  Expression_list* vals = new Expression_list();
  vals->push_back(Expression::make_boolean(true, bloc));
  Statement* s = Statement::make_return_statement(vals, bloc);
  gogo->add_statement(s);
}

// Reflection string.

void
Struct_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  ret->append("struct {");

  for (Struct_field_list::const_iterator p = this->fields_->begin();
       p != this->fields_->end();
       ++p)
    {
      if (p != this->fields_->begin())
	ret->push_back(';');
      ret->push_back(' ');
      if (p->is_anonymous())
	ret->push_back('?');
      else
	ret->append(Gogo::unpack_hidden_name(p->field_name()));
      ret->push_back(' ');
      this->append_reflection(p->type(), gogo, ret);

      if (p->has_tag())
	{
	  const std::string& tag(p->tag());
	  ret->append(" \"");
	  for (std::string::const_iterator p = tag.begin();
	       p != tag.end();
	       ++p)
	    {
	      if (*p == '\0')
		ret->append("\\x00");
	      else if (*p == '\n')
		ret->append("\\n");
	      else if (*p == '\t')
		ret->append("\\t");
	      else if (*p == '"')
		ret->append("\\\"");
	      else if (*p == '\\')
		ret->append("\\\\");
	      else
		ret->push_back(*p);
	    }
	  ret->push_back('"');
	}
    }

  if (!this->fields_->empty())
    ret->push_back(' ');

  ret->push_back('}');
}

// Mangled name.

void
Struct_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  ret->push_back('S');

  const Struct_field_list* fields = this->fields_;
  if (fields != NULL)
    {
      for (Struct_field_list::const_iterator p = fields->begin();
	   p != fields->end();
	   ++p)
	{
	  if (p->is_anonymous())
	    ret->append("0_");
	  else
	    {
	      std::string n = Gogo::unpack_hidden_name(p->field_name());
	      char buf[20];
	      snprintf(buf, sizeof buf, "%u_",
		       static_cast<unsigned int>(n.length()));
	      ret->append(buf);
	      ret->append(n);
	    }
	  this->append_mangled_name(p->type(), gogo, ret);
	  if (p->has_tag())
	    {
	      const std::string& tag(p->tag());
	      std::string out;
	      for (std::string::const_iterator p = tag.begin();
		   p != tag.end();
		   ++p)
		{
		  if (ISALNUM(*p) || *p == '_')
		    out.push_back(*p);
		  else
		    {
		      char buf[20];
		      snprintf(buf, sizeof buf, ".%x.",
			       static_cast<unsigned int>(*p));
		      out.append(buf);
		    }
		}
	      char buf[20];
	      snprintf(buf, sizeof buf, "T%u_",
		       static_cast<unsigned int>(out.length()));
	      ret->append(buf);
	      ret->append(out);
	    }
	}
    }

  ret->push_back('e');
}

// If the offset of field INDEX in the backend implementation can be
// determined, set *POFFSET to the offset in bytes and return true.
// Otherwise, return false.

bool
Struct_type::backend_field_offset(Gogo* gogo, unsigned int index,
				  unsigned int* poffset)
{
  if (!this->is_backend_type_size_known(gogo))
    return false;
  Btype* bt = this->get_backend_placeholder(gogo);
  size_t offset = gogo->backend()->type_field_offset(bt, index);
  *poffset = static_cast<unsigned int>(offset);
  if (*poffset != offset)
    return false;
  return true;
}

// Export.

void
Struct_type::do_export(Export* exp) const
{
  exp->write_c_string("struct { ");
  const Struct_field_list* fields = this->fields_;
  go_assert(fields != NULL);
  for (Struct_field_list::const_iterator p = fields->begin();
       p != fields->end();
       ++p)
    {
      if (p->is_anonymous())
	exp->write_string("? ");
      else
	{
	  exp->write_string(p->field_name());
	  exp->write_c_string(" ");
	}
      exp->write_type(p->type());

      if (p->has_tag())
	{
	  exp->write_c_string(" ");
	  Expression* expr =
            Expression::make_string(p->tag(), Linemap::predeclared_location());
	  expr->export_expression(exp);
	  delete expr;
	}

      exp->write_c_string("; ");
    }
  exp->write_c_string("}");
}

// Import.

Struct_type*
Struct_type::do_import(Import* imp)
{
  imp->require_c_string("struct { ");
  Struct_field_list* fields = new Struct_field_list;
  if (imp->peek_char() != '}')
    {
      while (true)
	{
	  std::string name;
	  if (imp->match_c_string("? "))
	    imp->advance(2);
	  else
	    {
	      name = imp->read_identifier();
	      imp->require_c_string(" ");
	    }
	  Type* ftype = imp->read_type();

	  Struct_field sf(Typed_identifier(name, ftype, imp->location()));

	  if (imp->peek_char() == ' ')
	    {
	      imp->advance(1);
	      Expression* expr = Expression::import_expression(imp);
	      String_expression* sexpr = expr->string_expression();
	      go_assert(sexpr != NULL);
	      sf.set_tag(sexpr->val());
	      delete sexpr;
	    }

	  imp->require_c_string("; ");
	  fields->push_back(sf);
	  if (imp->peek_char() == '}')
	    break;
	}
    }
  imp->require_c_string("}");

  return Type::make_struct_type(fields, imp->location());
}

// Make a struct type.

Struct_type*
Type::make_struct_type(Struct_field_list* fields,
		       Location location)
{
  return new Struct_type(fields, location);
}

// Class Array_type.

// Whether two array types are identical.

bool
Array_type::is_identical(const Array_type* t, bool errors_are_identical) const
{
  if (!Type::are_identical(this->element_type(), t->element_type(),
			   errors_are_identical, NULL))
    return false;

  Expression* l1 = this->length();
  Expression* l2 = t->length();

  // Slices of the same element type are identical.
  if (l1 == NULL && l2 == NULL)
    return true;

  // Arrays of the same element type are identical if they have the
  // same length.
  if (l1 != NULL && l2 != NULL)
    {
      if (l1 == l2)
	return true;

      // Try to determine the lengths.  If we can't, assume the arrays
      // are not identical.
      bool ret = false;
      Numeric_constant nc1, nc2;
      if (l1->numeric_constant_value(&nc1)
	  && l2->numeric_constant_value(&nc2))
	{
	  mpz_t v1;
	  if (nc1.to_int(&v1))
	    {
	      mpz_t v2;
	      if (nc2.to_int(&v2))
		{
		  ret = mpz_cmp(v1, v2) == 0;
		  mpz_clear(v2);
		}
	      mpz_clear(v1);
	    }
	}
      return ret;
    }

  // Otherwise the arrays are not identical.
  return false;
}

// Traversal.

int
Array_type::do_traverse(Traverse* traverse)
{
  if (Type::traverse(this->element_type_, traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  if (this->length_ != NULL
      && Expression::traverse(&this->length_, traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  return TRAVERSE_CONTINUE;
}

// Check that the length is valid.

bool
Array_type::verify_length()
{
  if (this->length_ == NULL)
    return true;

  Type_context context(Type::lookup_integer_type("int"), false);
  this->length_->determine_type(&context);

  if (!this->length_->is_constant())
    {
      error_at(this->length_->location(), "array bound is not constant");
      return false;
    }

  Numeric_constant nc;
  if (!this->length_->numeric_constant_value(&nc))
    {
      if (this->length_->type()->integer_type() != NULL
	  || this->length_->type()->float_type() != NULL)
	error_at(this->length_->location(), "array bound is not constant");
      else
	error_at(this->length_->location(), "array bound is not numeric");
      return false;
    }

  unsigned long val;
  switch (nc.to_unsigned_long(&val))
    {
    case Numeric_constant::NC_UL_VALID:
      break;
    case Numeric_constant::NC_UL_NOTINT:
      error_at(this->length_->location(), "array bound truncated to integer");
      return false;
    case Numeric_constant::NC_UL_NEGATIVE:
      error_at(this->length_->location(), "negative array bound");
      return false;
    case Numeric_constant::NC_UL_BIG:
      error_at(this->length_->location(), "array bound overflows");
      return false;
    default:
      go_unreachable();
    }

  Type* int_type = Type::lookup_integer_type("int");
  unsigned int tbits = int_type->integer_type()->bits();
  if (sizeof(val) <= tbits * 8
      && val >> (tbits - 1) != 0)
    {
      error_at(this->length_->location(), "array bound overflows");
      return false;
    }

  return true;
}

// Verify the type.

bool
Array_type::do_verify()
{
  if (!this->verify_length())
    this->length_ = Expression::make_error(this->length_->location());
  return true;
}

// Whether we can use memcmp to compare this array.

bool
Array_type::do_compare_is_identity(Gogo* gogo)
{
  if (this->length_ == NULL)
    return false;

  // Check for [...], which indicates that this is not a real type.
  if (this->length_->is_nil_expression())
    return false;

  if (!this->element_type_->compare_is_identity(gogo))
    return false;

  // If there is any padding, then we can't use memcmp.
  unsigned int size;
  unsigned int align;
  if (!this->element_type_->backend_type_size(gogo, &size)
      || !this->element_type_->backend_type_align(gogo, &align))
    return false;
  if ((size & (align - 1)) != 0)
    return false;

  return true;
}

// Array type hash code.

unsigned int
Array_type::do_hash_for_method(Gogo* gogo) const
{
  // There is no very convenient way to get a hash code for the
  // length.
  return this->element_type_->hash_for_method(gogo) + 1;
}

// Write the hash function for an array which can not use the identify
// function.

void
Array_type::write_hash_function(Gogo* gogo, Named_type* name,
				Function_type* hash_fntype,
				Function_type* equal_fntype)
{
  Location bloc = Linemap::predeclared_location();

  // The pointer to the array that we are going to hash.  This is an
  // argument to the hash function we are implementing here.
  Named_object* key_arg = gogo->lookup("key", NULL);
  go_assert(key_arg != NULL);
  Type* key_arg_type = key_arg->var_value()->type();

  Type* uintptr_type = Type::lookup_integer_type("uintptr");

  // Get a 0.
  mpz_t ival;
  mpz_init_set_ui(ival, 0);
  Expression* zero = Expression::make_integer(&ival, uintptr_type, bloc);
  mpz_clear(ival);

  // Make a temporary to hold the return value, initialized to 0.
  Temporary_statement* retval = Statement::make_temporary(uintptr_type, zero,
							  bloc);
  gogo->add_statement(retval);

  // Make a temporary to hold the key as a uintptr.
  Expression* ref = Expression::make_var_reference(key_arg, bloc);
  ref = Expression::make_cast(uintptr_type, ref, bloc);
  Temporary_statement* key = Statement::make_temporary(uintptr_type, ref,
						       bloc);
  gogo->add_statement(key);

  // Loop over the array elements.
  // for i = range a
  Type* int_type = Type::lookup_integer_type("int");
  Temporary_statement* index = Statement::make_temporary(int_type, NULL, bloc);
  gogo->add_statement(index);

  Expression* iref = Expression::make_temporary_reference(index, bloc);
  Expression* aref = Expression::make_var_reference(key_arg, bloc);
  Type* pt = Type::make_pointer_type(name != NULL
				     ? static_cast<Type*>(name)
				     : static_cast<Type*>(this));
  aref = Expression::make_cast(pt, aref, bloc);
  For_range_statement* for_range = Statement::make_for_range_statement(iref,
								       NULL,
								       aref,
								       bloc);

  gogo->start_block(bloc);

  // Multiply retval by 33.
  mpz_init_set_ui(ival, 33);
  Expression* i33 = Expression::make_integer(&ival, uintptr_type, bloc);
  mpz_clear(ival);

  ref = Expression::make_temporary_reference(retval, bloc);
  Statement* s = Statement::make_assignment_operation(OPERATOR_MULTEQ, ref,
						      i33, bloc);
  gogo->add_statement(s);

  // Get the hash function for the element type.
  Named_object* hash_fn;
  Named_object* equal_fn;
  this->element_type_->type_functions(gogo, this->element_type_->named_type(),
				      hash_fntype, equal_fntype, &hash_fn,
				      &equal_fn);

  // Get a pointer to this element in the loop.
  Expression* subkey = Expression::make_temporary_reference(key, bloc);
  subkey = Expression::make_cast(key_arg_type, subkey, bloc);

  // Get the size of each element.
  Expression* ele_size = Expression::make_type_info(this->element_type_,
						    Expression::TYPE_INFO_SIZE);

  // Get the hash of this element.
  Expression_list* args = new Expression_list();
  args->push_back(subkey);
  args->push_back(ele_size);
  Expression* func = Expression::make_func_reference(hash_fn, NULL, bloc);
  Expression* call = Expression::make_call(func, args, false, bloc);

  // Add the element's hash value to retval.
  Temporary_reference_expression* tref =
    Expression::make_temporary_reference(retval, bloc);
  tref->set_is_lvalue();
  s = Statement::make_assignment_operation(OPERATOR_PLUSEQ, tref, call, bloc);
  gogo->add_statement(s);

  // Increase the element pointer.
  tref = Expression::make_temporary_reference(key, bloc);
  tref->set_is_lvalue();
  s = Statement::make_assignment_operation(OPERATOR_PLUSEQ, tref, ele_size,
					   bloc);

  Block* statements = gogo->finish_block(bloc);

  for_range->add_statements(statements);
  gogo->add_statement(for_range);

  // Return retval to the caller of the hash function.
  Expression_list* vals = new Expression_list();
  ref = Expression::make_temporary_reference(retval, bloc);
  vals->push_back(ref);
  s = Statement::make_return_statement(vals, bloc);
  gogo->add_statement(s);
}

// Write the equality function for an array which can not use the
// identity function.

void
Array_type::write_equal_function(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  // The pointers to the arrays we are going to compare.
  Named_object* key1_arg = gogo->lookup("key1", NULL);
  Named_object* key2_arg = gogo->lookup("key2", NULL);
  go_assert(key1_arg != NULL && key2_arg != NULL);

  // Build temporaries for the keys with the right types.
  Type* pt = Type::make_pointer_type(name != NULL
				     ? static_cast<Type*>(name)
				     : static_cast<Type*>(this));

  Expression* ref = Expression::make_var_reference(key1_arg, bloc);
  ref = Expression::make_unsafe_cast(pt, ref, bloc);
  Temporary_statement* p1 = Statement::make_temporary(pt, ref, bloc);
  gogo->add_statement(p1);

  ref = Expression::make_var_reference(key2_arg, bloc);
  ref = Expression::make_unsafe_cast(pt, ref, bloc);
  Temporary_statement* p2 = Statement::make_temporary(pt, ref, bloc);
  gogo->add_statement(p2);

  // Loop over the array elements.
  // for i = range a
  Type* int_type = Type::lookup_integer_type("int");
  Temporary_statement* index = Statement::make_temporary(int_type, NULL, bloc);
  gogo->add_statement(index);

  Expression* iref = Expression::make_temporary_reference(index, bloc);
  Expression* aref = Expression::make_temporary_reference(p1, bloc);
  For_range_statement* for_range = Statement::make_for_range_statement(iref,
								       NULL,
								       aref,
								       bloc);

  gogo->start_block(bloc);

  // Compare element in P1 and P2.
  Expression* e1 = Expression::make_temporary_reference(p1, bloc);
  e1 = Expression::make_unary(OPERATOR_MULT, e1, bloc);
  ref = Expression::make_temporary_reference(index, bloc);
  e1 = Expression::make_array_index(e1, ref, NULL, bloc);

  Expression* e2 = Expression::make_temporary_reference(p2, bloc);
  e2 = Expression::make_unary(OPERATOR_MULT, e2, bloc);
  ref = Expression::make_temporary_reference(index, bloc);
  e2 = Expression::make_array_index(e2, ref, NULL, bloc);

  Expression* cond = Expression::make_binary(OPERATOR_NOTEQ, e1, e2, bloc);

  // If the elements are not equal, return false.
  gogo->start_block(bloc);
  Expression_list* vals = new Expression_list();
  vals->push_back(Expression::make_boolean(false, bloc));
  Statement* s = Statement::make_return_statement(vals, bloc);
  gogo->add_statement(s);
  Block* then_block = gogo->finish_block(bloc);

  s = Statement::make_if_statement(cond, then_block, NULL, bloc);
  gogo->add_statement(s);

  Block* statements = gogo->finish_block(bloc);

  for_range->add_statements(statements);
  gogo->add_statement(for_range);

  // All the elements are equal, so return true.
  vals = new Expression_list();
  vals->push_back(Expression::make_boolean(true, bloc));
  s = Statement::make_return_statement(vals, bloc);
  gogo->add_statement(s);
}

// Get a tree for the length of a fixed array.  The length may be
// computed using a function call, so we must only evaluate it once.

tree
Array_type::get_length_tree(Gogo* gogo)
{
  go_assert(this->length_ != NULL);
  if (this->length_tree_ == NULL_TREE)
    {
      Numeric_constant nc;
      mpz_t val;
      if (this->length_->numeric_constant_value(&nc) && nc.to_int(&val))
	{
	  if (mpz_sgn(val) < 0)
	    {
	      this->length_tree_ = error_mark_node;
	      return this->length_tree_;
	    }
	  Type* t = nc.type();
	  if (t == NULL)
	    t = Type::lookup_integer_type("int");
	  else if (t->is_abstract())
	    t = t->make_non_abstract_type();
	  tree tt = type_to_tree(t->get_backend(gogo));
	  this->length_tree_ = Expression::integer_constant_tree(val, tt);
	  mpz_clear(val);
	}
      else
	{
	  // Make up a translation context for the array length
	  // expression.  FIXME: This won't work in general.
	  Translate_context context(gogo, NULL, NULL, NULL);
	  tree len = this->length_->get_tree(&context);
	  if (len != error_mark_node)
	    {
	      Type* int_type = Type::lookup_integer_type("int");
	      tree int_type_tree = type_to_tree(int_type->get_backend(gogo));
	      len = convert_to_integer(int_type_tree, len);
	      len = save_expr(len);
	    }
	  this->length_tree_ = len;
	}
    }
  return this->length_tree_;
}

// Get the backend representation of the fields of a slice.  This is
// not declared in types.h so that types.h doesn't have to #include
// backend.h.
//
// We use int for the count and capacity fields.  This matches 6g.
// The language more or less assumes that we can't allocate space of a
// size which does not fit in int.

static void
get_backend_slice_fields(Gogo* gogo, Array_type* type, bool use_placeholder,
			 std::vector<Backend::Btyped_identifier>* bfields)
{
  bfields->resize(3);

  Type* pet = Type::make_pointer_type(type->element_type());
  Btype* pbet = (use_placeholder
		 ? pet->get_backend_placeholder(gogo)
		 : pet->get_backend(gogo));
  Location ploc = Linemap::predeclared_location();

  Backend::Btyped_identifier* p = &(*bfields)[0];
  p->name = "__values";
  p->btype = pbet;
  p->location = ploc;

  Type* int_type = Type::lookup_integer_type("int");

  p = &(*bfields)[1];
  p->name = "__count";
  p->btype = int_type->get_backend(gogo);
  p->location = ploc;

  p = &(*bfields)[2];
  p->name = "__capacity";
  p->btype = int_type->get_backend(gogo);
  p->location = ploc;
}

// Get a tree for the type of this array.  A fixed array is simply
// represented as ARRAY_TYPE with the appropriate index--i.e., it is
// just like an array in C.  An open array is a struct with three
// fields: a data pointer, the length, and the capacity.

Btype*
Array_type::do_get_backend(Gogo* gogo)
{
  if (this->length_ == NULL)
    {
      std::vector<Backend::Btyped_identifier> bfields;
      get_backend_slice_fields(gogo, this, false, &bfields);
      return gogo->backend()->struct_type(bfields);
    }
  else
    {
      Btype* element = this->get_backend_element(gogo, false);
      Bexpression* len = this->get_backend_length(gogo);
      return gogo->backend()->array_type(element, len);
    }
}

// Return the backend representation of the element type.

Btype*
Array_type::get_backend_element(Gogo* gogo, bool use_placeholder)
{
  if (use_placeholder)
    return this->element_type_->get_backend_placeholder(gogo);
  else
    return this->element_type_->get_backend(gogo);
}

// Return the backend representation of the length.

Bexpression*
Array_type::get_backend_length(Gogo* gogo)
{
  return tree_to_expr(this->get_length_tree(gogo));
}

// Finish backend representation of the array.

void
Array_type::finish_backend_element(Gogo* gogo)
{
  Type* et = this->array_type()->element_type();
  et->get_backend(gogo);
  if (this->is_slice_type())
    {
      // This relies on the fact that we always use the same
      // structure for a pointer to any given type.
      Type* pet = Type::make_pointer_type(et);
      pet->get_backend(gogo);
    }
}

// Return a tree for a pointer to the values in ARRAY.

tree
Array_type::value_pointer_tree(Gogo*, tree array) const
{
  tree ret;
  if (this->length() != NULL)
    {
      // Fixed array.
      ret = fold_convert(build_pointer_type(TREE_TYPE(TREE_TYPE(array))),
			 build_fold_addr_expr(array));
    }
  else
    {
      // Open array.
      tree field = TYPE_FIELDS(TREE_TYPE(array));
      go_assert(strcmp(IDENTIFIER_POINTER(DECL_NAME(field)),
			"__values") == 0);
      ret = fold_build3(COMPONENT_REF, TREE_TYPE(field), array, field,
			NULL_TREE);
    }
  if (TREE_CONSTANT(array))
    TREE_CONSTANT(ret) = 1;
  return ret;
}

// Return a tree for the length of the array ARRAY which has this
// type.

tree
Array_type::length_tree(Gogo* gogo, tree array)
{
  if (this->length_ != NULL)
    {
      if (TREE_CODE(array) == SAVE_EXPR)
	return this->get_length_tree(gogo);
      else
	{
	  tree len = this->get_length_tree(gogo);
	  return omit_one_operand(TREE_TYPE(len), len, array);
	}
    }

  // This is an open array.  We need to read the length field.

  tree type = TREE_TYPE(array);
  go_assert(TREE_CODE(type) == RECORD_TYPE);

  tree field = DECL_CHAIN(TYPE_FIELDS(type));
  go_assert(strcmp(IDENTIFIER_POINTER(DECL_NAME(field)), "__count") == 0);

  tree ret = build3(COMPONENT_REF, TREE_TYPE(field), array, field, NULL_TREE);
  if (TREE_CONSTANT(array))
    TREE_CONSTANT(ret) = 1;
  return ret;
}

// Return a tree for the capacity of the array ARRAY which has this
// type.

tree
Array_type::capacity_tree(Gogo* gogo, tree array)
{
  if (this->length_ != NULL)
    {
      tree len = this->get_length_tree(gogo);
      return omit_one_operand(TREE_TYPE(len), len, array);
    }

  // This is an open array.  We need to read the capacity field.

  tree type = TREE_TYPE(array);
  go_assert(TREE_CODE(type) == RECORD_TYPE);

  tree field = DECL_CHAIN(DECL_CHAIN(TYPE_FIELDS(type)));
  go_assert(strcmp(IDENTIFIER_POINTER(DECL_NAME(field)), "__capacity") == 0);

  return build3(COMPONENT_REF, TREE_TYPE(field), array, field, NULL_TREE);
}

// Export.

void
Array_type::do_export(Export* exp) const
{
  exp->write_c_string("[");
  if (this->length_ != NULL)
    this->length_->export_expression(exp);
  exp->write_c_string("] ");
  exp->write_type(this->element_type_);
}

// Import.

Array_type*
Array_type::do_import(Import* imp)
{
  imp->require_c_string("[");
  Expression* length;
  if (imp->peek_char() == ']')
    length = NULL;
  else
    length = Expression::import_expression(imp);
  imp->require_c_string("] ");
  Type* element_type = imp->read_type();
  return Type::make_array_type(element_type, length);
}

// The type of an array type descriptor.

Type*
Array_type::make_array_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Type* uintptr_type = Type::lookup_integer_type("uintptr");

      Struct_type* sf =
	Type::make_builtin_struct_type(4,
				       "", tdt,
				       "elem", ptdt,
				       "slice", ptdt,
				       "len", uintptr_type);

      ret = Type::make_builtin_named_type("ArrayType", sf);
    }

  return ret;
}

// The type of an slice type descriptor.

Type*
Array_type::make_slice_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Struct_type* sf =
	Type::make_builtin_struct_type(2,
				       "", tdt,
				       "elem", ptdt);

      ret = Type::make_builtin_named_type("SliceType", sf);
    }

  return ret;
}

// Build a type descriptor for an array/slice type.

Expression*
Array_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  if (this->length_ != NULL)
    return this->array_type_descriptor(gogo, name);
  else
    return this->slice_type_descriptor(gogo, name);
}

// Build a type descriptor for an array type.

Expression*
Array_type::array_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  Type* atdt = Array_type::make_array_type_descriptor_type();

  const Struct_field_list* fields = atdt->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(3);

  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("commonType"));
  vals->push_back(this->type_descriptor_constructor(gogo,
						    RUNTIME_TYPE_KIND_ARRAY,
						    name, NULL, true));

  ++p;
  go_assert(p->is_field_name("elem"));
  vals->push_back(Expression::make_type_descriptor(this->element_type_, bloc));

  ++p;
  go_assert(p->is_field_name("slice"));
  Type* slice_type = Type::make_array_type(this->element_type_, NULL);
  vals->push_back(Expression::make_type_descriptor(slice_type, bloc));

  ++p;
  go_assert(p->is_field_name("len"));
  vals->push_back(Expression::make_cast(p->type(), this->length_, bloc));

  ++p;
  go_assert(p == fields->end());

  return Expression::make_struct_composite_literal(atdt, vals, bloc);
}

// Build a type descriptor for a slice type.

Expression*
Array_type::slice_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  Type* stdt = Array_type::make_slice_type_descriptor_type();

  const Struct_field_list* fields = stdt->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(2);

  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("commonType"));
  vals->push_back(this->type_descriptor_constructor(gogo,
						    RUNTIME_TYPE_KIND_SLICE,
						    name, NULL, true));

  ++p;
  go_assert(p->is_field_name("elem"));
  vals->push_back(Expression::make_type_descriptor(this->element_type_, bloc));

  ++p;
  go_assert(p == fields->end());

  return Expression::make_struct_composite_literal(stdt, vals, bloc);
}

// Reflection string.

void
Array_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  ret->push_back('[');
  if (this->length_ != NULL)
    {
      Numeric_constant nc;
      unsigned long val;
      if (!this->length_->numeric_constant_value(&nc)
	  || nc.to_unsigned_long(&val) != Numeric_constant::NC_UL_VALID)
	error_at(this->length_->location(), "invalid array length");
      else
	{
	  char buf[50];
	  snprintf(buf, sizeof buf, "%lu", val);
	  ret->append(buf);
	}
    }
  ret->push_back(']');

  this->append_reflection(this->element_type_, gogo, ret);
}

// Mangled name.

void
Array_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  ret->push_back('A');
  this->append_mangled_name(this->element_type_, gogo, ret);
  if (this->length_ != NULL)
    {
      Numeric_constant nc;
      unsigned long val;
      if (!this->length_->numeric_constant_value(&nc)
	  || nc.to_unsigned_long(&val) != Numeric_constant::NC_UL_VALID)
	error_at(this->length_->location(), "invalid array length");
      else
	{
	  char buf[50];
	  snprintf(buf, sizeof buf, "%lu", val);
	  ret->append(buf);
	}
    }
  ret->push_back('e');
}

// Make an array type.

Array_type*
Type::make_array_type(Type* element_type, Expression* length)
{
  return new Array_type(element_type, length);
}

// Class Map_type.

// Traversal.

int
Map_type::do_traverse(Traverse* traverse)
{
  if (Type::traverse(this->key_type_, traverse) == TRAVERSE_EXIT
      || Type::traverse(this->val_type_, traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  return TRAVERSE_CONTINUE;
}

// Check that the map type is OK.

bool
Map_type::do_verify()
{
  // The runtime support uses "map[void]void".
  if (!this->key_type_->is_comparable() && !this->key_type_->is_void_type())
    error_at(this->location_, "invalid map key type");
  return true;
}

// Whether two map types are identical.

bool
Map_type::is_identical(const Map_type* t, bool errors_are_identical) const
{
  return (Type::are_identical(this->key_type(), t->key_type(),
			      errors_are_identical, NULL)
	  && Type::are_identical(this->val_type(), t->val_type(),
				 errors_are_identical, NULL));
}

// Hash code.

unsigned int
Map_type::do_hash_for_method(Gogo* gogo) const
{
  return (this->key_type_->hash_for_method(gogo)
	  + this->val_type_->hash_for_method(gogo)
	  + 2);
}

// Get the backend representation for a map type.  A map type is
// represented as a pointer to a struct.  The struct is __go_map in
// libgo/map.h.

Btype*
Map_type::do_get_backend(Gogo* gogo)
{
  static Btype* backend_map_type;
  if (backend_map_type == NULL)
    {
      std::vector<Backend::Btyped_identifier> bfields(4);

      Location bloc = Linemap::predeclared_location();

      Type* pdt = Type::make_type_descriptor_ptr_type();
      bfields[0].name = "__descriptor";
      bfields[0].btype = pdt->get_backend(gogo);
      bfields[0].location = bloc;

      Type* uintptr_type = Type::lookup_integer_type("uintptr");
      bfields[1].name = "__element_count";
      bfields[1].btype = uintptr_type->get_backend(gogo);
      bfields[1].location = bloc;

      bfields[2].name = "__bucket_count";
      bfields[2].btype = bfields[1].btype;
      bfields[2].location = bloc;

      Btype* bvt = gogo->backend()->void_type();
      Btype* bpvt = gogo->backend()->pointer_type(bvt);
      Btype* bppvt = gogo->backend()->pointer_type(bpvt);
      bfields[3].name = "__buckets";
      bfields[3].btype = bppvt;
      bfields[3].location = bloc;

      Btype *bt = gogo->backend()->struct_type(bfields);
      bt = gogo->backend()->named_type("__go_map", bt, bloc);
      backend_map_type = gogo->backend()->pointer_type(bt);
    }
  return backend_map_type;
}

// The type of a map type descriptor.

Type*
Map_type::make_map_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Struct_type* sf =
	Type::make_builtin_struct_type(3,
				       "", tdt,
				       "key", ptdt,
				       "elem", ptdt);

      ret = Type::make_builtin_named_type("MapType", sf);
    }

  return ret;
}

// Build a type descriptor for a map type.

Expression*
Map_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  Type* mtdt = Map_type::make_map_type_descriptor_type();

  const Struct_field_list* fields = mtdt->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(3);

  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("commonType"));
  vals->push_back(this->type_descriptor_constructor(gogo,
						    RUNTIME_TYPE_KIND_MAP,
						    name, NULL, true));

  ++p;
  go_assert(p->is_field_name("key"));
  vals->push_back(Expression::make_type_descriptor(this->key_type_, bloc));

  ++p;
  go_assert(p->is_field_name("elem"));
  vals->push_back(Expression::make_type_descriptor(this->val_type_, bloc));

  ++p;
  go_assert(p == fields->end());

  return Expression::make_struct_composite_literal(mtdt, vals, bloc);
}

// A mapping from map types to map descriptors.

Map_type::Map_descriptors Map_type::map_descriptors;

// Build a map descriptor for this type.  Return a pointer to it.

tree
Map_type::map_descriptor_pointer(Gogo* gogo, Location location)
{
  Bvariable* bvar = this->map_descriptor(gogo);
  tree var_tree = var_to_tree(bvar);
  if (var_tree == error_mark_node)
    return error_mark_node;
  return build_fold_addr_expr_loc(location.gcc_location(), var_tree);
}

// Build a map descriptor for this type.

Bvariable*
Map_type::map_descriptor(Gogo* gogo)
{
  std::pair<Map_type*, Bvariable*> val(this, NULL);
  std::pair<Map_type::Map_descriptors::iterator, bool> ins =
    Map_type::map_descriptors.insert(val);
  if (!ins.second)
    return ins.first->second;

  Type* key_type = this->key_type_;
  Type* val_type = this->val_type_;

  // The map entry type is a struct with three fields.  Build that
  // struct so that we can get the offsets of the key and value within
  // a map entry.  The first field should technically be a pointer to
  // this type itself, but since we only care about field offsets we
  // just use pointer to bool.
  Type* pbool = Type::make_pointer_type(Type::make_boolean_type());
  Struct_type* map_entry_type =
    Type::make_builtin_struct_type(3,
				   "__next", pbool,
				   "__key", key_type,
				   "__val", val_type);

  Type* map_descriptor_type = Map_type::make_map_descriptor_type();

  const Struct_field_list* fields =
    map_descriptor_type->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(4);

  Location bloc = Linemap::predeclared_location();

  Struct_field_list::const_iterator p = fields->begin();

  go_assert(p->is_field_name("__map_descriptor"));
  vals->push_back(Expression::make_type_descriptor(this, bloc));

  ++p;
  go_assert(p->is_field_name("__entry_size"));
  Expression::Type_info type_info = Expression::TYPE_INFO_SIZE;
  vals->push_back(Expression::make_type_info(map_entry_type, type_info));

  Struct_field_list::const_iterator pf = map_entry_type->fields()->begin();
  ++pf;
  go_assert(pf->is_field_name("__key"));

  ++p;
  go_assert(p->is_field_name("__key_offset"));
  vals->push_back(Expression::make_struct_field_offset(map_entry_type, &*pf));

  ++pf;
  go_assert(pf->is_field_name("__val"));

  ++p;
  go_assert(p->is_field_name("__val_offset"));
  vals->push_back(Expression::make_struct_field_offset(map_entry_type, &*pf));

  ++p;
  go_assert(p == fields->end());

  Expression* initializer =
    Expression::make_struct_composite_literal(map_descriptor_type, vals, bloc);

  std::string mangled_name = "__go_map_" + this->mangled_name(gogo);
  Btype* map_descriptor_btype = map_descriptor_type->get_backend(gogo);
  Bvariable* bvar = gogo->backend()->immutable_struct(mangled_name, false,
						      true,
						      map_descriptor_btype,
						      bloc);

  Translate_context context(gogo, NULL, NULL, NULL);
  context.set_is_const();
  Bexpression* binitializer = tree_to_expr(initializer->get_tree(&context));

  gogo->backend()->immutable_struct_set_init(bvar, mangled_name, false, true,
					     map_descriptor_btype, bloc,
					     binitializer);

  ins.first->second = bvar;
  return bvar;
}

// Build the type of a map descriptor.  This must match the struct
// __go_map_descriptor in libgo/runtime/map.h.

Type*
Map_type::make_map_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* ptdt = Type::make_type_descriptor_ptr_type();
      Type* uintptr_type = Type::lookup_integer_type("uintptr");
      Struct_type* sf =
	Type::make_builtin_struct_type(4,
				       "__map_descriptor", ptdt,
				       "__entry_size", uintptr_type,
				       "__key_offset", uintptr_type,
				       "__val_offset", uintptr_type);
      ret = Type::make_builtin_named_type("__go_map_descriptor", sf);
    }
  return ret;
}

// Reflection string for a map.

void
Map_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  ret->append("map[");
  this->append_reflection(this->key_type_, gogo, ret);
  ret->append("]");
  this->append_reflection(this->val_type_, gogo, ret);
}

// Mangled name for a map.

void
Map_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  ret->push_back('M');
  this->append_mangled_name(this->key_type_, gogo, ret);
  ret->append("__");
  this->append_mangled_name(this->val_type_, gogo, ret);
}

// Export a map type.

void
Map_type::do_export(Export* exp) const
{
  exp->write_c_string("map [");
  exp->write_type(this->key_type_);
  exp->write_c_string("] ");
  exp->write_type(this->val_type_);
}

// Import a map type.

Map_type*
Map_type::do_import(Import* imp)
{
  imp->require_c_string("map [");
  Type* key_type = imp->read_type();
  imp->require_c_string("] ");
  Type* val_type = imp->read_type();
  return Type::make_map_type(key_type, val_type, imp->location());
}

// Make a map type.

Map_type*
Type::make_map_type(Type* key_type, Type* val_type, Location location)
{
  return new Map_type(key_type, val_type, location);
}

// Class Channel_type.

// Hash code.

unsigned int
Channel_type::do_hash_for_method(Gogo* gogo) const
{
  unsigned int ret = 0;
  if (this->may_send_)
    ret += 1;
  if (this->may_receive_)
    ret += 2;
  if (this->element_type_ != NULL)
    ret += this->element_type_->hash_for_method(gogo) << 2;
  return ret << 3;
}

// Whether this type is the same as T.

bool
Channel_type::is_identical(const Channel_type* t,
			   bool errors_are_identical) const
{
  if (!Type::are_identical(this->element_type(), t->element_type(),
			   errors_are_identical, NULL))
    return false;
  return (this->may_send_ == t->may_send_
	  && this->may_receive_ == t->may_receive_);
}

// Return the tree for a channel type.  A channel is a pointer to a
// __go_channel struct.  The __go_channel struct is defined in
// libgo/runtime/channel.h.

Btype*
Channel_type::do_get_backend(Gogo* gogo)
{
  static Btype* backend_channel_type;
  if (backend_channel_type == NULL)
    {
      std::vector<Backend::Btyped_identifier> bfields;
      Btype* bt = gogo->backend()->struct_type(bfields);
      bt = gogo->backend()->named_type("__go_channel", bt,
                                       Linemap::predeclared_location());
      backend_channel_type = gogo->backend()->pointer_type(bt);
    }
  return backend_channel_type;
}

// Build a type descriptor for a channel type.

Type*
Channel_type::make_chan_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Type* uintptr_type = Type::lookup_integer_type("uintptr");

      Struct_type* sf =
	Type::make_builtin_struct_type(3,
				       "", tdt,
				       "elem", ptdt,
				       "dir", uintptr_type);

      ret = Type::make_builtin_named_type("ChanType", sf);
    }

  return ret;
}

// Build a type descriptor for a map type.

Expression*
Channel_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  Type* ctdt = Channel_type::make_chan_type_descriptor_type();

  const Struct_field_list* fields = ctdt->struct_type()->fields();

  Expression_list* vals = new Expression_list();
  vals->reserve(3);

  Struct_field_list::const_iterator p = fields->begin();
  go_assert(p->is_field_name("commonType"));
  vals->push_back(this->type_descriptor_constructor(gogo,
						    RUNTIME_TYPE_KIND_CHAN,
						    name, NULL, true));

  ++p;
  go_assert(p->is_field_name("elem"));
  vals->push_back(Expression::make_type_descriptor(this->element_type_, bloc));

  ++p;
  go_assert(p->is_field_name("dir"));
  // These bits must match the ones in libgo/runtime/go-type.h.
  int val = 0;
  if (this->may_receive_)
    val |= 1;
  if (this->may_send_)
    val |= 2;
  mpz_t iv;
  mpz_init_set_ui(iv, val);
  vals->push_back(Expression::make_integer(&iv, p->type(), bloc));
  mpz_clear(iv);

  ++p;
  go_assert(p == fields->end());

  return Expression::make_struct_composite_literal(ctdt, vals, bloc);
}

// Reflection string.

void
Channel_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  if (!this->may_send_)
    ret->append("<-");
  ret->append("chan");
  if (!this->may_receive_)
    ret->append("<-");
  ret->push_back(' ');
  this->append_reflection(this->element_type_, gogo, ret);
}

// Mangled name.

void
Channel_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  ret->push_back('C');
  this->append_mangled_name(this->element_type_, gogo, ret);
  if (this->may_send_)
    ret->push_back('s');
  if (this->may_receive_)
    ret->push_back('r');
  ret->push_back('e');
}

// Export.

void
Channel_type::do_export(Export* exp) const
{
  exp->write_c_string("chan ");
  if (this->may_send_ && !this->may_receive_)
    exp->write_c_string("-< ");
  else if (this->may_receive_ && !this->may_send_)
    exp->write_c_string("<- ");
  exp->write_type(this->element_type_);
}

// Import.

Channel_type*
Channel_type::do_import(Import* imp)
{
  imp->require_c_string("chan ");

  bool may_send;
  bool may_receive;
  if (imp->match_c_string("-< "))
    {
      imp->advance(3);
      may_send = true;
      may_receive = false;
    }
  else if (imp->match_c_string("<- "))
    {
      imp->advance(3);
      may_receive = true;
      may_send = false;
    }
  else
    {
      may_send = true;
      may_receive = true;
    }

  Type* element_type = imp->read_type();

  return Type::make_channel_type(may_send, may_receive, element_type);
}

// Make a new channel type.

Channel_type*
Type::make_channel_type(bool send, bool receive, Type* element_type)
{
  return new Channel_type(send, receive, element_type);
}

// Class Interface_type.

// Return the list of methods.

const Typed_identifier_list*
Interface_type::methods() const
{
  go_assert(this->methods_are_finalized_ || saw_errors());
  return this->all_methods_;
}

// Return the number of methods.

size_t
Interface_type::method_count() const
{
  go_assert(this->methods_are_finalized_ || saw_errors());
  return this->all_methods_ == NULL ? 0 : this->all_methods_->size();
}

// Traversal.

int
Interface_type::do_traverse(Traverse* traverse)
{
  Typed_identifier_list* methods = (this->methods_are_finalized_
				    ? this->all_methods_
				    : this->parse_methods_);
  if (methods == NULL)
    return TRAVERSE_CONTINUE;
  return methods->traverse(traverse);
}

// Finalize the methods.  This handles interface inheritance.

void
Interface_type::finalize_methods()
{
  if (this->methods_are_finalized_)
    return;
  this->methods_are_finalized_ = true;
  if (this->parse_methods_ == NULL)
    return;

  this->all_methods_ = new Typed_identifier_list();
  this->all_methods_->reserve(this->parse_methods_->size());
  Typed_identifier_list inherit;
  for (Typed_identifier_list::const_iterator pm =
	 this->parse_methods_->begin();
       pm != this->parse_methods_->end();
       ++pm)
    {
      const Typed_identifier* p = &*pm;
      if (p->name().empty())
	inherit.push_back(*p);
      else if (this->find_method(p->name()) == NULL)
	this->all_methods_->push_back(*p);
      else
	error_at(p->location(), "duplicate method %qs",
		 Gogo::message_name(p->name()).c_str());
    }

  std::vector<Named_type*> seen;
  seen.reserve(inherit.size());
  bool issued_recursive_error = false;
  while (!inherit.empty())
    {
      Type* t = inherit.back().type();
      Location tl = inherit.back().location();
      inherit.pop_back();

      Interface_type* it = t->interface_type();
      if (it == NULL)
	{
	  if (!t->is_error())
	    error_at(tl, "interface contains embedded non-interface");
	  continue;
	}
      if (it == this)
	{
	  if (!issued_recursive_error)
	    {
	      error_at(tl, "invalid recursive interface");
	      issued_recursive_error = true;
	    }
	  continue;
	}

      Named_type* nt = t->named_type();
      if (nt != NULL && it->parse_methods_ != NULL)
	{
	  std::vector<Named_type*>::const_iterator q;
	  for (q = seen.begin(); q != seen.end(); ++q)
	    {
	      if (*q == nt)
		{
		  error_at(tl, "inherited interface loop");
		  break;
		}
	    }
	  if (q != seen.end())
	    continue;
	  seen.push_back(nt);
	}

      const Typed_identifier_list* imethods = it->parse_methods_;
      if (imethods == NULL)
	continue;
      for (Typed_identifier_list::const_iterator q = imethods->begin();
	   q != imethods->end();
	   ++q)
	{
	  if (q->name().empty())
	    inherit.push_back(*q);
	  else if (this->find_method(q->name()) == NULL)
	    this->all_methods_->push_back(Typed_identifier(q->name(),
							   q->type(), tl));
	  else
	    error_at(tl, "inherited method %qs is ambiguous",
		     Gogo::message_name(q->name()).c_str());
	}
    }

  if (!this->all_methods_->empty())
    this->all_methods_->sort_by_name();
  else
    {
      delete this->all_methods_;
      this->all_methods_ = NULL;
    }
}

// Return the method NAME, or NULL.

const Typed_identifier*
Interface_type::find_method(const std::string& name) const
{
  go_assert(this->methods_are_finalized_);
  if (this->all_methods_ == NULL)
    return NULL;
  for (Typed_identifier_list::const_iterator p = this->all_methods_->begin();
       p != this->all_methods_->end();
       ++p)
    if (p->name() == name)
      return &*p;
  return NULL;
}

// Return the method index.

size_t
Interface_type::method_index(const std::string& name) const
{
  go_assert(this->methods_are_finalized_ && this->all_methods_ != NULL);
  size_t ret = 0;
  for (Typed_identifier_list::const_iterator p = this->all_methods_->begin();
       p != this->all_methods_->end();
       ++p, ++ret)
    if (p->name() == name)
      return ret;
  go_unreachable();
}

// Return whether NAME is an unexported method, for better error
// reporting.

bool
Interface_type::is_unexported_method(Gogo* gogo, const std::string& name) const
{
  go_assert(this->methods_are_finalized_);
  if (this->all_methods_ == NULL)
    return false;
  for (Typed_identifier_list::const_iterator p = this->all_methods_->begin();
       p != this->all_methods_->end();
       ++p)
    {
      const std::string& method_name(p->name());
      if (Gogo::is_hidden_name(method_name)
	  && name == Gogo::unpack_hidden_name(method_name)
	  && gogo->pack_hidden_name(name, false) != method_name)
	return true;
    }
  return false;
}

// Whether this type is identical with T.

bool
Interface_type::is_identical(const Interface_type* t,
			     bool errors_are_identical) const
{
  // If methods have not been finalized, then we are asking whether
  // func redeclarations are the same.  This is an error, so for
  // simplicity we say they are never the same.
  if (!this->methods_are_finalized_ || !t->methods_are_finalized_)
    return false;

  // We require the same methods with the same types.  The methods
  // have already been sorted.
  if (this->all_methods_ == NULL || t->all_methods_ == NULL)
    return this->all_methods_ == t->all_methods_;

  if (this->assume_identical(this, t) || t->assume_identical(t, this))
    return true;

  Assume_identical* hold_ai = this->assume_identical_;
  Assume_identical ai;
  ai.t1 = this;
  ai.t2 = t;
  ai.next = hold_ai;
  this->assume_identical_ = &ai;

  Typed_identifier_list::const_iterator p1 = this->all_methods_->begin();
  Typed_identifier_list::const_iterator p2;
  for (p2 = t->all_methods_->begin(); p2 != t->all_methods_->end(); ++p1, ++p2)
    {
      if (p1 == this->all_methods_->end())
	break;
      if (p1->name() != p2->name()
	  || !Type::are_identical(p1->type(), p2->type(),
				  errors_are_identical, NULL))
	break;
    }

  this->assume_identical_ = hold_ai;

  return p1 == this->all_methods_->end() && p2 == t->all_methods_->end();
}

// Return true if T1 and T2 are assumed to be identical during a type
// comparison.

bool
Interface_type::assume_identical(const Interface_type* t1,
				 const Interface_type* t2) const
{
  for (Assume_identical* p = this->assume_identical_;
       p != NULL;
       p = p->next)
    if ((p->t1 == t1 && p->t2 == t2) || (p->t1 == t2 && p->t2 == t1))
      return true;
  return false;
}

// Whether we can assign the interface type T to this type.  The types
// are known to not be identical.  An interface assignment is only
// permitted if T is known to implement all methods in THIS.
// Otherwise a type guard is required.

bool
Interface_type::is_compatible_for_assign(const Interface_type* t,
					 std::string* reason) const
{
  go_assert(this->methods_are_finalized_ && t->methods_are_finalized_);
  if (this->all_methods_ == NULL)
    return true;
  for (Typed_identifier_list::const_iterator p = this->all_methods_->begin();
       p != this->all_methods_->end();
       ++p)
    {
      const Typed_identifier* m = t->find_method(p->name());
      if (m == NULL)
	{
	  if (reason != NULL)
	    {
	      char buf[200];
	      snprintf(buf, sizeof buf,
		       _("need explicit conversion; missing method %s%s%s"),
		       open_quote, Gogo::message_name(p->name()).c_str(),
		       close_quote);
	      reason->assign(buf);
	    }
	  return false;
	}

      std::string subreason;
      if (!Type::are_identical(p->type(), m->type(), true, &subreason))
	{
	  if (reason != NULL)
	    {
	      std::string n = Gogo::message_name(p->name());
	      size_t len = 100 + n.length() + subreason.length();
	      char* buf = new char[len];
	      if (subreason.empty())
		snprintf(buf, len, _("incompatible type for method %s%s%s"),
			 open_quote, n.c_str(), close_quote);
	      else
		snprintf(buf, len,
			 _("incompatible type for method %s%s%s (%s)"),
			 open_quote, n.c_str(), close_quote,
			 subreason.c_str());
	      reason->assign(buf);
	      delete[] buf;
	    }
	  return false;
	}
    }

  return true;
}

// Hash code.

unsigned int
Interface_type::do_hash_for_method(Gogo*) const
{
  go_assert(this->methods_are_finalized_);
  unsigned int ret = 0;
  if (this->all_methods_ != NULL)
    {
      for (Typed_identifier_list::const_iterator p =
	     this->all_methods_->begin();
	   p != this->all_methods_->end();
	   ++p)
	{
	  ret = Type::hash_string(p->name(), ret);
	  // We don't use the method type in the hash, to avoid
	  // infinite recursion if an interface method uses a type
	  // which is an interface which inherits from the interface
	  // itself.
	  // type T interface { F() interface {T}}
	  ret <<= 1;
	}
    }
  return ret;
}

// Return true if T implements the interface.  If it does not, and
// REASON is not NULL, set *REASON to a useful error message.

bool
Interface_type::implements_interface(const Type* t, std::string* reason) const
{
  go_assert(this->methods_are_finalized_);
  if (this->all_methods_ == NULL)
    return true;

  bool is_pointer = false;
  const Named_type* nt = t->named_type();
  const Struct_type* st = t->struct_type();
  // If we start with a named type, we don't dereference it to find
  // methods.
  if (nt == NULL)
    {
      const Type* pt = t->points_to();
      if (pt != NULL)
	{
	  // If T is a pointer to a named type, then we need to look at
	  // the type to which it points.
	  is_pointer = true;
	  nt = pt->named_type();
	  st = pt->struct_type();
	}
    }

  // If we have a named type, get the methods from it rather than from
  // any struct type.
  if (nt != NULL)
    st = NULL;

  // Only named and struct types have methods.
  if (nt == NULL && st == NULL)
    {
      if (reason != NULL)
	{
	  if (t->points_to() != NULL
	      && t->points_to()->interface_type() != NULL)
	    reason->assign(_("pointer to interface type has no methods"));
	  else
	    reason->assign(_("type has no methods"));
	}
      return false;
    }

  if (nt != NULL ? !nt->has_any_methods() : !st->has_any_methods())
    {
      if (reason != NULL)
	{
	  if (t->points_to() != NULL
	      && t->points_to()->interface_type() != NULL)
	    reason->assign(_("pointer to interface type has no methods"));
	  else
	    reason->assign(_("type has no methods"));
	}
      return false;
    }

  for (Typed_identifier_list::const_iterator p = this->all_methods_->begin();
       p != this->all_methods_->end();
       ++p)
    {
      bool is_ambiguous = false;
      Method* m = (nt != NULL
		   ? nt->method_function(p->name(), &is_ambiguous)
		   : st->method_function(p->name(), &is_ambiguous));
      if (m == NULL)
	{
	  if (reason != NULL)
	    {
	      std::string n = Gogo::message_name(p->name());
	      size_t len = n.length() + 100;
	      char* buf = new char[len];
	      if (is_ambiguous)
		snprintf(buf, len, _("ambiguous method %s%s%s"),
			 open_quote, n.c_str(), close_quote);
	      else
		snprintf(buf, len, _("missing method %s%s%s"),
			 open_quote, n.c_str(), close_quote);
	      reason->assign(buf);
	      delete[] buf;
	    }
	  return false;
	}

      Function_type *p_fn_type = p->type()->function_type();
      Function_type* m_fn_type = m->type()->function_type();
      go_assert(p_fn_type != NULL && m_fn_type != NULL);
      std::string subreason;
      if (!p_fn_type->is_identical(m_fn_type, true, true, &subreason))
	{
	  if (reason != NULL)
	    {
	      std::string n = Gogo::message_name(p->name());
	      size_t len = 100 + n.length() + subreason.length();
	      char* buf = new char[len];
	      if (subreason.empty())
		snprintf(buf, len, _("incompatible type for method %s%s%s"),
			 open_quote, n.c_str(), close_quote);
	      else
		snprintf(buf, len,
			 _("incompatible type for method %s%s%s (%s)"),
			 open_quote, n.c_str(), close_quote,
			 subreason.c_str());
	      reason->assign(buf);
	      delete[] buf;
	    }
	  return false;
	}

      if (!is_pointer && !m->is_value_method())
	{
	  if (reason != NULL)
	    {
	      std::string n = Gogo::message_name(p->name());
	      size_t len = 100 + n.length();
	      char* buf = new char[len];
	      snprintf(buf, len,
		       _("method %s%s%s requires a pointer receiver"),
		       open_quote, n.c_str(), close_quote);
	      reason->assign(buf);
	      delete[] buf;
	    }
	  return false;
	}

      // If the magic //go:nointerface comment was used, the method
      // may not be used to implement interfaces.
      if (m->nointerface())
	{
	  if (reason != NULL)
	    {
	      std::string n = Gogo::message_name(p->name());
	      size_t len = 100 + n.length();
	      char* buf = new char[len];
	      snprintf(buf, len,
		       _("method %s%s%s is marked go:nointerface"),
		       open_quote, n.c_str(), close_quote);
	      reason->assign(buf);
	      delete[] buf;
	    }
	  return false;
	}
    }

  return true;
}

// Return the backend representation of the empty interface type.  We
// use the same struct for all empty interfaces.

Btype*
Interface_type::get_backend_empty_interface_type(Gogo* gogo)
{
  static Btype* empty_interface_type;
  if (empty_interface_type == NULL)
    {
      std::vector<Backend::Btyped_identifier> bfields(2);

      Location bloc = Linemap::predeclared_location();

      Type* pdt = Type::make_type_descriptor_ptr_type();
      bfields[0].name = "__type_descriptor";
      bfields[0].btype = pdt->get_backend(gogo);
      bfields[0].location = bloc;

      Type* vt = Type::make_pointer_type(Type::make_void_type());
      bfields[1].name = "__object";
      bfields[1].btype = vt->get_backend(gogo);
      bfields[1].location = bloc;

      empty_interface_type = gogo->backend()->struct_type(bfields);
    }
  return empty_interface_type;
}

// Return the fields of a non-empty interface type.  This is not
// declared in types.h so that types.h doesn't have to #include
// backend.h.

static void
get_backend_interface_fields(Gogo* gogo, Interface_type* type,
			     bool use_placeholder,
			     std::vector<Backend::Btyped_identifier>* bfields)
{
  Location loc = type->location();

  std::vector<Backend::Btyped_identifier> mfields(type->methods()->size() + 1);

  Type* pdt = Type::make_type_descriptor_ptr_type();
  mfields[0].name = "__type_descriptor";
  mfields[0].btype = pdt->get_backend(gogo);
  mfields[0].location = loc;

  std::string last_name = "";
  size_t i = 1;
  for (Typed_identifier_list::const_iterator p = type->methods()->begin();
       p != type->methods()->end();
       ++p, ++i)
    {
      // The type of the method in Go only includes the parameters.
      // The actual method also has a receiver, which is always a
      // pointer.  We need to add that pointer type here in order to
      // generate the correct type for the backend.
      Function_type* ft = p->type()->function_type();
      go_assert(ft->receiver() == NULL);

      const Typed_identifier_list* params = ft->parameters();
      Typed_identifier_list* mparams = new Typed_identifier_list();
      if (params != NULL)
	mparams->reserve(params->size() + 1);
      Type* vt = Type::make_pointer_type(Type::make_void_type());
      mparams->push_back(Typed_identifier("", vt, ft->location()));
      if (params != NULL)
	{
	  for (Typed_identifier_list::const_iterator pp = params->begin();
	       pp != params->end();
	       ++pp)
	    mparams->push_back(*pp);
	}

      Typed_identifier_list* mresults = (ft->results() == NULL
					 ? NULL
					 : ft->results()->copy());
      Function_type* mft = Type::make_function_type(NULL, mparams, mresults,
						    ft->location());

      mfields[i].name = Gogo::unpack_hidden_name(p->name());
      mfields[i].btype = (use_placeholder
			  ? mft->get_backend_placeholder(gogo)
			  : mft->get_backend(gogo));
      mfields[i].location = loc;
      // Sanity check: the names should be sorted.
      go_assert(p->name() > last_name);
      last_name = p->name();
    }

  Btype* methods = gogo->backend()->struct_type(mfields);

  bfields->resize(2);

  (*bfields)[0].name = "__methods";
  (*bfields)[0].btype = gogo->backend()->pointer_type(methods);
  (*bfields)[0].location = loc;

  Type* vt = Type::make_pointer_type(Type::make_void_type());
  (*bfields)[1].name = "__object";
  (*bfields)[1].btype = vt->get_backend(gogo);
  (*bfields)[1].location = Linemap::predeclared_location();
}

// Return a tree for an interface type.  An interface is a pointer to
// a struct.  The struct has three fields.  The first field is a
// pointer to the type descriptor for the dynamic type of the object.
// The second field is a pointer to a table of methods for the
// interface to be used with the object.  The third field is the value
// of the object itself.

Btype*
Interface_type::do_get_backend(Gogo* gogo)
{
  if (this->is_empty())
    return Interface_type::get_backend_empty_interface_type(gogo);
  else
    {
      if (this->interface_btype_ != NULL)
	return this->interface_btype_;
      this->interface_btype_ =
	gogo->backend()->placeholder_struct_type("", this->location_);
      std::vector<Backend::Btyped_identifier> bfields;
      get_backend_interface_fields(gogo, this, false, &bfields);
      if (!gogo->backend()->set_placeholder_struct_type(this->interface_btype_,
							bfields))
	this->interface_btype_ = gogo->backend()->error_type();
      return this->interface_btype_;
    }
}

// Finish the backend representation of the methods.

void
Interface_type::finish_backend_methods(Gogo* gogo)
{
  if (!this->interface_type()->is_empty())
    {
      const Typed_identifier_list* methods = this->methods();
      if (methods != NULL)
	{
	  for (Typed_identifier_list::const_iterator p = methods->begin();
	       p != methods->end();
	       ++p)
	    p->type()->get_backend(gogo);
	}
    }
}

// The type of an interface type descriptor.

Type*
Interface_type::make_interface_type_descriptor_type()
{
  static Type* ret;
  if (ret == NULL)
    {
      Type* tdt = Type::make_type_descriptor_type();
      Type* ptdt = Type::make_type_descriptor_ptr_type();

      Type* string_type = Type::lookup_string_type();
      Type* pointer_string_type = Type::make_pointer_type(string_type);

      Struct_type* sm =
	Type::make_builtin_struct_type(3,
				       "name", pointer_string_type,
				       "pkgPath", pointer_string_type,
				       "typ", ptdt);

      Type* nsm = Type::make_builtin_named_type("imethod", sm);

      Type* slice_nsm = Type::make_array_type(nsm, NULL);

      Struct_type* s = Type::make_builtin_struct_type(2,
						      "", tdt,
						      "methods", slice_nsm);

      ret = Type::make_builtin_named_type("InterfaceType", s);
    }

  return ret;
}

// Build a type descriptor for an interface type.

Expression*
Interface_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location bloc = Linemap::predeclared_location();

  Type* itdt = Interface_type::make_interface_type_descriptor_type();

  const Struct_field_list* ifields = itdt->struct_type()->fields();

  Expression_list* ivals = new Expression_list();
  ivals->reserve(2);

  Struct_field_list::const_iterator pif = ifields->begin();
  go_assert(pif->is_field_name("commonType"));
  const int rt = RUNTIME_TYPE_KIND_INTERFACE;
  ivals->push_back(this->type_descriptor_constructor(gogo, rt, name, NULL,
						     true));

  ++pif;
  go_assert(pif->is_field_name("methods"));

  Expression_list* methods = new Expression_list();
  if (this->all_methods_ != NULL)
    {
      Type* elemtype = pif->type()->array_type()->element_type();

      methods->reserve(this->all_methods_->size());
      for (Typed_identifier_list::const_iterator pm =
	     this->all_methods_->begin();
	   pm != this->all_methods_->end();
	   ++pm)
	{
	  const Struct_field_list* mfields = elemtype->struct_type()->fields();

	  Expression_list* mvals = new Expression_list();
	  mvals->reserve(3);

	  Struct_field_list::const_iterator pmf = mfields->begin();
	  go_assert(pmf->is_field_name("name"));
	  std::string s = Gogo::unpack_hidden_name(pm->name());
	  Expression* e = Expression::make_string(s, bloc);
	  mvals->push_back(Expression::make_unary(OPERATOR_AND, e, bloc));

	  ++pmf;
	  go_assert(pmf->is_field_name("pkgPath"));
	  if (!Gogo::is_hidden_name(pm->name()))
	    mvals->push_back(Expression::make_nil(bloc));
	  else
	    {
	      s = Gogo::hidden_name_pkgpath(pm->name());
	      e = Expression::make_string(s, bloc);
	      mvals->push_back(Expression::make_unary(OPERATOR_AND, e, bloc));
	    }

	  ++pmf;
	  go_assert(pmf->is_field_name("typ"));
	  mvals->push_back(Expression::make_type_descriptor(pm->type(), bloc));

	  ++pmf;
	  go_assert(pmf == mfields->end());

	  e = Expression::make_struct_composite_literal(elemtype, mvals,
							bloc);
	  methods->push_back(e);
	}
    }

  ivals->push_back(Expression::make_slice_composite_literal(pif->type(),
							    methods, bloc));

  ++pif;
  go_assert(pif == ifields->end());

  return Expression::make_struct_composite_literal(itdt, ivals, bloc);
}

// Reflection string.

void
Interface_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  ret->append("interface {");
  const Typed_identifier_list* methods = this->parse_methods_;
  if (methods != NULL)
    {
      ret->push_back(' ');
      for (Typed_identifier_list::const_iterator p = methods->begin();
	   p != methods->end();
	   ++p)
	{
	  if (p != methods->begin())
	    ret->append("; ");
	  if (p->name().empty())
	    this->append_reflection(p->type(), gogo, ret);
	  else
	    {
	      if (!Gogo::is_hidden_name(p->name()))
		ret->append(p->name());
	      else if (gogo->pkgpath_from_option())
		ret->append(p->name().substr(1));
	      else
		{
		  // If no -fgo-pkgpath option, backward compatibility
		  // for how this used to work before -fgo-pkgpath was
		  // introduced.
		  std::string pkgpath = Gogo::hidden_name_pkgpath(p->name());
		  ret->append(pkgpath.substr(pkgpath.find('.') + 1));
		  ret->push_back('.');
		  ret->append(Gogo::unpack_hidden_name(p->name()));
		}
	      std::string sub = p->type()->reflection(gogo);
	      go_assert(sub.compare(0, 4, "func") == 0);
	      sub = sub.substr(4);
	      ret->append(sub);
	    }
	}
      ret->push_back(' ');
    }
  ret->append("}");
}

// Mangled name.

void
Interface_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  go_assert(this->methods_are_finalized_);

  ret->push_back('I');

  const Typed_identifier_list* methods = this->all_methods_;
  if (methods != NULL && !this->seen_)
    {
      this->seen_ = true;
      for (Typed_identifier_list::const_iterator p = methods->begin();
	   p != methods->end();
	   ++p)
	{
	  if (!p->name().empty())
	    {
	      std::string n;
	      if (!Gogo::is_hidden_name(p->name()))
		n = p->name();
	      else
		{
		  n = ".";
		  std::string pkgpath = Gogo::hidden_name_pkgpath(p->name());
		  n.append(Gogo::pkgpath_for_symbol(pkgpath));
		  n.append(1, '.');
		  n.append(Gogo::unpack_hidden_name(p->name()));
		}
	      char buf[20];
	      snprintf(buf, sizeof buf, "%u_",
		       static_cast<unsigned int>(n.length()));
	      ret->append(buf);
	      ret->append(n);
	    }
	  this->append_mangled_name(p->type(), gogo, ret);
	}
      this->seen_ = false;
    }

  ret->push_back('e');
}

// Export.

void
Interface_type::do_export(Export* exp) const
{
  exp->write_c_string("interface { ");

  const Typed_identifier_list* methods = this->parse_methods_;
  if (methods != NULL)
    {
      for (Typed_identifier_list::const_iterator pm = methods->begin();
	   pm != methods->end();
	   ++pm)
	{
	  if (pm->name().empty())
	    {
	      exp->write_c_string("? ");
	      exp->write_type(pm->type());
	    }
	  else
	    {
	      exp->write_string(pm->name());
	      exp->write_c_string(" (");

	      const Function_type* fntype = pm->type()->function_type();

	      bool first = true;
	      const Typed_identifier_list* parameters = fntype->parameters();
	      if (parameters != NULL)
		{
		  bool is_varargs = fntype->is_varargs();
		  for (Typed_identifier_list::const_iterator pp =
			 parameters->begin();
		       pp != parameters->end();
		       ++pp)
		    {
		      if (first)
			first = false;
		      else
			exp->write_c_string(", ");
		      exp->write_name(pp->name());
		      exp->write_c_string(" ");
		      if (!is_varargs || pp + 1 != parameters->end())
			exp->write_type(pp->type());
		      else
			{
			  exp->write_c_string("...");
			  Type *pptype = pp->type();
			  exp->write_type(pptype->array_type()->element_type());
			}
		    }
		}

	      exp->write_c_string(")");

	      const Typed_identifier_list* results = fntype->results();
	      if (results != NULL)
		{
		  exp->write_c_string(" ");
		  if (results->size() == 1 && results->begin()->name().empty())
		    exp->write_type(results->begin()->type());
		  else
		    {
		      first = true;
		      exp->write_c_string("(");
		      for (Typed_identifier_list::const_iterator p =
			     results->begin();
			   p != results->end();
			   ++p)
			{
			  if (first)
			    first = false;
			  else
			    exp->write_c_string(", ");
			  exp->write_name(p->name());
			  exp->write_c_string(" ");
			  exp->write_type(p->type());
			}
		      exp->write_c_string(")");
		    }
		}
	    }

	  exp->write_c_string("; ");
	}
    }

  exp->write_c_string("}");
}

// Import an interface type.

Interface_type*
Interface_type::do_import(Import* imp)
{
  imp->require_c_string("interface { ");

  Typed_identifier_list* methods = new Typed_identifier_list;
  while (imp->peek_char() != '}')
    {
      std::string name = imp->read_identifier();

      if (name == "?")
	{
	  imp->require_c_string(" ");
	  Type* t = imp->read_type();
	  methods->push_back(Typed_identifier("", t, imp->location()));
	  imp->require_c_string("; ");
	  continue;
	}

      imp->require_c_string(" (");

      Typed_identifier_list* parameters;
      bool is_varargs = false;
      if (imp->peek_char() == ')')
	parameters = NULL;
      else
	{
	  parameters = new Typed_identifier_list;
	  while (true)
	    {
	      std::string name = imp->read_name();
	      imp->require_c_string(" ");

	      if (imp->match_c_string("..."))
		{
		  imp->advance(3);
		  is_varargs = true;
		}

	      Type* ptype = imp->read_type();
	      if (is_varargs)
		ptype = Type::make_array_type(ptype, NULL);
	      parameters->push_back(Typed_identifier(name, ptype,
						     imp->location()));
	      if (imp->peek_char() != ',')
		break;
	      go_assert(!is_varargs);
	      imp->require_c_string(", ");
	    }
	}
      imp->require_c_string(")");

      Typed_identifier_list* results;
      if (imp->peek_char() != ' ')
	results = NULL;
      else
	{
	  results = new Typed_identifier_list;
	  imp->advance(1);
	  if (imp->peek_char() != '(')
	    {
	      Type* rtype = imp->read_type();
	      results->push_back(Typed_identifier("", rtype, imp->location()));
	    }
	  else
	    {
	      imp->advance(1);
	      while (true)
		{
		  std::string name = imp->read_name();
		  imp->require_c_string(" ");
		  Type* rtype = imp->read_type();
		  results->push_back(Typed_identifier(name, rtype,
						      imp->location()));
		  if (imp->peek_char() != ',')
		    break;
		  imp->require_c_string(", ");
		}
	      imp->require_c_string(")");
	    }
	}

      Function_type* fntype = Type::make_function_type(NULL, parameters,
						       results,
						       imp->location());
      if (is_varargs)
	fntype->set_is_varargs();
      methods->push_back(Typed_identifier(name, fntype, imp->location()));

      imp->require_c_string("; ");
    }

  imp->require_c_string("}");

  if (methods->empty())
    {
      delete methods;
      methods = NULL;
    }

  return Type::make_interface_type(methods, imp->location());
}

// Make an interface type.

Interface_type*
Type::make_interface_type(Typed_identifier_list* methods,
			  Location location)
{
  return new Interface_type(methods, location);
}

// Make an empty interface type.

Interface_type*
Type::make_empty_interface_type(Location location)
{
  Interface_type* ret = new Interface_type(NULL, location);
  ret->finalize_methods();
  return ret;
}

// Class Method.

// Bind a method to an object.

Expression*
Method::bind_method(Expression* expr, Location location) const
{
  if (this->stub_ == NULL)
    {
      // When there is no stub object, the binding is determined by
      // the child class.
      return this->do_bind_method(expr, location);
    }
  return Expression::make_bound_method(expr, this, this->stub_, location);
}

// Return the named object associated with a method.  This may only be
// called after methods are finalized.

Named_object*
Method::named_object() const
{
  if (this->stub_ != NULL)
    return this->stub_;
  return this->do_named_object();
}

// Class Named_method.

// The type of the method.

Function_type*
Named_method::do_type() const
{
  if (this->named_object_->is_function())
    return this->named_object_->func_value()->type();
  else if (this->named_object_->is_function_declaration())
    return this->named_object_->func_declaration_value()->type();
  else
    go_unreachable();
}

// Return the location of the method receiver.

Location
Named_method::do_receiver_location() const
{
  return this->do_type()->receiver()->location();
}

// Bind a method to an object.

Expression*
Named_method::do_bind_method(Expression* expr, Location location) const
{
  Named_object* no = this->named_object_;
  Bound_method_expression* bme = Expression::make_bound_method(expr, this,
							       no, location);
  // If this is not a local method, and it does not use a stub, then
  // the real method expects a different type.  We need to cast the
  // first argument.
  if (this->depth() > 0 && !this->needs_stub_method())
    {
      Function_type* ftype = this->do_type();
      go_assert(ftype->is_method());
      Type* frtype = ftype->receiver()->type();
      bme->set_first_argument_type(frtype);
    }
  return bme;
}

// Return whether this method should not participate in interfaces.

bool
Named_method::do_nointerface() const
{
  Named_object* no = this->named_object_;
  return no->is_function() && no->func_value()->nointerface();
}

// Class Interface_method.

// Bind a method to an object.

Expression*
Interface_method::do_bind_method(Expression* expr,
				 Location location) const
{
  return Expression::make_interface_field_reference(expr, this->name_,
						    location);
}

// Class Methods.

// Insert a new method.  Return true if it was inserted, false
// otherwise.

bool
Methods::insert(const std::string& name, Method* m)
{
  std::pair<Method_map::iterator, bool> ins =
    this->methods_.insert(std::make_pair(name, m));
  if (ins.second)
    return true;
  else
    {
      Method* old_method = ins.first->second;
      if (m->depth() < old_method->depth())
	{
	  delete old_method;
	  ins.first->second = m;
	  return true;
	}
      else
	{
	  if (m->depth() == old_method->depth())
	    old_method->set_is_ambiguous();
	  return false;
	}
    }
}

// Return the number of unambiguous methods.

size_t
Methods::count() const
{
  size_t ret = 0;
  for (Method_map::const_iterator p = this->methods_.begin();
       p != this->methods_.end();
       ++p)
    if (!p->second->is_ambiguous())
      ++ret;
  return ret;
}

// Class Named_type.

// Return the name of the type.

const std::string&
Named_type::name() const
{
  return this->named_object_->name();
}

// Return the name of the type to use in an error message.

std::string
Named_type::message_name() const
{
  return this->named_object_->message_name();
}

// Whether this is an alias.  There are currently only two aliases so
// we just recognize them by name.

bool
Named_type::is_alias() const
{
  if (!this->is_builtin())
    return false;
  const std::string& name(this->name());
  return name == "byte" || name == "rune";
}

// Return the base type for this type.  We have to be careful about
// circular type definitions, which are invalid but may be seen here.

Type*
Named_type::named_base()
{
  if (this->seen_)
    return this;
  this->seen_ = true;
  Type* ret = this->type_->base();
  this->seen_ = false;
  return ret;
}

const Type*
Named_type::named_base() const
{
  if (this->seen_)
    return this;
  this->seen_ = true;
  const Type* ret = this->type_->base();
  this->seen_ = false;
  return ret;
}

// Return whether this is an error type.  We have to be careful about
// circular type definitions, which are invalid but may be seen here.

bool
Named_type::is_named_error_type() const
{
  if (this->seen_)
    return false;
  this->seen_ = true;
  bool ret = this->type_->is_error_type();
  this->seen_ = false;
  return ret;
}

// Whether this type is comparable.  We have to be careful about
// circular type definitions.

bool
Named_type::named_type_is_comparable(std::string* reason) const
{
  if (this->seen_)
    return false;
  this->seen_ = true;
  bool ret = Type::are_compatible_for_comparison(true, this->type_,
						 this->type_, reason);
  this->seen_ = false;
  return ret;
}

// Add a method to this type.

Named_object*
Named_type::add_method(const std::string& name, Function* function)
{
  if (this->local_methods_ == NULL)
    this->local_methods_ = new Bindings(NULL);
  return this->local_methods_->add_function(name, NULL, function);
}

// Add a method declaration to this type.

Named_object*
Named_type::add_method_declaration(const std::string& name, Package* package,
				   Function_type* type,
				   Location location)
{
  if (this->local_methods_ == NULL)
    this->local_methods_ = new Bindings(NULL);
  return this->local_methods_->add_function_declaration(name, package, type,
							location);
}

// Add an existing method to this type.

void
Named_type::add_existing_method(Named_object* no)
{
  if (this->local_methods_ == NULL)
    this->local_methods_ = new Bindings(NULL);
  this->local_methods_->add_named_object(no);
}

// Look for a local method NAME, and returns its named object, or NULL
// if not there.

Named_object*
Named_type::find_local_method(const std::string& name) const
{
  if (this->local_methods_ == NULL)
    return NULL;
  return this->local_methods_->lookup(name);
}

// Return whether NAME is an unexported field or method, for better
// error reporting.

bool
Named_type::is_unexported_local_method(Gogo* gogo,
				       const std::string& name) const
{
  Bindings* methods = this->local_methods_;
  if (methods != NULL)
    {
      for (Bindings::const_declarations_iterator p =
	     methods->begin_declarations();
	   p != methods->end_declarations();
	   ++p)
	{
	  if (Gogo::is_hidden_name(p->first)
	      && name == Gogo::unpack_hidden_name(p->first)
	      && gogo->pack_hidden_name(name, false) != p->first)
	    return true;
	}
    }
  return false;
}

// Build the complete list of methods for this type, which means
// recursively including all methods for anonymous fields.  Create all
// stub methods.

void
Named_type::finalize_methods(Gogo* gogo)
{
  if (this->all_methods_ != NULL)
    return;

  if (this->local_methods_ != NULL
      && (this->points_to() != NULL || this->interface_type() != NULL))
    {
      const Bindings* lm = this->local_methods_;
      for (Bindings::const_declarations_iterator p = lm->begin_declarations();
	   p != lm->end_declarations();
	   ++p)
	error_at(p->second->location(),
		 "invalid pointer or interface receiver type");
      delete this->local_methods_;
      this->local_methods_ = NULL;
      return;
    }

  Type::finalize_methods(gogo, this, this->location_, &this->all_methods_);
}

// Return the method NAME, or NULL if there isn't one or if it is
// ambiguous.  Set *IS_AMBIGUOUS if the method exists but is
// ambiguous.

Method*
Named_type::method_function(const std::string& name, bool* is_ambiguous) const
{
  return Type::method_function(this->all_methods_, name, is_ambiguous);
}

// Return a pointer to the interface method table for this type for
// the interface INTERFACE.  IS_POINTER is true if this is for a
// pointer to THIS.

tree
Named_type::interface_method_table(Gogo* gogo, const Interface_type* interface,
				   bool is_pointer)
{
  return Type::interface_method_table(gogo, this, interface, is_pointer,
				      &this->interface_method_tables_,
				      &this->pointer_interface_method_tables_);
}

// Return whether a named type has any hidden fields.

bool
Named_type::named_type_has_hidden_fields(std::string* reason) const
{
  if (this->seen_)
    return false;
  this->seen_ = true;
  bool ret = this->type_->has_hidden_fields(this, reason);
  this->seen_ = false;
  return ret;
}

// Look for a use of a complete type within another type.  This is
// used to check that we don't try to use a type within itself.

class Find_type_use : public Traverse
{
 public:
  Find_type_use(Named_type* find_type)
    : Traverse(traverse_types),
      find_type_(find_type), found_(false)
  { }

  // Whether we found the type.
  bool
  found() const
  { return this->found_; }

 protected:
  int
  type(Type*);

 private:
  // The type we are looking for.
  Named_type* find_type_;
  // Whether we found the type.
  bool found_;
};

// Check for FIND_TYPE in TYPE.

int
Find_type_use::type(Type* type)
{
  if (type->named_type() != NULL && this->find_type_ == type->named_type())
    {
      this->found_ = true;
      return TRAVERSE_EXIT;
    }

  // It's OK if we see a reference to the type in any type which is
  // essentially a pointer: a pointer, a slice, a function, a map, or
  // a channel.
  if (type->points_to() != NULL
      || type->is_slice_type()
      || type->function_type() != NULL
      || type->map_type() != NULL
      || type->channel_type() != NULL)
    return TRAVERSE_SKIP_COMPONENTS;

  // For an interface, a reference to the type in a method type should
  // be ignored, but we have to consider direct inheritance.  When
  // this is called, there may be cases of direct inheritance
  // represented as a method with no name.
  if (type->interface_type() != NULL)
    {
      const Typed_identifier_list* methods = type->interface_type()->methods();
      if (methods != NULL)
	{
	  for (Typed_identifier_list::const_iterator p = methods->begin();
	       p != methods->end();
	       ++p)
	    {
	      if (p->name().empty())
		{
		  if (Type::traverse(p->type(), this) == TRAVERSE_EXIT)
		    return TRAVERSE_EXIT;
		}
	    }
	}
      return TRAVERSE_SKIP_COMPONENTS;
    }

  // Otherwise, FIND_TYPE_ depends on TYPE, in the sense that we need
  // to convert TYPE to the backend representation before we convert
  // FIND_TYPE_.
  if (type->named_type() != NULL)
    {
      switch (type->base()->classification())
	{
	case Type::TYPE_ERROR:
	case Type::TYPE_BOOLEAN:
	case Type::TYPE_INTEGER:
	case Type::TYPE_FLOAT:
	case Type::TYPE_COMPLEX:
	case Type::TYPE_STRING:
	case Type::TYPE_NIL:
	  break;

	case Type::TYPE_ARRAY:
	case Type::TYPE_STRUCT:
	  this->find_type_->add_dependency(type->named_type());
	  break;

	case Type::TYPE_NAMED:
	case Type::TYPE_FORWARD:
	  go_assert(saw_errors());
	  break;

	case Type::TYPE_VOID:
	case Type::TYPE_SINK:
	case Type::TYPE_FUNCTION:
	case Type::TYPE_POINTER:
	case Type::TYPE_CALL_MULTIPLE_RESULT:
	case Type::TYPE_MAP:
	case Type::TYPE_CHANNEL:
	case Type::TYPE_INTERFACE:
	default:
	  go_unreachable();
	}
    }

  return TRAVERSE_CONTINUE;
}

// Verify that a named type does not refer to itself.

bool
Named_type::do_verify()
{
  if (this->is_verified_)
    return true;
  this->is_verified_ = true;

  Find_type_use find(this);
  Type::traverse(this->type_, &find);
  if (find.found())
    {
      error_at(this->location_, "invalid recursive type %qs",
	       this->message_name().c_str());
      this->is_error_ = true;
      return false;
    }

  // Check whether any of the local methods overloads an existing
  // struct field or interface method.  We don't need to check the
  // list of methods against itself: that is handled by the Bindings
  // code.
  if (this->local_methods_ != NULL)
    {
      Struct_type* st = this->type_->struct_type();
      if (st != NULL)
	{
	  for (Bindings::const_declarations_iterator p =
		 this->local_methods_->begin_declarations();
	       p != this->local_methods_->end_declarations();
	       ++p)
	    {
	      const std::string& name(p->first);
	      if (st != NULL && st->find_local_field(name, NULL) != NULL)
		{
		  error_at(p->second->location(),
			   "method %qs redeclares struct field name",
			   Gogo::message_name(name).c_str());
		}
	    }
	}
    }

  return true;
}

// Return whether this type is or contains a pointer.

bool
Named_type::do_has_pointer() const
{
  if (this->seen_)
    return false;
  this->seen_ = true;
  bool ret = this->type_->has_pointer();
  this->seen_ = false;
  return ret;
}

// Return whether comparisons for this type can use the identity
// function.

bool
Named_type::do_compare_is_identity(Gogo* gogo)
{
  // We don't use this->seen_ here because compare_is_identity may
  // call base() later, and that will mess up if seen_ is set here.
  if (this->seen_in_compare_is_identity_)
    return false;
  this->seen_in_compare_is_identity_ = true;
  bool ret = this->type_->compare_is_identity(gogo);
  this->seen_in_compare_is_identity_ = false;
  return ret;
}

// Return a hash code.  This is used for method lookup.  We simply
// hash on the name itself.

unsigned int
Named_type::do_hash_for_method(Gogo* gogo) const
{
  if (this->is_alias())
    return this->type_->named_type()->do_hash_for_method(gogo);

  const std::string& name(this->named_object()->name());
  unsigned int ret = Type::hash_string(name, 0);

  // GOGO will be NULL here when called from Type_hash_identical.
  // That is OK because that is only used for internal hash tables
  // where we are going to be comparing named types for equality.  In
  // other cases, which are cases where the runtime is going to
  // compare hash codes to see if the types are the same, we need to
  // include the pkgpath in the hash.
  if (gogo != NULL && !Gogo::is_hidden_name(name) && !this->is_builtin())
    {
      const Package* package = this->named_object()->package();
      if (package == NULL)
	ret = Type::hash_string(gogo->pkgpath(), ret);
      else
	ret = Type::hash_string(package->pkgpath(), ret);
    }

  return ret;
}

// Convert a named type to the backend representation.  In order to
// get dependencies right, we fill in a dummy structure for this type,
// then convert all the dependencies, then complete this type.  When
// this function is complete, the size of the type is known.

void
Named_type::convert(Gogo* gogo)
{
  if (this->is_error_ || this->is_converted_)
    return;

  this->create_placeholder(gogo);

  // If we are called to turn unsafe.Sizeof into a constant, we may
  // not have verified the type yet.  We have to make sure it is
  // verified, since that sets the list of dependencies.
  this->verify();

  // Convert all the dependencies.  If they refer indirectly back to
  // this type, they will pick up the intermediate tree we just
  // created.
  for (std::vector<Named_type*>::const_iterator p = this->dependencies_.begin();
       p != this->dependencies_.end();
       ++p)
    (*p)->convert(gogo);

  // Complete this type.
  Btype* bt = this->named_btype_;
  Type* base = this->type_->base();
  switch (base->classification())
    {
    case TYPE_VOID:
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_COMPLEX:
    case TYPE_STRING:
    case TYPE_NIL:
      break;

    case TYPE_MAP:
    case TYPE_CHANNEL:
      break;

    case TYPE_FUNCTION:
    case TYPE_POINTER:
      // The size of these types is already correct.  We don't worry
      // about filling them in until later, when we also track
      // circular references.
      break;

    case TYPE_STRUCT:
      {
	std::vector<Backend::Btyped_identifier> bfields;
	get_backend_struct_fields(gogo, base->struct_type()->fields(),
				  true, &bfields);
	if (!gogo->backend()->set_placeholder_struct_type(bt, bfields))
	  bt = gogo->backend()->error_type();
      }
      break;

    case TYPE_ARRAY:
      // Slice types were completed in create_placeholder.
      if (!base->is_slice_type())
	{
	  Btype* bet = base->array_type()->get_backend_element(gogo, true);
	  Bexpression* blen = base->array_type()->get_backend_length(gogo);
	  if (!gogo->backend()->set_placeholder_array_type(bt, bet, blen))
	    bt = gogo->backend()->error_type();
	}
      break;

    case TYPE_INTERFACE:
      // Interface types were completed in create_placeholder.
      break;

    case TYPE_ERROR:
      return;

    default:
    case TYPE_SINK:
    case TYPE_CALL_MULTIPLE_RESULT:
    case TYPE_NAMED:
    case TYPE_FORWARD:
      go_unreachable();
    }

  this->named_btype_ = bt;
  this->is_converted_ = true;
  this->is_placeholder_ = false;
}

// Create the placeholder for a named type.  This is the first step in
// converting to the backend representation.

void
Named_type::create_placeholder(Gogo* gogo)
{
  if (this->is_error_)
    this->named_btype_ = gogo->backend()->error_type();

  if (this->named_btype_ != NULL)
    return;

  // Create the structure for this type.  Note that because we call
  // base() here, we don't attempt to represent a named type defined
  // as another named type.  Instead both named types will point to
  // different base representations.
  Type* base = this->type_->base();
  Btype* bt;
  bool set_name = true;
  switch (base->classification())
    {
    case TYPE_ERROR:
      this->is_error_ = true;
      this->named_btype_ = gogo->backend()->error_type();
      return;

    case TYPE_VOID:
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_COMPLEX:
    case TYPE_STRING:
    case TYPE_NIL:
      // These are simple basic types, we can just create them
      // directly.
      bt = Type::get_named_base_btype(gogo, base);
      break;

    case TYPE_MAP:
    case TYPE_CHANNEL:
      // All maps and channels have the same backend representation.
      bt = Type::get_named_base_btype(gogo, base);
      break;

    case TYPE_FUNCTION:
    case TYPE_POINTER:
      {
	bool for_function = base->classification() == TYPE_FUNCTION;
	bt = gogo->backend()->placeholder_pointer_type(this->name(),
						       this->location_,
						       for_function);
	set_name = false;
      }
      break;

    case TYPE_STRUCT:
      bt = gogo->backend()->placeholder_struct_type(this->name(),
						    this->location_);
      this->is_placeholder_ = true;
      set_name = false;
      break;

    case TYPE_ARRAY:
      if (base->is_slice_type())
	bt = gogo->backend()->placeholder_struct_type(this->name(),
						      this->location_);
      else
	{
	  bt = gogo->backend()->placeholder_array_type(this->name(),
						       this->location_);
	  this->is_placeholder_ = true;
	}
      set_name = false;
      break;

    case TYPE_INTERFACE:
      if (base->interface_type()->is_empty())
	bt = Interface_type::get_backend_empty_interface_type(gogo);
      else
	{
	  bt = gogo->backend()->placeholder_struct_type(this->name(),
							this->location_);
	  set_name = false;
	}
      break;

    default:
    case TYPE_SINK:
    case TYPE_CALL_MULTIPLE_RESULT:
    case TYPE_NAMED:
    case TYPE_FORWARD:
      go_unreachable();
    }

  if (set_name)
    bt = gogo->backend()->named_type(this->name(), bt, this->location_);

  this->named_btype_ = bt;

  if (base->is_slice_type())
    {
      // We do not record slices as dependencies of other types,
      // because we can fill them in completely here with the final
      // size.
      std::vector<Backend::Btyped_identifier> bfields;
      get_backend_slice_fields(gogo, base->array_type(), true, &bfields);
      if (!gogo->backend()->set_placeholder_struct_type(bt, bfields))
	this->named_btype_ = gogo->backend()->error_type();
    }
  else if (base->interface_type() != NULL
	   && !base->interface_type()->is_empty())
    {
      // We do not record interfaces as dependencies of other types,
      // because we can fill them in completely here with the final
      // size.
      std::vector<Backend::Btyped_identifier> bfields;
      get_backend_interface_fields(gogo, base->interface_type(), true,
				   &bfields);
      if (!gogo->backend()->set_placeholder_struct_type(bt, bfields))
	this->named_btype_ = gogo->backend()->error_type();
    }
}

// Get a tree for a named type.

Btype*
Named_type::do_get_backend(Gogo* gogo)
{
  if (this->is_error_)
    return gogo->backend()->error_type();

  Btype* bt = this->named_btype_;

  if (!gogo->named_types_are_converted())
    {
      // We have not completed converting named types.  NAMED_BTYPE_
      // is a placeholder and we shouldn't do anything further.
      if (bt != NULL)
	return bt;

      // We don't build dependencies for types whose sizes do not
      // change or are not relevant, so we may see them here while
      // converting types.
      this->create_placeholder(gogo);
      bt = this->named_btype_;
      go_assert(bt != NULL);
      return bt;
    }

  // We are not converting types.  This should only be called if the
  // type has already been converted.
  if (!this->is_converted_)
    {
      go_assert(saw_errors());
      return gogo->backend()->error_type();
    }

  go_assert(bt != NULL);

  // Complete the tree.
  Type* base = this->type_->base();
  Btype* bt1;
  switch (base->classification())
    {
    case TYPE_ERROR:
      return gogo->backend()->error_type();

    case TYPE_VOID:
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_COMPLEX:
    case TYPE_STRING:
    case TYPE_NIL:
    case TYPE_MAP:
    case TYPE_CHANNEL:
      return bt;

    case TYPE_STRUCT:
      if (!this->seen_in_get_backend_)
	{
	  this->seen_in_get_backend_ = true;
	  base->struct_type()->finish_backend_fields(gogo);
	  this->seen_in_get_backend_ = false;
	}
      return bt;

    case TYPE_ARRAY:
      if (!this->seen_in_get_backend_)
	{
	  this->seen_in_get_backend_ = true;
	  base->array_type()->finish_backend_element(gogo);
	  this->seen_in_get_backend_ = false;
	}
      return bt;

    case TYPE_INTERFACE:
      if (!this->seen_in_get_backend_)
	{
	  this->seen_in_get_backend_ = true;
	  base->interface_type()->finish_backend_methods(gogo);
	  this->seen_in_get_backend_ = false;
	}
      return bt;

    case TYPE_FUNCTION:
      // Don't build a circular data structure.  GENERIC can't handle
      // it.
      if (this->seen_in_get_backend_)
	{
	  this->is_circular_ = true;
	  return gogo->backend()->circular_pointer_type(bt, true);
	}
      this->seen_in_get_backend_ = true;
      bt1 = Type::get_named_base_btype(gogo, base);
      this->seen_in_get_backend_ = false;
      if (this->is_circular_)
	bt1 = gogo->backend()->circular_pointer_type(bt, true);
      if (!gogo->backend()->set_placeholder_function_type(bt, bt1))
	bt = gogo->backend()->error_type();
      return bt;

    case TYPE_POINTER:
      // Don't build a circular data structure. GENERIC can't handle
      // it.
      if (this->seen_in_get_backend_)
	{
	  this->is_circular_ = true;
	  return gogo->backend()->circular_pointer_type(bt, false);
	}
      this->seen_in_get_backend_ = true;
      bt1 = Type::get_named_base_btype(gogo, base);
      this->seen_in_get_backend_ = false;
      if (this->is_circular_)
	bt1 = gogo->backend()->circular_pointer_type(bt, false);
      if (!gogo->backend()->set_placeholder_pointer_type(bt, bt1))
	bt = gogo->backend()->error_type();
      return bt;

    default:
    case TYPE_SINK:
    case TYPE_CALL_MULTIPLE_RESULT:
    case TYPE_NAMED:
    case TYPE_FORWARD:
      go_unreachable();
    }

  go_unreachable();
}

// Build a type descriptor for a named type.

Expression*
Named_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  if (name == NULL && this->is_alias())
    return this->type_->type_descriptor(gogo, this->type_);

  // If NAME is not NULL, then we don't really want the type
  // descriptor for this type; we want the descriptor for the
  // underlying type, giving it the name NAME.
  return this->named_type_descriptor(gogo, this->type_,
				     name == NULL ? this : name);
}

// Add to the reflection string.  This is used mostly for the name of
// the type used in a type descriptor, not for actual reflection
// strings.

void
Named_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  if (this->is_alias())
    {
      this->append_reflection(this->type_, gogo, ret);
      return;
    }
  if (!this->is_builtin())
    {
      // We handle -fgo-prefix and -fgo-pkgpath differently here for
      // compatibility with how the compiler worked before
      // -fgo-pkgpath was introduced.  When -fgo-pkgpath is specified,
      // we use it to make a unique reflection string, so that the
      // type canonicalization in the reflect package will work.  In
      // order to be compatible with the gc compiler, we put tabs into
      // the package path, so that the reflect methods can discard it.
      const Package* package = this->named_object_->package();
      if (gogo->pkgpath_from_option())
	{
	  ret->push_back('\t');
	  ret->append(package != NULL
		      ? package->pkgpath_symbol()
		      : gogo->pkgpath_symbol());
	  ret->push_back('\t');
	}
      ret->append(package != NULL
		  ? package->package_name()
		  : gogo->package_name());
      ret->push_back('.');
    }
  if (this->in_function_ != NULL)
    {
      ret->push_back('\t');
      ret->append(Gogo::unpack_hidden_name(this->in_function_->name()));
      ret->push_back('$');
      if (this->in_function_index_ > 0)
	{
	  char buf[30];
	  snprintf(buf, sizeof buf, "%u", this->in_function_index_);
	  ret->append(buf);
	  ret->push_back('$');
	}
      ret->push_back('\t');
    }
  ret->append(Gogo::unpack_hidden_name(this->named_object_->name()));
}

// Get the mangled name.

void
Named_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  if (this->is_alias())
    {
      this->append_mangled_name(this->type_, gogo, ret);
      return;
    }
  Named_object* no = this->named_object_;
  std::string name;
  if (this->is_builtin())
    go_assert(this->in_function_ == NULL);
  else
    {
      const std::string& pkgpath(no->package() == NULL
				 ? gogo->pkgpath_symbol()
				 : no->package()->pkgpath_symbol());
      name = pkgpath;
      name.append(1, '.');
      if (this->in_function_ != NULL)
	{
	  name.append(Gogo::unpack_hidden_name(this->in_function_->name()));
	  name.append(1, '$');
	  if (this->in_function_index_ > 0)
	    {
	      char buf[30];
	      snprintf(buf, sizeof buf, "%u", this->in_function_index_);
	      name.append(buf);
	      name.append(1, '$');
	    }
	}
    }
  name.append(Gogo::unpack_hidden_name(no->name()));
  char buf[20];
  snprintf(buf, sizeof buf, "N%u_", static_cast<unsigned int>(name.length()));
  ret->append(buf);
  ret->append(name);
}

// Export the type.  This is called to export a global type.

void
Named_type::export_named_type(Export* exp, const std::string&) const
{
  // We don't need to write the name of the type here, because it will
  // be written by Export::write_type anyhow.
  exp->write_c_string("type ");
  exp->write_type(this);
  exp->write_c_string(";\n");
}

// Import a named type.

void
Named_type::import_named_type(Import* imp, Named_type** ptype)
{
  imp->require_c_string("type ");
  Type *type = imp->read_type();
  *ptype = type->named_type();
  go_assert(*ptype != NULL);
  imp->require_c_string(";\n");
}

// Export the type when it is referenced by another type.  In this
// case Export::export_type will already have issued the name.

void
Named_type::do_export(Export* exp) const
{
  exp->write_type(this->type_);

  // To save space, we only export the methods directly attached to
  // this type.
  Bindings* methods = this->local_methods_;
  if (methods == NULL)
    return;

  exp->write_c_string("\n");
  for (Bindings::const_definitions_iterator p = methods->begin_definitions();
       p != methods->end_definitions();
       ++p)
    {
      exp->write_c_string(" ");
      (*p)->export_named_object(exp);
    }

  for (Bindings::const_declarations_iterator p = methods->begin_declarations();
       p != methods->end_declarations();
       ++p)
    {
      if (p->second->is_function_declaration())
	{
	  exp->write_c_string(" ");
	  p->second->export_named_object(exp);
	}
    }
}

// Make a named type.

Named_type*
Type::make_named_type(Named_object* named_object, Type* type,
		      Location location)
{
  return new Named_type(named_object, type, location);
}

// Finalize the methods for TYPE.  It will be a named type or a struct
// type.  This sets *ALL_METHODS to the list of methods, and builds
// all required stubs.

void
Type::finalize_methods(Gogo* gogo, const Type* type, Location location,
		       Methods** all_methods)
{
  *all_methods = NULL;
  Types_seen types_seen;
  Type::add_methods_for_type(type, NULL, 0, false, false, &types_seen,
			     all_methods);
  Type::build_stub_methods(gogo, type, *all_methods, location);
}

// Add the methods for TYPE to *METHODS.  FIELD_INDEXES is used to
// build up the struct field indexes as we go.  DEPTH is the depth of
// the field within TYPE.  IS_EMBEDDED_POINTER is true if we are
// adding these methods for an anonymous field with pointer type.
// NEEDS_STUB_METHOD is true if we need to use a stub method which
// calls the real method.  TYPES_SEEN is used to avoid infinite
// recursion.

void
Type::add_methods_for_type(const Type* type,
			   const Method::Field_indexes* field_indexes,
			   unsigned int depth,
			   bool is_embedded_pointer,
			   bool needs_stub_method,
			   Types_seen* types_seen,
			   Methods** methods)
{
  // Pointer types may not have methods.
  if (type->points_to() != NULL)
    return;

  const Named_type* nt = type->named_type();
  if (nt != NULL)
    {
      std::pair<Types_seen::iterator, bool> ins = types_seen->insert(nt);
      if (!ins.second)
	return;
    }

  if (nt != NULL)
    Type::add_local_methods_for_type(nt, field_indexes, depth,
				     is_embedded_pointer, needs_stub_method,
				     methods);

  Type::add_embedded_methods_for_type(type, field_indexes, depth,
				      is_embedded_pointer, needs_stub_method,
				      types_seen, methods);

  // If we are called with depth > 0, then we are looking at an
  // anonymous field of a struct.  If such a field has interface type,
  // then we need to add the interface methods.  We don't want to add
  // them when depth == 0, because we will already handle them
  // following the usual rules for an interface type.
  if (depth > 0)
    Type::add_interface_methods_for_type(type, field_indexes, depth, methods);
}

// Add the local methods for the named type NT to *METHODS.  The
// parameters are as for add_methods_to_type.

void
Type::add_local_methods_for_type(const Named_type* nt,
				 const Method::Field_indexes* field_indexes,
				 unsigned int depth,
				 bool is_embedded_pointer,
				 bool needs_stub_method,
				 Methods** methods)
{
  const Bindings* local_methods = nt->local_methods();
  if (local_methods == NULL)
    return;

  if (*methods == NULL)
    *methods = new Methods();

  for (Bindings::const_declarations_iterator p =
	 local_methods->begin_declarations();
       p != local_methods->end_declarations();
       ++p)
    {
      Named_object* no = p->second;
      bool is_value_method = (is_embedded_pointer
			      || !Type::method_expects_pointer(no));
      Method* m = new Named_method(no, field_indexes, depth, is_value_method,
				   (needs_stub_method
				    || (depth > 0 && is_value_method)));
      if (!(*methods)->insert(no->name(), m))
	delete m;
    }
}

// Add the embedded methods for TYPE to *METHODS.  These are the
// methods attached to anonymous fields.  The parameters are as for
// add_methods_to_type.

void
Type::add_embedded_methods_for_type(const Type* type,
				    const Method::Field_indexes* field_indexes,
				    unsigned int depth,
				    bool is_embedded_pointer,
				    bool needs_stub_method,
				    Types_seen* types_seen,
				    Methods** methods)
{
  // Look for anonymous fields in TYPE.  TYPE has fields if it is a
  // struct.
  const Struct_type* st = type->struct_type();
  if (st == NULL)
    return;

  const Struct_field_list* fields = st->fields();
  if (fields == NULL)
    return;

  unsigned int i = 0;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf, ++i)
    {
      if (!pf->is_anonymous())
	continue;

      Type* ftype = pf->type();
      bool is_pointer = false;
      if (ftype->points_to() != NULL)
	{
	  ftype = ftype->points_to();
	  is_pointer = true;
	}
      Named_type* fnt = ftype->named_type();
      if (fnt == NULL)
	{
	  // This is an error, but it will be diagnosed elsewhere.
	  continue;
	}

      Method::Field_indexes* sub_field_indexes = new Method::Field_indexes();
      sub_field_indexes->next = field_indexes;
      sub_field_indexes->field_index = i;

      Type::add_methods_for_type(fnt, sub_field_indexes, depth + 1,
				 (is_embedded_pointer || is_pointer),
				 (needs_stub_method
				  || is_pointer
				  || i > 0),
				 types_seen,
				 methods);
    }
}

// If TYPE is an interface type, then add its method to *METHODS.
// This is for interface methods attached to an anonymous field.  The
// parameters are as for add_methods_for_type.

void
Type::add_interface_methods_for_type(const Type* type,
				     const Method::Field_indexes* field_indexes,
				     unsigned int depth,
				     Methods** methods)
{
  const Interface_type* it = type->interface_type();
  if (it == NULL)
    return;

  const Typed_identifier_list* imethods = it->methods();
  if (imethods == NULL)
    return;

  if (*methods == NULL)
    *methods = new Methods();

  for (Typed_identifier_list::const_iterator pm = imethods->begin();
       pm != imethods->end();
       ++pm)
    {
      Function_type* fntype = pm->type()->function_type();
      if (fntype == NULL)
	{
	  // This is an error, but it should be reported elsewhere
	  // when we look at the methods for IT.
	  continue;
	}
      go_assert(!fntype->is_method());
      fntype = fntype->copy_with_receiver(const_cast<Type*>(type));
      Method* m = new Interface_method(pm->name(), pm->location(), fntype,
				       field_indexes, depth);
      if (!(*methods)->insert(pm->name(), m))
	delete m;
    }
}

// Build stub methods for TYPE as needed.  METHODS is the set of
// methods for the type.  A stub method may be needed when a type
// inherits a method from an anonymous field.  When we need the
// address of the method, as in a type descriptor, we need to build a
// little stub which does the required field dereferences and jumps to
// the real method.  LOCATION is the location of the type definition.

void
Type::build_stub_methods(Gogo* gogo, const Type* type, const Methods* methods,
			 Location location)
{
  if (methods == NULL)
    return;
  for (Methods::const_iterator p = methods->begin();
       p != methods->end();
       ++p)
    {
      Method* m = p->second;
      if (m->is_ambiguous() || !m->needs_stub_method())
	continue;

      const std::string& name(p->first);

      // Build a stub method.

      const Function_type* fntype = m->type();

      static unsigned int counter;
      char buf[100];
      snprintf(buf, sizeof buf, "$this%u", counter);
      ++counter;

      Type* receiver_type = const_cast<Type*>(type);
      if (!m->is_value_method())
	receiver_type = Type::make_pointer_type(receiver_type);
      Location receiver_location = m->receiver_location();
      Typed_identifier* receiver = new Typed_identifier(buf, receiver_type,
							receiver_location);

      const Typed_identifier_list* fnparams = fntype->parameters();
      Typed_identifier_list* stub_params;
      if (fnparams == NULL || fnparams->empty())
	stub_params = NULL;
      else
	{
	  // We give each stub parameter a unique name.
	  stub_params = new Typed_identifier_list();
	  for (Typed_identifier_list::const_iterator pp = fnparams->begin();
	       pp != fnparams->end();
	       ++pp)
	    {
	      char pbuf[100];
	      snprintf(pbuf, sizeof pbuf, "$p%u", counter);
	      stub_params->push_back(Typed_identifier(pbuf, pp->type(),
						      pp->location()));
	      ++counter;
	    }
	}

      const Typed_identifier_list* fnresults = fntype->results();
      Typed_identifier_list* stub_results;
      if (fnresults == NULL || fnresults->empty())
	stub_results = NULL;
      else
	{
	  // We create the result parameters without any names, since
	  // we won't refer to them.
	  stub_results = new Typed_identifier_list();
	  for (Typed_identifier_list::const_iterator pr = fnresults->begin();
	       pr != fnresults->end();
	       ++pr)
	    stub_results->push_back(Typed_identifier("", pr->type(),
						     pr->location()));
	}

      Function_type* stub_type = Type::make_function_type(receiver,
							  stub_params,
							  stub_results,
							  fntype->location());
      if (fntype->is_varargs())
	stub_type->set_is_varargs();

      // We only create the function in the package which creates the
      // type.
      const Package* package;
      if (type->named_type() == NULL)
	package = NULL;
      else
	package = type->named_type()->named_object()->package();
      Named_object* stub;
      if (package != NULL)
	stub = Named_object::make_function_declaration(name, package,
						       stub_type, location);
      else
	{
	  stub = gogo->start_function(name, stub_type, false,
				      fntype->location());
	  Type::build_one_stub_method(gogo, m, buf, stub_params,
				      fntype->is_varargs(), location);
	  gogo->finish_function(fntype->location());

	  if (m->nointerface() && stub->is_function())
	    stub->func_value()->set_nointerface();
	}

      m->set_stub_object(stub);
    }
}

// Build a stub method which adjusts the receiver as required to call
// METHOD.  RECEIVER_NAME is the name we used for the receiver.
// PARAMS is the list of function parameters.

void
Type::build_one_stub_method(Gogo* gogo, Method* method,
			    const char* receiver_name,
			    const Typed_identifier_list* params,
			    bool is_varargs,
			    Location location)
{
  Named_object* receiver_object = gogo->lookup(receiver_name, NULL);
  go_assert(receiver_object != NULL);

  Expression* expr = Expression::make_var_reference(receiver_object, location);
  expr = Type::apply_field_indexes(expr, method->field_indexes(), location);
  if (expr->type()->points_to() == NULL)
    expr = Expression::make_unary(OPERATOR_AND, expr, location);

  Expression_list* arguments;
  if (params == NULL || params->empty())
    arguments = NULL;
  else
    {
      arguments = new Expression_list();
      for (Typed_identifier_list::const_iterator p = params->begin();
	   p != params->end();
	   ++p)
	{
	  Named_object* param = gogo->lookup(p->name(), NULL);
	  go_assert(param != NULL);
	  Expression* param_ref = Expression::make_var_reference(param,
								 location);
	  arguments->push_back(param_ref);
	}
    }

  Expression* func = method->bind_method(expr, location);
  go_assert(func != NULL);
  Call_expression* call = Expression::make_call(func, arguments, is_varargs,
						location);
  call->set_hidden_fields_are_ok();

  Statement* s = Statement::make_return_from_call(call, location);
  Return_statement* retstat = s->return_statement();
  if (retstat != NULL)
    {
      // We can return values with hidden fields from a stub.  This is
      // necessary if the method is itself hidden.
      retstat->set_hidden_fields_are_ok();
    }
  gogo->add_statement(s);
}

// Apply FIELD_INDEXES to EXPR.  The field indexes have to be applied
// in reverse order.

Expression*
Type::apply_field_indexes(Expression* expr,
			  const Method::Field_indexes* field_indexes,
			  Location location)
{
  if (field_indexes == NULL)
    return expr;
  expr = Type::apply_field_indexes(expr, field_indexes->next, location);
  Struct_type* stype = expr->type()->deref()->struct_type();
  go_assert(stype != NULL
	     && field_indexes->field_index < stype->field_count());
  if (expr->type()->struct_type() == NULL)
    {
      go_assert(expr->type()->points_to() != NULL);
      expr = Expression::make_unary(OPERATOR_MULT, expr, location);
      go_assert(expr->type()->struct_type() == stype);
    }
  return Expression::make_field_reference(expr, field_indexes->field_index,
					  location);
}

// Return whether NO is a method for which the receiver is a pointer.

bool
Type::method_expects_pointer(const Named_object* no)
{
  const Function_type *fntype;
  if (no->is_function())
    fntype = no->func_value()->type();
  else if (no->is_function_declaration())
    fntype = no->func_declaration_value()->type();
  else
    go_unreachable();
  return fntype->receiver()->type()->points_to() != NULL;
}

// Given a set of methods for a type, METHODS, return the method NAME,
// or NULL if there isn't one or if it is ambiguous.  If IS_AMBIGUOUS
// is not NULL, then set *IS_AMBIGUOUS to true if the method exists
// but is ambiguous (and return NULL).

Method*
Type::method_function(const Methods* methods, const std::string& name,
		      bool* is_ambiguous)
{
  if (is_ambiguous != NULL)
    *is_ambiguous = false;
  if (methods == NULL)
    return NULL;
  Methods::const_iterator p = methods->find(name);
  if (p == methods->end())
    return NULL;
  Method* m = p->second;
  if (m->is_ambiguous())
    {
      if (is_ambiguous != NULL)
	*is_ambiguous = true;
      return NULL;
    }
  return m;
}

// Return a pointer to the interface method table for TYPE for the
// interface INTERFACE.

tree
Type::interface_method_table(Gogo* gogo, Type* type,
			     const Interface_type *interface,
			     bool is_pointer,
			     Interface_method_tables** method_tables,
			     Interface_method_tables** pointer_tables)
{
  go_assert(!interface->is_empty());

  Interface_method_tables** pimt = is_pointer ? method_tables : pointer_tables;

  if (*pimt == NULL)
    *pimt = new Interface_method_tables(5);

  std::pair<const Interface_type*, tree> val(interface, NULL_TREE);
  std::pair<Interface_method_tables::iterator, bool> ins = (*pimt)->insert(val);

  if (ins.second)
    {
      // This is a new entry in the hash table.
      go_assert(ins.first->second == NULL_TREE);
      ins.first->second = gogo->interface_method_table_for_type(interface,
								type,
								is_pointer);
    }

  tree decl = ins.first->second;
  if (decl == error_mark_node)
    return error_mark_node;
  go_assert(decl != NULL_TREE && TREE_CODE(decl) == VAR_DECL);
  return build_fold_addr_expr(decl);
}

// Look for field or method NAME for TYPE.  Return an Expression for
// the field or method bound to EXPR.  If there is no such field or
// method, give an appropriate error and return an error expression.

Expression*
Type::bind_field_or_method(Gogo* gogo, const Type* type, Expression* expr,
			   const std::string& name,
			   Location location)
{
  if (type->deref()->is_error_type())
    return Expression::make_error(location);

  const Named_type* nt = type->deref()->named_type();
  const Struct_type* st = type->deref()->struct_type();
  const Interface_type* it = type->interface_type();

  // If this is a pointer to a pointer, then it is possible that the
  // pointed-to type has methods.
  bool dereferenced = false;
  if (nt == NULL
      && st == NULL
      && it == NULL
      && type->points_to() != NULL
      && type->points_to()->points_to() != NULL)
    {
      expr = Expression::make_unary(OPERATOR_MULT, expr, location);
      type = type->points_to();
      if (type->deref()->is_error_type())
	return Expression::make_error(location);
      nt = type->points_to()->named_type();
      st = type->points_to()->struct_type();
      dereferenced = true;
    }

  bool receiver_can_be_pointer = (expr->type()->points_to() != NULL
				  || expr->is_addressable());
  std::vector<const Named_type*> seen;
  bool is_method = false;
  bool found_pointer_method = false;
  std::string ambig1;
  std::string ambig2;
  if (Type::find_field_or_method(type, name, receiver_can_be_pointer,
				 &seen, NULL, &is_method,
				 &found_pointer_method, &ambig1, &ambig2))
    {
      Expression* ret;
      if (!is_method)
	{
	  go_assert(st != NULL);
	  if (type->struct_type() == NULL)
	    {
	      go_assert(type->points_to() != NULL);
	      expr = Expression::make_unary(OPERATOR_MULT, expr,
					    location);
	      go_assert(expr->type()->struct_type() == st);
	    }
	  ret = st->field_reference(expr, name, location);
	}
      else if (it != NULL && it->find_method(name) != NULL)
	ret = Expression::make_interface_field_reference(expr, name,
							 location);
      else
	{
	  Method* m;
	  if (nt != NULL)
	    m = nt->method_function(name, NULL);
	  else if (st != NULL)
	    m = st->method_function(name, NULL);
	  else
	    go_unreachable();
	  go_assert(m != NULL);
	  if (dereferenced && m->is_value_method())
	    {
	      error_at(location,
		       "calling value method requires explicit dereference");
	      return Expression::make_error(location);
	    }
	  if (!m->is_value_method() && expr->type()->points_to() == NULL)
	    expr = Expression::make_unary(OPERATOR_AND, expr, location);
	  ret = m->bind_method(expr, location);
	}
      go_assert(ret != NULL);
      return ret;
    }
  else
    {
      if (!ambig1.empty())
	error_at(location, "%qs is ambiguous via %qs and %qs",
		 Gogo::message_name(name).c_str(), ambig1.c_str(),
		 ambig2.c_str());
      else if (found_pointer_method)
	error_at(location, "method requires a pointer receiver");
      else if (nt == NULL && st == NULL && it == NULL)
	error_at(location,
		 ("reference to field %qs in object which "
		  "has no fields or methods"),
		 Gogo::message_name(name).c_str());
      else
	{
	  bool is_unexported;
	  if (!Gogo::is_hidden_name(name))
	    is_unexported = false;
	  else
	    {
	      std::string unpacked = Gogo::unpack_hidden_name(name);
	      seen.clear();
	      is_unexported = Type::is_unexported_field_or_method(gogo, type,
								  unpacked,
								  &seen);
	    }
	  if (is_unexported)
	    error_at(location, "reference to unexported field or method %qs",
		     Gogo::message_name(name).c_str());
	  else
	    error_at(location, "reference to undefined field or method %qs",
		     Gogo::message_name(name).c_str());
	}
      return Expression::make_error(location);
    }
}

// Look in TYPE for a field or method named NAME, return true if one
// is found.  This looks through embedded anonymous fields and handles
// ambiguity.  If a method is found, sets *IS_METHOD to true;
// otherwise, if a field is found, set it to false.  If
// RECEIVER_CAN_BE_POINTER is false, then the receiver is a value
// whose address can not be taken.  SEEN is used to avoid infinite
// recursion on invalid types.

// When returning false, this sets *FOUND_POINTER_METHOD if we found a
// method we couldn't use because it requires a pointer.  LEVEL is
// used for recursive calls, and can be NULL for a non-recursive call.
// When this function returns false because it finds that the name is
// ambiguous, it will store a path to the ambiguous names in *AMBIG1
// and *AMBIG2.  If the name is not found at all, *AMBIG1 and *AMBIG2
// will be unchanged.

// This function just returns whether or not there is a field or
// method, and whether it is a field or method.  It doesn't build an
// expression to refer to it.  If it is a method, we then look in the
// list of all methods for the type.  If it is a field, the search has
// to be done again, looking only for fields, and building up the
// expression as we go.

bool
Type::find_field_or_method(const Type* type,
			   const std::string& name,
			   bool receiver_can_be_pointer,
			   std::vector<const Named_type*>* seen,
			   int* level,
			   bool* is_method,
			   bool* found_pointer_method,
			   std::string* ambig1,
			   std::string* ambig2)
{
  // Named types can have locally defined methods.
  const Named_type* nt = type->named_type();
  if (nt == NULL && type->points_to() != NULL)
    nt = type->points_to()->named_type();
  if (nt != NULL)
    {
      Named_object* no = nt->find_local_method(name);
      if (no != NULL)
	{
	  if (receiver_can_be_pointer || !Type::method_expects_pointer(no))
	    {
	      *is_method = true;
	      return true;
	    }

	  // Record that we have found a pointer method in order to
	  // give a better error message if we don't find anything
	  // else.
	  *found_pointer_method = true;
	}

      for (std::vector<const Named_type*>::const_iterator p = seen->begin();
	   p != seen->end();
	   ++p)
	{
	  if (*p == nt)
	    {
	      // We've already seen this type when searching for methods.
	      return false;
	    }
	}
    }

  // Interface types can have methods.
  const Interface_type* it = type->interface_type();
  if (it != NULL && it->find_method(name) != NULL)
    {
      *is_method = true;
      return true;
    }

  // Struct types can have fields.  They can also inherit fields and
  // methods from anonymous fields.
  const Struct_type* st = type->deref()->struct_type();
  if (st == NULL)
    return false;
  const Struct_field_list* fields = st->fields();
  if (fields == NULL)
    return false;

  if (nt != NULL)
    seen->push_back(nt);

  int found_level = 0;
  bool found_is_method = false;
  std::string found_ambig1;
  std::string found_ambig2;
  const Struct_field* found_parent = NULL;
  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf)
    {
      if (pf->is_field_name(name))
	{
	  *is_method = false;
	  if (nt != NULL)
	    seen->pop_back();
	  return true;
	}

      if (!pf->is_anonymous())
	continue;

      if (pf->type()->deref()->is_error_type()
	  || pf->type()->deref()->is_undefined())
	continue;

      Named_type* fnt = pf->type()->named_type();
      if (fnt == NULL)
	fnt = pf->type()->deref()->named_type();
      go_assert(fnt != NULL);

      // Methods with pointer receivers on embedded field are
      // inherited by the pointer to struct, and also by the struct
      // type if the field itself is a pointer.
      bool can_be_pointer = (receiver_can_be_pointer
			     || pf->type()->points_to() != NULL);
      int sublevel = level == NULL ? 1 : *level + 1;
      bool sub_is_method;
      std::string subambig1;
      std::string subambig2;
      bool subfound = Type::find_field_or_method(fnt,
						 name,
						 can_be_pointer,
						 seen,
						 &sublevel,
						 &sub_is_method,
						 found_pointer_method,
						 &subambig1,
						 &subambig2);
      if (!subfound)
	{
	  if (!subambig1.empty())
	    {
	      // The name was found via this field, but is ambiguous.
	      // if the ambiguity is lower or at the same level as
	      // anything else we have already found, then we want to
	      // pass the ambiguity back to the caller.
	      if (found_level == 0 || sublevel <= found_level)
		{
		  found_ambig1 = (Gogo::message_name(pf->field_name())
				  + '.' + subambig1);
		  found_ambig2 = (Gogo::message_name(pf->field_name())
				  + '.' + subambig2);
		  found_level = sublevel;
		}
	    }
	}
      else
	{
	  // The name was found via this field.  Use the level to see
	  // if we want to use this one, or whether it introduces an
	  // ambiguity.
	  if (found_level == 0 || sublevel < found_level)
	    {
	      found_level = sublevel;
	      found_is_method = sub_is_method;
	      found_ambig1.clear();
	      found_ambig2.clear();
	      found_parent = &*pf;
	    }
	  else if (sublevel > found_level)
	    ;
	  else if (found_ambig1.empty())
	    {
	      // We found an ambiguity.
	      go_assert(found_parent != NULL);
	      found_ambig1 = Gogo::message_name(found_parent->field_name());
	      found_ambig2 = Gogo::message_name(pf->field_name());
	    }
	  else
	    {
	      // We found an ambiguity, but we already know of one.
	      // Just report the earlier one.
	    }
	}
    }

  // Here if we didn't find anything FOUND_LEVEL is 0.  If we found
  // something ambiguous, FOUND_LEVEL is not 0 and FOUND_AMBIG1 and
  // FOUND_AMBIG2 are not empty.  If we found the field, FOUND_LEVEL
  // is not 0 and FOUND_AMBIG1 and FOUND_AMBIG2 are empty.

  if (nt != NULL)
    seen->pop_back();

  if (found_level == 0)
    return false;
  else if (!found_ambig1.empty())
    {
      go_assert(!found_ambig1.empty());
      ambig1->assign(found_ambig1);
      ambig2->assign(found_ambig2);
      if (level != NULL)
	*level = found_level;
      return false;
    }
  else
    {
      if (level != NULL)
	*level = found_level;
      *is_method = found_is_method;
      return true;
    }
}

// Return whether NAME is an unexported field or method for TYPE.

bool
Type::is_unexported_field_or_method(Gogo* gogo, const Type* type,
				    const std::string& name,
				    std::vector<const Named_type*>* seen)
{
  const Named_type* nt = type->named_type();
  if (nt == NULL)
    nt = type->deref()->named_type();
  if (nt != NULL)
    {
      if (nt->is_unexported_local_method(gogo, name))
	return true;

      for (std::vector<const Named_type*>::const_iterator p = seen->begin();
	   p != seen->end();
	   ++p)
	{
	  if (*p == nt)
	    {
	      // We've already seen this type.
	      return false;
	    }
	}
    }

  const Interface_type* it = type->interface_type();
  if (it != NULL && it->is_unexported_method(gogo, name))
    return true;

  type = type->deref();

  const Struct_type* st = type->struct_type();
  if (st != NULL && st->is_unexported_local_field(gogo, name))
    return true;

  if (st == NULL)
    return false;

  const Struct_field_list* fields = st->fields();
  if (fields == NULL)
    return false;

  if (nt != NULL)
    seen->push_back(nt);

  for (Struct_field_list::const_iterator pf = fields->begin();
       pf != fields->end();
       ++pf)
    {
      if (pf->is_anonymous()
	  && !pf->type()->deref()->is_error_type()
	  && !pf->type()->deref()->is_undefined())
	{
	  Named_type* subtype = pf->type()->named_type();
	  if (subtype == NULL)
	    subtype = pf->type()->deref()->named_type();
	  if (subtype == NULL)
	    {
	      // This is an error, but it will be diagnosed elsewhere.
	      continue;
	    }
	  if (Type::is_unexported_field_or_method(gogo, subtype, name, seen))
	    {
	      if (nt != NULL)
		seen->pop_back();
	      return true;
	    }
	}
    }

  if (nt != NULL)
    seen->pop_back();

  return false;
}

// Class Forward_declaration.

Forward_declaration_type::Forward_declaration_type(Named_object* named_object)
  : Type(TYPE_FORWARD),
    named_object_(named_object->resolve()), warned_(false)
{
  go_assert(this->named_object_->is_unknown()
	     || this->named_object_->is_type_declaration());
}

// Return the named object.

Named_object*
Forward_declaration_type::named_object()
{
  return this->named_object_->resolve();
}

const Named_object*
Forward_declaration_type::named_object() const
{
  return this->named_object_->resolve();
}

// Return the name of the forward declared type.

const std::string&
Forward_declaration_type::name() const
{
  return this->named_object()->name();
}

// Warn about a use of a type which has been declared but not defined.

void
Forward_declaration_type::warn() const
{
  Named_object* no = this->named_object_->resolve();
  if (no->is_unknown())
    {
      // The name was not defined anywhere.
      if (!this->warned_)
	{
	  error_at(this->named_object_->location(),
		   "use of undefined type %qs",
		   no->message_name().c_str());
	  this->warned_ = true;
	}
    }
  else if (no->is_type_declaration())
    {
      // The name was seen as a type, but the type was never defined.
      if (no->type_declaration_value()->using_type())
	{
	  error_at(this->named_object_->location(),
		   "use of undefined type %qs",
		   no->message_name().c_str());
	  this->warned_ = true;
	}
    }
  else
    {
      // The name was defined, but not as a type.
      if (!this->warned_)
	{
	  error_at(this->named_object_->location(), "expected type");
	  this->warned_ = true;
	}
    }
}

// Get the base type of a declaration.  This gives an error if the
// type has not yet been defined.

Type*
Forward_declaration_type::real_type()
{
  if (this->is_defined())
    return this->named_object()->type_value();
  else
    {
      this->warn();
      return Type::make_error_type();
    }
}

const Type*
Forward_declaration_type::real_type() const
{
  if (this->is_defined())
    return this->named_object()->type_value();
  else
    {
      this->warn();
      return Type::make_error_type();
    }
}

// Return whether the base type is defined.

bool
Forward_declaration_type::is_defined() const
{
  return this->named_object()->is_type();
}

// Add a method.  This is used when methods are defined before the
// type.

Named_object*
Forward_declaration_type::add_method(const std::string& name,
				     Function* function)
{
  Named_object* no = this->named_object();
  if (no->is_unknown())
    no->declare_as_type();
  return no->type_declaration_value()->add_method(name, function);
}

// Add a method declaration.  This is used when methods are declared
// before the type.

Named_object*
Forward_declaration_type::add_method_declaration(const std::string& name,
						 Package* package,
						 Function_type* type,
						 Location location)
{
  Named_object* no = this->named_object();
  if (no->is_unknown())
    no->declare_as_type();
  Type_declaration* td = no->type_declaration_value();
  return td->add_method_declaration(name, package, type, location);
}

// Traversal.

int
Forward_declaration_type::do_traverse(Traverse* traverse)
{
  if (this->is_defined()
      && Type::traverse(this->real_type(), traverse) == TRAVERSE_EXIT)
    return TRAVERSE_EXIT;
  return TRAVERSE_CONTINUE;
}

// Verify the type.

bool
Forward_declaration_type::do_verify()
{
  if (!this->is_defined() && !this->is_nil_constant_as_type())
    {
      this->warn();
      return false;
    }
  return true;
}

// Get the backend representation for the type.

Btype*
Forward_declaration_type::do_get_backend(Gogo* gogo)
{
  if (this->is_defined())
    return Type::get_named_base_btype(gogo, this->real_type());

  if (this->warned_)
    return gogo->backend()->error_type();

  // We represent an undefined type as a struct with no fields.  That
  // should work fine for the backend, since the same case can arise
  // in C.
  std::vector<Backend::Btyped_identifier> fields;
  Btype* bt = gogo->backend()->struct_type(fields);
  return gogo->backend()->named_type(this->name(), bt,
				     this->named_object()->location());
}

// Build a type descriptor for a forwarded type.

Expression*
Forward_declaration_type::do_type_descriptor(Gogo* gogo, Named_type* name)
{
  Location ploc = Linemap::predeclared_location();
  if (!this->is_defined())
    return Expression::make_error(ploc);
  else
    {
      Type* t = this->real_type();
      if (name != NULL)
	return this->named_type_descriptor(gogo, t, name);
      else
	return Expression::make_type_descriptor(t, ploc);
    }
}

// The reflection string.

void
Forward_declaration_type::do_reflection(Gogo* gogo, std::string* ret) const
{
  this->append_reflection(this->real_type(), gogo, ret);
}

// The mangled name.

void
Forward_declaration_type::do_mangled_name(Gogo* gogo, std::string* ret) const
{
  if (this->is_defined())
    this->append_mangled_name(this->real_type(), gogo, ret);
  else
    {
      const Named_object* no = this->named_object();
      std::string name;
      if (no->package() == NULL)
	name = gogo->pkgpath_symbol();
      else
	name = no->package()->pkgpath_symbol();
      name += '.';
      name += Gogo::unpack_hidden_name(no->name());
      char buf[20];
      snprintf(buf, sizeof buf, "N%u_",
	       static_cast<unsigned int>(name.length()));
      ret->append(buf);
      ret->append(name);
    }
}

// Export a forward declaration.  This can happen when a defined type
// refers to a type which is only declared (and is presumably defined
// in some other file in the same package).

void
Forward_declaration_type::do_export(Export*) const
{
  // If there is a base type, that should be exported instead of this.
  go_assert(!this->is_defined());

  // We don't output anything.
}

// Make a forward declaration.

Type*
Type::make_forward_declaration(Named_object* named_object)
{
  return new Forward_declaration_type(named_object);
}

// Class Typed_identifier_list.

// Sort the entries by name.

struct Typed_identifier_list_sort
{
 public:
  bool
  operator()(const Typed_identifier& t1, const Typed_identifier& t2) const
  { return t1.name() < t2.name(); }
};

void
Typed_identifier_list::sort_by_name()
{
  std::sort(this->entries_.begin(), this->entries_.end(),
	    Typed_identifier_list_sort());
}

// Traverse types.

int
Typed_identifier_list::traverse(Traverse* traverse)
{
  for (Typed_identifier_list::const_iterator p = this->begin();
       p != this->end();
       ++p)
    {
      if (Type::traverse(p->type(), traverse) == TRAVERSE_EXIT)
	return TRAVERSE_EXIT;
    }
  return TRAVERSE_CONTINUE;
}

// Copy the list.

Typed_identifier_list*
Typed_identifier_list::copy() const
{
  Typed_identifier_list* ret = new Typed_identifier_list();
  for (Typed_identifier_list::const_iterator p = this->begin();
       p != this->end();
       ++p)
    ret->push_back(Typed_identifier(p->name(), p->type(), p->location()));
  return ret;
}
