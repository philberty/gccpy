// Copyright 2009 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// DWARF type information structures.
// The format is heavily biased toward C, but for simplicity
// the String methods use a pseudo-Go syntax.

package dwarf

import "strconv"

// A Type conventionally represents a pointer to any of the
// specific Type structures (CharType, StructType, etc.).
type Type interface {
	Common() *CommonType
	String() string
	Size() int64
}

// A CommonType holds fields common to multiple types.
// If a field is not known or not applicable for a given type,
// the zero value is used.
type CommonType struct {
	ByteSize int64  // size of value of this type, in bytes
	Name     string // name that can be used to refer to type
}

func (c *CommonType) Common() *CommonType { return c }

func (c *CommonType) Size() int64 { return c.ByteSize }

// Basic types

// A BasicType holds fields common to all basic types.
type BasicType struct {
	CommonType
	BitSize   int64
	BitOffset int64
}

func (b *BasicType) Basic() *BasicType { return b }

func (t *BasicType) String() string {
	if t.Name != "" {
		return t.Name
	}
	return "?"
}

// A CharType represents a signed character type.
type CharType struct {
	BasicType
}

// A UcharType represents an unsigned character type.
type UcharType struct {
	BasicType
}

// An IntType represents a signed integer type.
type IntType struct {
	BasicType
}

// A UintType represents an unsigned integer type.
type UintType struct {
	BasicType
}

// A FloatType represents a floating point type.
type FloatType struct {
	BasicType
}

// A ComplexType represents a complex floating point type.
type ComplexType struct {
	BasicType
}

// A BoolType represents a boolean type.
type BoolType struct {
	BasicType
}

// An AddrType represents a machine address type.
type AddrType struct {
	BasicType
}

// qualifiers

// A QualType represents a type that has the C/C++ "const", "restrict", or "volatile" qualifier.
type QualType struct {
	CommonType
	Qual string
	Type Type
}

func (t *QualType) String() string { return t.Qual + " " + t.Type.String() }

func (t *QualType) Size() int64 { return t.Type.Size() }

// An ArrayType represents a fixed size array type.
type ArrayType struct {
	CommonType
	Type          Type
	StrideBitSize int64 // if > 0, number of bits to hold each element
	Count         int64 // if == -1, an incomplete array, like char x[].
}

func (t *ArrayType) String() string {
	return "[" + strconv.FormatInt(t.Count, 10) + "]" + t.Type.String()
}

func (t *ArrayType) Size() int64 { return t.Count * t.Type.Size() }

// A VoidType represents the C void type.
type VoidType struct {
	CommonType
}

func (t *VoidType) String() string { return "void" }

// A PtrType represents a pointer type.
type PtrType struct {
	CommonType
	Type Type
}

func (t *PtrType) String() string { return "*" + t.Type.String() }

// A StructType represents a struct, union, or C++ class type.
type StructType struct {
	CommonType
	StructName string
	Kind       string // "struct", "union", or "class".
	Field      []*StructField
	Incomplete bool // if true, struct, union, class is declared but not defined
}

// A StructField represents a field in a struct, union, or C++ class type.
type StructField struct {
	Name       string
	Type       Type
	ByteOffset int64
	ByteSize   int64
	BitOffset  int64 // within the ByteSize bytes at ByteOffset
	BitSize    int64 // zero if not a bit field
}

func (t *StructType) String() string {
	if t.StructName != "" {
		return t.Kind + " " + t.StructName
	}
	return t.Defn()
}

func (t *StructType) Defn() string {
	s := t.Kind
	if t.StructName != "" {
		s += " " + t.StructName
	}
	if t.Incomplete {
		s += " /*incomplete*/"
		return s
	}
	s += " {"
	for i, f := range t.Field {
		if i > 0 {
			s += "; "
		}
		s += f.Name + " " + f.Type.String()
		s += "@" + strconv.FormatInt(f.ByteOffset, 10)
		if f.BitSize > 0 {
			s += " : " + strconv.FormatInt(f.BitSize, 10)
			s += "@" + strconv.FormatInt(f.BitOffset, 10)
		}
	}
	s += "}"
	return s
}

// An EnumType represents an enumerated type.
// The only indication of its native integer type is its ByteSize
// (inside CommonType).
type EnumType struct {
	CommonType
	EnumName string
	Val      []*EnumValue
}

// An EnumValue represents a single enumeration value.
type EnumValue struct {
	Name string
	Val  int64
}

func (t *EnumType) String() string {
	s := "enum"
	if t.EnumName != "" {
		s += " " + t.EnumName
	}
	s += " {"
	for i, v := range t.Val {
		if i > 0 {
			s += "; "
		}
		s += v.Name + "=" + strconv.FormatInt(v.Val, 10)
	}
	s += "}"
	return s
}

// A FuncType represents a function type.
type FuncType struct {
	CommonType
	ReturnType Type
	ParamType  []Type
}

func (t *FuncType) String() string {
	s := "func("
	for i, t := range t.ParamType {
		if i > 0 {
			s += ", "
		}
		s += t.String()
	}
	s += ")"
	if t.ReturnType != nil {
		s += " " + t.ReturnType.String()
	}
	return s
}

// A DotDotDotType represents the variadic ... function parameter.
type DotDotDotType struct {
	CommonType
}

func (t *DotDotDotType) String() string { return "..." }

// A TypedefType represents a named type.
type TypedefType struct {
	CommonType
	Type Type
}

func (t *TypedefType) String() string { return t.Name }

func (t *TypedefType) Size() int64 { return t.Type.Size() }

func (d *Data) Type(off Offset) (Type, error) {
	if t, ok := d.typeCache[off]; ok {
		return t, nil
	}

	r := d.Reader()
	r.Seek(off)
	e, err := r.Next()
	if err != nil {
		return nil, err
	}
	if e == nil || e.Offset != off {
		return nil, DecodeError{"info", off, "no type at offset"}
	}

	// Parse type from Entry.
	// Must always set d.typeCache[off] before calling
	// d.Type recursively, to handle circular types correctly.
	var typ Type

	// Get next child; set err if error happens.
	next := func() *Entry {
		if !e.Children {
			return nil
		}
		kid, err1 := r.Next()
		if err1 != nil {
			err = err1
			return nil
		}
		if kid == nil {
			err = DecodeError{"info", r.b.off, "unexpected end of DWARF entries"}
			return nil
		}
		if kid.Tag == 0 {
			return nil
		}
		return kid
	}

	// Get Type referred to by Entry's AttrType field.
	// Set err if error happens.  Not having a type is an error.
	typeOf := func(e *Entry) Type {
		toff, ok := e.Val(AttrType).(Offset)
		if !ok {
			// It appears that no Type means "void".
			return new(VoidType)
		}
		var t Type
		if t, err = d.Type(toff); err != nil {
			return nil
		}
		return t
	}

	switch e.Tag {
	case TagArrayType:
		// Multi-dimensional array.  (DWARF v2 §5.4)
		// Attributes:
		//	AttrType:subtype [required]
		//	AttrStrideSize: size in bits of each element of the array
		//	AttrByteSize: size of entire array
		// Children:
		//	TagSubrangeType or TagEnumerationType giving one dimension.
		//	dimensions are in left to right order.
		t := new(ArrayType)
		typ = t
		d.typeCache[off] = t
		if t.Type = typeOf(e); err != nil {
			goto Error
		}
		t.StrideBitSize, _ = e.Val(AttrStrideSize).(int64)

		// Accumulate dimensions,
		ndim := 0
		for kid := next(); kid != nil; kid = next() {
			// TODO(rsc): Can also be TagEnumerationType
			// but haven't seen that in the wild yet.
			switch kid.Tag {
			case TagSubrangeType:
				max, ok := kid.Val(AttrUpperBound).(int64)
				if !ok {
					max = -2 // Count == -1, as in x[].
				}
				if ndim == 0 {
					t.Count = max + 1
				} else {
					// Multidimensional array.
					// Create new array type underneath this one.
					t.Type = &ArrayType{Type: t.Type, Count: max + 1}
				}
				ndim++
			case TagEnumerationType:
				err = DecodeError{"info", kid.Offset, "cannot handle enumeration type as array bound"}
				goto Error
			}
		}
		if ndim == 0 {
			// LLVM generates this for x[].
			t.Count = -1
		}

	case TagBaseType:
		// Basic type.  (DWARF v2 §5.1)
		// Attributes:
		//	AttrName: name of base type in programming language of the compilation unit [required]
		//	AttrEncoding: encoding value for type (encFloat etc) [required]
		//	AttrByteSize: size of type in bytes [required]
		//	AttrBitOffset: for sub-byte types, size in bits
		//	AttrBitSize: for sub-byte types, bit offset of high order bit in the AttrByteSize bytes
		name, _ := e.Val(AttrName).(string)
		enc, ok := e.Val(AttrEncoding).(int64)
		if !ok {
			err = DecodeError{"info", e.Offset, "missing encoding attribute for " + name}
			goto Error
		}
		switch enc {
		default:
			err = DecodeError{"info", e.Offset, "unrecognized encoding attribute value"}
			goto Error

		case encAddress:
			typ = new(AddrType)
		case encBoolean:
			typ = new(BoolType)
		case encComplexFloat:
			typ = new(ComplexType)
		case encFloat:
			typ = new(FloatType)
		case encSigned:
			typ = new(IntType)
		case encUnsigned:
			typ = new(UintType)
		case encSignedChar:
			typ = new(CharType)
		case encUnsignedChar:
			typ = new(UcharType)
		}
		d.typeCache[off] = typ
		t := typ.(interface {
			Basic() *BasicType
		}).Basic()
		t.Name = name
		t.BitSize, _ = e.Val(AttrBitSize).(int64)
		t.BitOffset, _ = e.Val(AttrBitOffset).(int64)

	case TagClassType, TagStructType, TagUnionType:
		// Structure, union, or class type.  (DWARF v2 §5.5)
		// Attributes:
		//	AttrName: name of struct, union, or class
		//	AttrByteSize: byte size [required]
		//	AttrDeclaration: if true, struct/union/class is incomplete
		// Children:
		//	TagMember to describe one member.
		//		AttrName: name of member [required]
		//		AttrType: type of member [required]
		//		AttrByteSize: size in bytes
		//		AttrBitOffset: bit offset within bytes for bit fields
		//		AttrBitSize: bit size for bit fields
		//		AttrDataMemberLoc: location within struct [required for struct, class]
		// There is much more to handle C++, all ignored for now.
		t := new(StructType)
		typ = t
		d.typeCache[off] = t
		switch e.Tag {
		case TagClassType:
			t.Kind = "class"
		case TagStructType:
			t.Kind = "struct"
		case TagUnionType:
			t.Kind = "union"
		}
		t.StructName, _ = e.Val(AttrName).(string)
		t.Incomplete = e.Val(AttrDeclaration) != nil
		t.Field = make([]*StructField, 0, 8)
		var lastFieldType Type
		var lastFieldBitOffset int64
		for kid := next(); kid != nil; kid = next() {
			if kid.Tag == TagMember {
				f := new(StructField)
				if f.Type = typeOf(kid); err != nil {
					goto Error
				}
				if loc, ok := kid.Val(AttrDataMemberLoc).([]byte); ok {
					// TODO: Should have original compilation
					// unit here, not unknownFormat.
					b := makeBuf(d, unknownFormat{}, "location", 0, loc)
					if b.uint8() != opPlusUconst {
						err = DecodeError{"info", kid.Offset, "unexpected opcode"}
						goto Error
					}
					f.ByteOffset = int64(b.uint())
					if b.err != nil {
						err = b.err
						goto Error
					}
				}

				haveBitOffset := false
				f.Name, _ = kid.Val(AttrName).(string)
				f.ByteSize, _ = kid.Val(AttrByteSize).(int64)
				f.BitOffset, haveBitOffset = kid.Val(AttrBitOffset).(int64)
				f.BitSize, _ = kid.Val(AttrBitSize).(int64)
				t.Field = append(t.Field, f)

				bito := f.BitOffset
				if !haveBitOffset {
					bito = f.ByteOffset * 8
				}
				if bito == lastFieldBitOffset && t.Kind != "union" {
					// Last field was zero width.  Fix array length.
					// (DWARF writes out 0-length arrays as if they were 1-length arrays.)
					zeroArray(lastFieldType)
				}
				lastFieldType = f.Type
				lastFieldBitOffset = bito
			}
		}
		if t.Kind != "union" {
			b, ok := e.Val(AttrByteSize).(int64)
			if ok && b*8 == lastFieldBitOffset {
				// Final field must be zero width.  Fix array length.
				zeroArray(lastFieldType)
			}
		}

	case TagConstType, TagVolatileType, TagRestrictType:
		// Type modifier (DWARF v2 §5.2)
		// Attributes:
		//	AttrType: subtype
		t := new(QualType)
		typ = t
		d.typeCache[off] = t
		if t.Type = typeOf(e); err != nil {
			goto Error
		}
		switch e.Tag {
		case TagConstType:
			t.Qual = "const"
		case TagRestrictType:
			t.Qual = "restrict"
		case TagVolatileType:
			t.Qual = "volatile"
		}

	case TagEnumerationType:
		// Enumeration type (DWARF v2 §5.6)
		// Attributes:
		//	AttrName: enum name if any
		//	AttrByteSize: bytes required to represent largest value
		// Children:
		//	TagEnumerator:
		//		AttrName: name of constant
		//		AttrConstValue: value of constant
		t := new(EnumType)
		typ = t
		d.typeCache[off] = t
		t.EnumName, _ = e.Val(AttrName).(string)
		t.Val = make([]*EnumValue, 0, 8)
		for kid := next(); kid != nil; kid = next() {
			if kid.Tag == TagEnumerator {
				f := new(EnumValue)
				f.Name, _ = kid.Val(AttrName).(string)
				f.Val, _ = kid.Val(AttrConstValue).(int64)
				n := len(t.Val)
				if n >= cap(t.Val) {
					val := make([]*EnumValue, n, n*2)
					copy(val, t.Val)
					t.Val = val
				}
				t.Val = t.Val[0 : n+1]
				t.Val[n] = f
			}
		}

	case TagPointerType:
		// Type modifier (DWARF v2 §5.2)
		// Attributes:
		//	AttrType: subtype [not required!  void* has no AttrType]
		//	AttrAddrClass: address class [ignored]
		t := new(PtrType)
		typ = t
		d.typeCache[off] = t
		if e.Val(AttrType) == nil {
			t.Type = &VoidType{}
			break
		}
		t.Type = typeOf(e)

	case TagSubroutineType:
		// Subroutine type.  (DWARF v2 §5.7)
		// Attributes:
		//	AttrType: type of return value if any
		//	AttrName: possible name of type [ignored]
		//	AttrPrototyped: whether used ANSI C prototype [ignored]
		// Children:
		//	TagFormalParameter: typed parameter
		//		AttrType: type of parameter
		//	TagUnspecifiedParameter: final ...
		t := new(FuncType)
		typ = t
		d.typeCache[off] = t
		if t.ReturnType = typeOf(e); err != nil {
			goto Error
		}
		t.ParamType = make([]Type, 0, 8)
		for kid := next(); kid != nil; kid = next() {
			var tkid Type
			switch kid.Tag {
			default:
				continue
			case TagFormalParameter:
				if tkid = typeOf(kid); err != nil {
					goto Error
				}
			case TagUnspecifiedParameters:
				tkid = &DotDotDotType{}
			}
			t.ParamType = append(t.ParamType, tkid)
		}

	case TagTypedef:
		// Typedef (DWARF v2 §5.3)
		// Attributes:
		//	AttrName: name [required]
		//	AttrType: type definition [required]
		t := new(TypedefType)
		typ = t
		d.typeCache[off] = t
		t.Name, _ = e.Val(AttrName).(string)
		t.Type = typeOf(e)
	}

	if err != nil {
		goto Error
	}

	{
		b, ok := e.Val(AttrByteSize).(int64)
		if !ok {
			b = -1
		}
		typ.Common().ByteSize = b
	}
	return typ, nil

Error:
	// If the parse fails, take the type out of the cache
	// so that the next call with this offset doesn't hit
	// the cache and return success.
	delete(d.typeCache, off)
	return nil, err
}

func zeroArray(t Type) {
	for {
		at, ok := t.(*ArrayType)
		if !ok {
			break
		}
		at.Count = 0
		t = at.Type
	}
}
