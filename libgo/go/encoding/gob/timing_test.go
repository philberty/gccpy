// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package gob

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"runtime"
	"testing"
)

type Bench struct {
	A int
	B float64
	C string
	D []byte
}

func benchmarkEndToEnd(r io.Reader, w io.Writer, b *testing.B) {
	b.StopTimer()
	enc := NewEncoder(w)
	dec := NewDecoder(r)
	bench := &Bench{7, 3.2, "now is the time", []byte("for all good men")}
	b.StartTimer()
	for i := 0; i < b.N; i++ {
		if enc.Encode(bench) != nil {
			panic("encode error")
		}
		if dec.Decode(bench) != nil {
			panic("decode error")
		}
	}
}

func BenchmarkEndToEndPipe(b *testing.B) {
	r, w, err := os.Pipe()
	if err != nil {
		b.Fatal("can't get pipe:", err)
	}
	benchmarkEndToEnd(r, w, b)
}

func BenchmarkEndToEndByteBuffer(b *testing.B) {
	var buf bytes.Buffer
	benchmarkEndToEnd(&buf, &buf, b)
}

func TestCountEncodeMallocs(t *testing.T) {
	if runtime.GOMAXPROCS(0) > 1 {
		t.Skip("skipping; GOMAXPROCS>1")
	}

	const N = 1000

	var buf bytes.Buffer
	enc := NewEncoder(&buf)
	bench := &Bench{7, 3.2, "now is the time", []byte("for all good men")}

	allocs := testing.AllocsPerRun(N, func() {
		err := enc.Encode(bench)
		if err != nil {
			t.Fatal("encode:", err)
		}
	})
	fmt.Printf("mallocs per encode of type Bench: %v\n", allocs)
}

func TestCountDecodeMallocs(t *testing.T) {
	if runtime.GOMAXPROCS(0) > 1 {
		t.Skip("skipping; GOMAXPROCS>1")
	}

	const N = 1000

	var buf bytes.Buffer
	enc := NewEncoder(&buf)
	bench := &Bench{7, 3.2, "now is the time", []byte("for all good men")}

	// Fill the buffer with enough to decode
	testing.AllocsPerRun(N, func() {
		err := enc.Encode(bench)
		if err != nil {
			t.Fatal("encode:", err)
		}
	})

	dec := NewDecoder(&buf)
	allocs := testing.AllocsPerRun(N, func() {
		*bench = Bench{}
		err := dec.Decode(&bench)
		if err != nil {
			t.Fatal("decode:", err)
		}
	})
	fmt.Printf("mallocs per decode of type Bench: %v\n", allocs)
}
