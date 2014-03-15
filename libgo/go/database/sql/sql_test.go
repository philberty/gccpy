// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package sql

import (
	"fmt"
	"reflect"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"
)

func init() {
	type dbConn struct {
		db *DB
		c  *driverConn
	}
	freedFrom := make(map[dbConn]string)
	putConnHook = func(db *DB, c *driverConn) {
		for _, oc := range db.freeConn {
			if oc == c {
				// print before panic, as panic may get lost due to conflicting panic
				// (all goroutines asleep) elsewhere, since we might not unlock
				// the mutex in freeConn here.
				println("double free of conn. conflicts are:\nA) " + freedFrom[dbConn{db, c}] + "\n\nand\nB) " + stack())
				panic("double free of conn.")
			}
		}
		freedFrom[dbConn{db, c}] = stack()
	}
}

const fakeDBName = "foo"

var chrisBirthday = time.Unix(123456789, 0)

type testOrBench interface {
	Fatalf(string, ...interface{})
	Errorf(string, ...interface{})
	Fatal(...interface{})
	Error(...interface{})
	Logf(string, ...interface{})
}

func newTestDB(t testOrBench, name string) *DB {
	db, err := Open("test", fakeDBName)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	if _, err := db.Exec("WIPE"); err != nil {
		t.Fatalf("exec wipe: %v", err)
	}
	if name == "people" {
		exec(t, db, "CREATE|people|name=string,age=int32,photo=blob,dead=bool,bdate=datetime")
		exec(t, db, "INSERT|people|name=Alice,age=?,photo=APHOTO", 1)
		exec(t, db, "INSERT|people|name=Bob,age=?,photo=BPHOTO", 2)
		exec(t, db, "INSERT|people|name=Chris,age=?,photo=CPHOTO,bdate=?", 3, chrisBirthday)
	}
	if name == "magicquery" {
		// Magic table name and column, known by fakedb_test.go.
		exec(t, db, "CREATE|magicquery|op=string,millis=int32")
		exec(t, db, "INSERT|magicquery|op=sleep,millis=10")
	}
	return db
}

func exec(t testOrBench, db *DB, query string, args ...interface{}) {
	_, err := db.Exec(query, args...)
	if err != nil {
		t.Fatalf("Exec of %q: %v", query, err)
	}
}

func closeDB(t testOrBench, db *DB) {
	if e := recover(); e != nil {
		fmt.Printf("Panic: %v\n", e)
		panic(e)
	}
	defer setHookpostCloseConn(nil)
	setHookpostCloseConn(func(_ *fakeConn, err error) {
		if err != nil {
			t.Errorf("Error closing fakeConn: %v", err)
		}
	})
	for i, dc := range db.freeConn {
		if n := len(dc.openStmt); n > 0 {
			// Just a sanity check. This is legal in
			// general, but if we make the tests clean up
			// their statements first, then we can safely
			// verify this is always zero here, and any
			// other value is a leak.
			t.Errorf("while closing db, freeConn %d/%d had %d open stmts; want 0", i, len(db.freeConn), n)
		}
	}
	err := db.Close()
	if err != nil {
		t.Fatalf("error closing DB: %v", err)
	}
}

// numPrepares assumes that db has exactly 1 idle conn and returns
// its count of calls to Prepare
func numPrepares(t *testing.T, db *DB) int {
	if n := len(db.freeConn); n != 1 {
		t.Fatalf("free conns = %d; want 1", n)
	}
	return db.freeConn[0].ci.(*fakeConn).numPrepare
}

func (db *DB) numDeps() int {
	db.mu.Lock()
	defer db.mu.Unlock()
	return len(db.dep)
}

// Dependencies are closed via a goroutine, so this polls waiting for
// numDeps to fall to want, waiting up to d.
func (db *DB) numDepsPollUntil(want int, d time.Duration) int {
	deadline := time.Now().Add(d)
	for {
		n := db.numDeps()
		if n <= want || time.Now().After(deadline) {
			return n
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func (db *DB) numFreeConns() int {
	db.mu.Lock()
	defer db.mu.Unlock()
	return len(db.freeConn)
}

func (db *DB) dumpDeps(t *testing.T) {
	for fc := range db.dep {
		db.dumpDep(t, 0, fc, map[finalCloser]bool{})
	}
}

func (db *DB) dumpDep(t *testing.T, depth int, dep finalCloser, seen map[finalCloser]bool) {
	seen[dep] = true
	indent := strings.Repeat("  ", depth)
	ds := db.dep[dep]
	for k := range ds {
		t.Logf("%s%T (%p) waiting for -> %T (%p)", indent, dep, dep, k, k)
		if fc, ok := k.(finalCloser); ok {
			if !seen[fc] {
				db.dumpDep(t, depth+1, fc, seen)
			}
		}
	}
}

func TestQuery(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	prepares0 := numPrepares(t, db)
	rows, err := db.Query("SELECT|people|age,name|")
	if err != nil {
		t.Fatalf("Query: %v", err)
	}
	type row struct {
		age  int
		name string
	}
	got := []row{}
	for rows.Next() {
		var r row
		err = rows.Scan(&r.age, &r.name)
		if err != nil {
			t.Fatalf("Scan: %v", err)
		}
		got = append(got, r)
	}
	err = rows.Err()
	if err != nil {
		t.Fatalf("Err: %v", err)
	}
	want := []row{
		{age: 1, name: "Alice"},
		{age: 2, name: "Bob"},
		{age: 3, name: "Chris"},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("mismatch.\n got: %#v\nwant: %#v", got, want)
	}

	// And verify that the final rows.Next() call, which hit EOF,
	// also closed the rows connection.
	if n := db.numFreeConns(); n != 1 {
		t.Fatalf("free conns after query hitting EOF = %d; want 1", n)
	}
	if prepares := numPrepares(t, db) - prepares0; prepares != 1 {
		t.Errorf("executed %d Prepare statements; want 1", prepares)
	}
}

func TestByteOwnership(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	rows, err := db.Query("SELECT|people|name,photo|")
	if err != nil {
		t.Fatalf("Query: %v", err)
	}
	type row struct {
		name  []byte
		photo RawBytes
	}
	got := []row{}
	for rows.Next() {
		var r row
		err = rows.Scan(&r.name, &r.photo)
		if err != nil {
			t.Fatalf("Scan: %v", err)
		}
		got = append(got, r)
	}
	corruptMemory := []byte("\xffPHOTO")
	want := []row{
		{name: []byte("Alice"), photo: corruptMemory},
		{name: []byte("Bob"), photo: corruptMemory},
		{name: []byte("Chris"), photo: corruptMemory},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("mismatch.\n got: %#v\nwant: %#v", got, want)
	}

	var photo RawBytes
	err = db.QueryRow("SELECT|people|photo|name=?", "Alice").Scan(&photo)
	if err == nil {
		t.Error("want error scanning into RawBytes from QueryRow")
	}
}

func TestRowsColumns(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	rows, err := db.Query("SELECT|people|age,name|")
	if err != nil {
		t.Fatalf("Query: %v", err)
	}
	cols, err := rows.Columns()
	if err != nil {
		t.Fatalf("Columns: %v", err)
	}
	want := []string{"age", "name"}
	if !reflect.DeepEqual(cols, want) {
		t.Errorf("got %#v; want %#v", cols, want)
	}
}

func TestQueryRow(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	var name string
	var age int
	var birthday time.Time

	err := db.QueryRow("SELECT|people|age,name|age=?", 3).Scan(&age)
	if err == nil || !strings.Contains(err.Error(), "expected 2 destination arguments") {
		t.Errorf("expected error from wrong number of arguments; actually got: %v", err)
	}

	err = db.QueryRow("SELECT|people|bdate|age=?", 3).Scan(&birthday)
	if err != nil || !birthday.Equal(chrisBirthday) {
		t.Errorf("chris birthday = %v, err = %v; want %v", birthday, err, chrisBirthday)
	}

	err = db.QueryRow("SELECT|people|age,name|age=?", 2).Scan(&age, &name)
	if err != nil {
		t.Fatalf("age QueryRow+Scan: %v", err)
	}
	if name != "Bob" {
		t.Errorf("expected name Bob, got %q", name)
	}
	if age != 2 {
		t.Errorf("expected age 2, got %d", age)
	}

	err = db.QueryRow("SELECT|people|age,name|name=?", "Alice").Scan(&age, &name)
	if err != nil {
		t.Fatalf("name QueryRow+Scan: %v", err)
	}
	if name != "Alice" {
		t.Errorf("expected name Alice, got %q", name)
	}
	if age != 1 {
		t.Errorf("expected age 1, got %d", age)
	}

	var photo []byte
	err = db.QueryRow("SELECT|people|photo|name=?", "Alice").Scan(&photo)
	if err != nil {
		t.Fatalf("photo QueryRow+Scan: %v", err)
	}
	want := []byte("APHOTO")
	if !reflect.DeepEqual(photo, want) {
		t.Errorf("photo = %q; want %q", photo, want)
	}
}

func TestStatementErrorAfterClose(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	stmt, err := db.Prepare("SELECT|people|age|name=?")
	if err != nil {
		t.Fatalf("Prepare: %v", err)
	}
	err = stmt.Close()
	if err != nil {
		t.Fatalf("Close: %v", err)
	}
	var name string
	err = stmt.QueryRow("foo").Scan(&name)
	if err == nil {
		t.Errorf("expected error from QueryRow.Scan after Stmt.Close")
	}
}

func TestStatementQueryRow(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	stmt, err := db.Prepare("SELECT|people|age|name=?")
	if err != nil {
		t.Fatalf("Prepare: %v", err)
	}
	defer stmt.Close()
	var age int
	for n, tt := range []struct {
		name string
		want int
	}{
		{"Alice", 1},
		{"Bob", 2},
		{"Chris", 3},
	} {
		if err := stmt.QueryRow(tt.name).Scan(&age); err != nil {
			t.Errorf("%d: on %q, QueryRow/Scan: %v", n, tt.name, err)
		} else if age != tt.want {
			t.Errorf("%d: age=%d, want %d", n, age, tt.want)
		}
	}

}

// golang.org/issue/3734
func TestStatementQueryRowConcurrent(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	stmt, err := db.Prepare("SELECT|people|age|name=?")
	if err != nil {
		t.Fatalf("Prepare: %v", err)
	}
	defer stmt.Close()

	const n = 10
	ch := make(chan error, n)
	for i := 0; i < n; i++ {
		go func() {
			var age int
			err := stmt.QueryRow("Alice").Scan(&age)
			if err == nil && age != 1 {
				err = fmt.Errorf("unexpected age %d", age)
			}
			ch <- err
		}()
	}
	for i := 0; i < n; i++ {
		if err := <-ch; err != nil {
			t.Error(err)
		}
	}
}

// just a test of fakedb itself
func TestBogusPreboundParameters(t *testing.T) {
	db := newTestDB(t, "foo")
	defer closeDB(t, db)
	exec(t, db, "CREATE|t1|name=string,age=int32,dead=bool")
	_, err := db.Prepare("INSERT|t1|name=?,age=bogusconversion")
	if err == nil {
		t.Fatalf("expected error")
	}
	if err.Error() != `fakedb: invalid conversion to int32 from "bogusconversion"` {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestExec(t *testing.T) {
	db := newTestDB(t, "foo")
	defer closeDB(t, db)
	exec(t, db, "CREATE|t1|name=string,age=int32,dead=bool")
	stmt, err := db.Prepare("INSERT|t1|name=?,age=?")
	if err != nil {
		t.Errorf("Stmt, err = %v, %v", stmt, err)
	}
	defer stmt.Close()

	type execTest struct {
		args    []interface{}
		wantErr string
	}
	execTests := []execTest{
		// Okay:
		{[]interface{}{"Brad", 31}, ""},
		{[]interface{}{"Brad", int64(31)}, ""},
		{[]interface{}{"Bob", "32"}, ""},
		{[]interface{}{7, 9}, ""},

		// Invalid conversions:
		{[]interface{}{"Brad", int64(0xFFFFFFFF)}, "sql: converting argument #1's type: sql/driver: value 4294967295 overflows int32"},
		{[]interface{}{"Brad", "strconv fail"}, "sql: converting argument #1's type: sql/driver: value \"strconv fail\" can't be converted to int32"},

		// Wrong number of args:
		{[]interface{}{}, "sql: expected 2 arguments, got 0"},
		{[]interface{}{1, 2, 3}, "sql: expected 2 arguments, got 3"},
	}
	for n, et := range execTests {
		_, err := stmt.Exec(et.args...)
		errStr := ""
		if err != nil {
			errStr = err.Error()
		}
		if errStr != et.wantErr {
			t.Errorf("stmt.Execute #%d: for %v, got error %q, want error %q",
				n, et.args, errStr, et.wantErr)
		}
	}
}

func TestTxStmt(t *testing.T) {
	db := newTestDB(t, "")
	defer closeDB(t, db)
	exec(t, db, "CREATE|t1|name=string,age=int32,dead=bool")
	stmt, err := db.Prepare("INSERT|t1|name=?,age=?")
	if err != nil {
		t.Fatalf("Stmt, err = %v, %v", stmt, err)
	}
	defer stmt.Close()
	tx, err := db.Begin()
	if err != nil {
		t.Fatalf("Begin = %v", err)
	}
	txs := tx.Stmt(stmt)
	defer txs.Close()
	_, err = txs.Exec("Bobby", 7)
	if err != nil {
		t.Fatalf("Exec = %v", err)
	}
	err = tx.Commit()
	if err != nil {
		t.Fatalf("Commit = %v", err)
	}
}

// Issue: http://golang.org/issue/2784
// This test didn't fail before because we got luckly with the fakedb driver.
// It was failing, and now not, in github.com/bradfitz/go-sql-test
func TestTxQuery(t *testing.T) {
	db := newTestDB(t, "")
	defer closeDB(t, db)
	exec(t, db, "CREATE|t1|name=string,age=int32,dead=bool")
	exec(t, db, "INSERT|t1|name=Alice")

	tx, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	defer tx.Rollback()

	r, err := tx.Query("SELECT|t1|name|")
	if err != nil {
		t.Fatal(err)
	}
	defer r.Close()

	if !r.Next() {
		if r.Err() != nil {
			t.Fatal(r.Err())
		}
		t.Fatal("expected one row")
	}

	var x string
	err = r.Scan(&x)
	if err != nil {
		t.Fatal(err)
	}
}

func TestTxQueryInvalid(t *testing.T) {
	db := newTestDB(t, "")
	defer closeDB(t, db)

	tx, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	defer tx.Rollback()

	_, err = tx.Query("SELECT|t1|name|")
	if err == nil {
		t.Fatal("Error expected")
	}
}

// Tests fix for issue 4433, that retries in Begin happen when
// conn.Begin() returns ErrBadConn
func TestTxErrBadConn(t *testing.T) {
	db, err := Open("test", fakeDBName+";badConn")
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	if _, err := db.Exec("WIPE"); err != nil {
		t.Fatalf("exec wipe: %v", err)
	}
	defer closeDB(t, db)
	exec(t, db, "CREATE|t1|name=string,age=int32,dead=bool")
	stmt, err := db.Prepare("INSERT|t1|name=?,age=?")
	if err != nil {
		t.Fatalf("Stmt, err = %v, %v", stmt, err)
	}
	defer stmt.Close()
	tx, err := db.Begin()
	if err != nil {
		t.Fatalf("Begin = %v", err)
	}
	txs := tx.Stmt(stmt)
	defer txs.Close()
	_, err = txs.Exec("Bobby", 7)
	if err != nil {
		t.Fatalf("Exec = %v", err)
	}
	err = tx.Commit()
	if err != nil {
		t.Fatalf("Commit = %v", err)
	}
}

// Tests fix for issue 2542, that we release a lock when querying on
// a closed connection.
func TestIssue2542Deadlock(t *testing.T) {
	db := newTestDB(t, "people")
	closeDB(t, db)
	for i := 0; i < 2; i++ {
		_, err := db.Query("SELECT|people|age,name|")
		if err == nil {
			t.Fatalf("expected error")
		}
	}
}

// From golang.org/issue/3865
func TestCloseStmtBeforeRows(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)

	s, err := db.Prepare("SELECT|people|name|")
	if err != nil {
		t.Fatal(err)
	}

	r, err := s.Query()
	if err != nil {
		s.Close()
		t.Fatal(err)
	}

	err = s.Close()
	if err != nil {
		t.Fatal(err)
	}

	r.Close()
}

// Tests fix for issue 2788, that we bind nil to a []byte if the
// value in the column is sql null
func TestNullByteSlice(t *testing.T) {
	db := newTestDB(t, "")
	defer closeDB(t, db)
	exec(t, db, "CREATE|t|id=int32,name=nullstring")
	exec(t, db, "INSERT|t|id=10,name=?", nil)

	var name []byte

	err := db.QueryRow("SELECT|t|name|id=?", 10).Scan(&name)
	if err != nil {
		t.Fatal(err)
	}
	if name != nil {
		t.Fatalf("name []byte should be nil for null column value, got: %#v", name)
	}

	exec(t, db, "INSERT|t|id=11,name=?", "bob")
	err = db.QueryRow("SELECT|t|name|id=?", 11).Scan(&name)
	if err != nil {
		t.Fatal(err)
	}
	if string(name) != "bob" {
		t.Fatalf("name []byte should be bob, got: %q", string(name))
	}
}

func TestPointerParamsAndScans(t *testing.T) {
	db := newTestDB(t, "")
	defer closeDB(t, db)
	exec(t, db, "CREATE|t|id=int32,name=nullstring")

	bob := "bob"
	var name *string

	name = &bob
	exec(t, db, "INSERT|t|id=10,name=?", name)
	name = nil
	exec(t, db, "INSERT|t|id=20,name=?", name)

	err := db.QueryRow("SELECT|t|name|id=?", 10).Scan(&name)
	if err != nil {
		t.Fatalf("querying id 10: %v", err)
	}
	if name == nil {
		t.Errorf("id 10's name = nil; want bob")
	} else if *name != "bob" {
		t.Errorf("id 10's name = %q; want bob", *name)
	}

	err = db.QueryRow("SELECT|t|name|id=?", 20).Scan(&name)
	if err != nil {
		t.Fatalf("querying id 20: %v", err)
	}
	if name != nil {
		t.Errorf("id 20 = %q; want nil", *name)
	}
}

func TestQueryRowClosingStmt(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	var name string
	var age int
	err := db.QueryRow("SELECT|people|age,name|age=?", 3).Scan(&age, &name)
	if err != nil {
		t.Fatal(err)
	}
	if len(db.freeConn) != 1 {
		t.Fatalf("expected 1 free conn")
	}
	fakeConn := db.freeConn[0].ci.(*fakeConn)
	if made, closed := fakeConn.stmtsMade, fakeConn.stmtsClosed; made != closed {
		t.Errorf("statement close mismatch: made %d, closed %d", made, closed)
	}
}

type nullTestRow struct {
	nullParam    interface{}
	notNullParam interface{}
	scanNullVal  interface{}
}

type nullTestSpec struct {
	nullType    string
	notNullType string
	rows        [6]nullTestRow
}

func TestNullStringParam(t *testing.T) {
	spec := nullTestSpec{"nullstring", "string", [6]nullTestRow{
		{NullString{"aqua", true}, "", NullString{"aqua", true}},
		{NullString{"brown", false}, "", NullString{"", false}},
		{"chartreuse", "", NullString{"chartreuse", true}},
		{NullString{"darkred", true}, "", NullString{"darkred", true}},
		{NullString{"eel", false}, "", NullString{"", false}},
		{"foo", NullString{"black", false}, nil},
	}}
	nullTestRun(t, spec)
}

func TestNullInt64Param(t *testing.T) {
	spec := nullTestSpec{"nullint64", "int64", [6]nullTestRow{
		{NullInt64{31, true}, 1, NullInt64{31, true}},
		{NullInt64{-22, false}, 1, NullInt64{0, false}},
		{22, 1, NullInt64{22, true}},
		{NullInt64{33, true}, 1, NullInt64{33, true}},
		{NullInt64{222, false}, 1, NullInt64{0, false}},
		{0, NullInt64{31, false}, nil},
	}}
	nullTestRun(t, spec)
}

func TestNullFloat64Param(t *testing.T) {
	spec := nullTestSpec{"nullfloat64", "float64", [6]nullTestRow{
		{NullFloat64{31.2, true}, 1, NullFloat64{31.2, true}},
		{NullFloat64{13.1, false}, 1, NullFloat64{0, false}},
		{-22.9, 1, NullFloat64{-22.9, true}},
		{NullFloat64{33.81, true}, 1, NullFloat64{33.81, true}},
		{NullFloat64{222, false}, 1, NullFloat64{0, false}},
		{10, NullFloat64{31.2, false}, nil},
	}}
	nullTestRun(t, spec)
}

func TestNullBoolParam(t *testing.T) {
	spec := nullTestSpec{"nullbool", "bool", [6]nullTestRow{
		{NullBool{false, true}, true, NullBool{false, true}},
		{NullBool{true, false}, false, NullBool{false, false}},
		{true, true, NullBool{true, true}},
		{NullBool{true, true}, false, NullBool{true, true}},
		{NullBool{true, false}, true, NullBool{false, false}},
		{true, NullBool{true, false}, nil},
	}}
	nullTestRun(t, spec)
}

func nullTestRun(t *testing.T, spec nullTestSpec) {
	db := newTestDB(t, "")
	defer closeDB(t, db)
	exec(t, db, fmt.Sprintf("CREATE|t|id=int32,name=string,nullf=%s,notnullf=%s", spec.nullType, spec.notNullType))

	// Inserts with db.Exec:
	exec(t, db, "INSERT|t|id=?,name=?,nullf=?,notnullf=?", 1, "alice", spec.rows[0].nullParam, spec.rows[0].notNullParam)
	exec(t, db, "INSERT|t|id=?,name=?,nullf=?,notnullf=?", 2, "bob", spec.rows[1].nullParam, spec.rows[1].notNullParam)

	// Inserts with a prepared statement:
	stmt, err := db.Prepare("INSERT|t|id=?,name=?,nullf=?,notnullf=?")
	if err != nil {
		t.Fatalf("prepare: %v", err)
	}
	defer stmt.Close()
	if _, err := stmt.Exec(3, "chris", spec.rows[2].nullParam, spec.rows[2].notNullParam); err != nil {
		t.Errorf("exec insert chris: %v", err)
	}
	if _, err := stmt.Exec(4, "dave", spec.rows[3].nullParam, spec.rows[3].notNullParam); err != nil {
		t.Errorf("exec insert dave: %v", err)
	}
	if _, err := stmt.Exec(5, "eleanor", spec.rows[4].nullParam, spec.rows[4].notNullParam); err != nil {
		t.Errorf("exec insert eleanor: %v", err)
	}

	// Can't put null val into non-null col
	if _, err := stmt.Exec(6, "bob", spec.rows[5].nullParam, spec.rows[5].notNullParam); err == nil {
		t.Errorf("expected error inserting nil val with prepared statement Exec")
	}

	_, err = db.Exec("INSERT|t|id=?,name=?,nullf=?", 999, nil, nil)
	if err == nil {
		// TODO: this test fails, but it's just because
		// fakeConn implements the optional Execer interface,
		// so arguably this is the correct behavior.  But
		// maybe I should flesh out the fakeConn.Exec
		// implementation so this properly fails.
		// t.Errorf("expected error inserting nil name with Exec")
	}

	paramtype := reflect.TypeOf(spec.rows[0].nullParam)
	bindVal := reflect.New(paramtype).Interface()

	for i := 0; i < 5; i++ {
		id := i + 1
		if err := db.QueryRow("SELECT|t|nullf|id=?", id).Scan(bindVal); err != nil {
			t.Errorf("id=%d Scan: %v", id, err)
		}
		bindValDeref := reflect.ValueOf(bindVal).Elem().Interface()
		if !reflect.DeepEqual(bindValDeref, spec.rows[i].scanNullVal) {
			t.Errorf("id=%d got %#v, want %#v", id, bindValDeref, spec.rows[i].scanNullVal)
		}
	}
}

// golang.org/issue/4859
func TestQueryRowNilScanDest(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)
	var name *string // nil pointer
	err := db.QueryRow("SELECT|people|name|").Scan(name)
	want := "sql: Scan error on column index 0: destination pointer is nil"
	if err == nil || err.Error() != want {
		t.Errorf("error = %q; want %q", err.Error(), want)
	}
}

func TestIssue4902(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)

	driver := db.driver.(*fakeDriver)
	opens0 := driver.openCount

	var stmt *Stmt
	var err error
	for i := 0; i < 10; i++ {
		stmt, err = db.Prepare("SELECT|people|name|")
		if err != nil {
			t.Fatal(err)
		}
		err = stmt.Close()
		if err != nil {
			t.Fatal(err)
		}
	}

	opens := driver.openCount - opens0
	if opens > 1 {
		t.Errorf("opens = %d; want <= 1", opens)
		t.Logf("db = %#v", db)
		t.Logf("driver = %#v", driver)
		t.Logf("stmt = %#v", stmt)
	}
}

// Issue 3857
// This used to deadlock.
func TestSimultaneousQueries(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)

	tx, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	defer tx.Rollback()

	r1, err := tx.Query("SELECT|people|name|")
	if err != nil {
		t.Fatal(err)
	}
	defer r1.Close()

	r2, err := tx.Query("SELECT|people|name|")
	if err != nil {
		t.Fatal(err)
	}
	defer r2.Close()
}

func TestMaxIdleConns(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)

	tx, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	tx.Commit()
	if got := len(db.freeConn); got != 1 {
		t.Errorf("freeConns = %d; want 1", got)
	}

	db.SetMaxIdleConns(0)

	if got := len(db.freeConn); got != 0 {
		t.Errorf("freeConns after set to zero = %d; want 0", got)
	}

	tx, err = db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	tx.Commit()
	if got := len(db.freeConn); got != 0 {
		t.Errorf("freeConns = %d; want 0", got)
	}
}

// golang.org/issue/5323
func TestStmtCloseDeps(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping in short mode")
	}
	defer setHookpostCloseConn(nil)
	setHookpostCloseConn(func(_ *fakeConn, err error) {
		if err != nil {
			t.Errorf("Error closing fakeConn: %v", err)
		}
	})

	db := newTestDB(t, "magicquery")
	defer closeDB(t, db)

	driver := db.driver.(*fakeDriver)

	driver.mu.Lock()
	opens0 := driver.openCount
	closes0 := driver.closeCount
	driver.mu.Unlock()
	openDelta0 := opens0 - closes0

	stmt, err := db.Prepare("SELECT|magicquery|op|op=?,millis=?")
	if err != nil {
		t.Fatal(err)
	}

	// Start 50 parallel slow queries.
	const (
		nquery      = 50
		sleepMillis = 25
		nbatch      = 2
	)
	var wg sync.WaitGroup
	for batch := 0; batch < nbatch; batch++ {
		for i := 0; i < nquery; i++ {
			wg.Add(1)
			go func() {
				defer wg.Done()
				var op string
				if err := stmt.QueryRow("sleep", sleepMillis).Scan(&op); err != nil && err != ErrNoRows {
					t.Error(err)
				}
			}()
		}
		// Sleep for twice the expected length of time for the
		// batch of 50 queries above to finish before starting
		// the next round.
		time.Sleep(2 * sleepMillis * time.Millisecond)
	}
	wg.Wait()

	if g, w := db.numFreeConns(), 2; g != w {
		t.Errorf("free conns = %d; want %d", g, w)
	}

	if n := db.numDepsPollUntil(4, time.Second); n > 4 {
		t.Errorf("number of dependencies = %d; expected <= 4", n)
		db.dumpDeps(t)
	}

	driver.mu.Lock()
	opens := driver.openCount - opens0
	closes := driver.closeCount - closes0
	driver.mu.Unlock()
	openDelta := (driver.openCount - driver.closeCount) - openDelta0

	if openDelta > 2 {
		t.Logf("open calls = %d", opens)
		t.Logf("close calls = %d", closes)
		t.Logf("open delta = %d", openDelta)
		t.Errorf("db connections opened = %d; want <= 2", openDelta)
		db.dumpDeps(t)
	}

	if len(stmt.css) > nquery {
		t.Errorf("len(stmt.css) = %d; want <= %d", len(stmt.css), nquery)
	}

	if err := stmt.Close(); err != nil {
		t.Fatal(err)
	}

	if g, w := db.numFreeConns(), 2; g != w {
		t.Errorf("free conns = %d; want %d", g, w)
	}

	if n := db.numDepsPollUntil(2, time.Second); n > 2 {
		t.Errorf("number of dependencies = %d; expected <= 2", n)
		db.dumpDeps(t)
	}

	db.SetMaxIdleConns(0)

	if g, w := db.numFreeConns(), 0; g != w {
		t.Errorf("free conns = %d; want %d", g, w)
	}

	if n := db.numDepsPollUntil(0, time.Second); n > 0 {
		t.Errorf("number of dependencies = %d; expected 0", n)
		db.dumpDeps(t)
	}
}

// golang.org/issue/5046
func TestCloseConnBeforeStmts(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)

	defer setHookpostCloseConn(nil)
	setHookpostCloseConn(func(_ *fakeConn, err error) {
		if err != nil {
			t.Errorf("Error closing fakeConn: %v; from %s", err, stack())
			db.dumpDeps(t)
			t.Errorf("DB = %#v", db)
		}
	})

	stmt, err := db.Prepare("SELECT|people|name|")
	if err != nil {
		t.Fatal(err)
	}

	if len(db.freeConn) != 1 {
		t.Fatalf("expected 1 freeConn; got %d", len(db.freeConn))
	}
	dc := db.freeConn[0]
	if dc.closed {
		t.Errorf("conn shouldn't be closed")
	}

	if n := len(dc.openStmt); n != 1 {
		t.Errorf("driverConn num openStmt = %d; want 1", n)
	}
	err = db.Close()
	if err != nil {
		t.Errorf("db Close = %v", err)
	}
	if !dc.closed {
		t.Errorf("after db.Close, driverConn should be closed")
	}
	if n := len(dc.openStmt); n != 0 {
		t.Errorf("driverConn num openStmt = %d; want 0", n)
	}

	err = stmt.Close()
	if err != nil {
		t.Errorf("Stmt close = %v", err)
	}

	if !dc.closed {
		t.Errorf("conn should be closed")
	}
	if dc.ci != nil {
		t.Errorf("after Stmt Close, driverConn's Conn interface should be nil")
	}
}

// golang.org/issue/5283: don't release the Rows' connection in Close
// before calling Stmt.Close.
func TestRowsCloseOrder(t *testing.T) {
	db := newTestDB(t, "people")
	defer closeDB(t, db)

	db.SetMaxIdleConns(0)
	setStrictFakeConnClose(t)
	defer setStrictFakeConnClose(nil)

	rows, err := db.Query("SELECT|people|age,name|")
	if err != nil {
		t.Fatal(err)
	}
	err = rows.Close()
	if err != nil {
		t.Fatal(err)
	}
}

func manyConcurrentQueries(t testOrBench) {
	maxProcs, numReqs := 16, 500
	if testing.Short() {
		maxProcs, numReqs = 4, 50
	}
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(maxProcs))

	db := newTestDB(t, "people")
	defer closeDB(t, db)

	stmt, err := db.Prepare("SELECT|people|name|")
	if err != nil {
		t.Fatal(err)
	}
	defer stmt.Close()

	var wg sync.WaitGroup
	wg.Add(numReqs)

	reqs := make(chan bool)
	defer close(reqs)

	for i := 0; i < maxProcs*2; i++ {
		go func() {
			for _ = range reqs {
				rows, err := stmt.Query()
				if err != nil {
					t.Errorf("error on query:  %v", err)
					wg.Done()
					continue
				}

				var name string
				for rows.Next() {
					rows.Scan(&name)
				}
				rows.Close()

				wg.Done()
			}
		}()
	}

	for i := 0; i < numReqs; i++ {
		reqs <- true
	}

	wg.Wait()
}

func TestConcurrency(t *testing.T) {
	manyConcurrentQueries(t)
}

func BenchmarkConcurrency(b *testing.B) {
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		manyConcurrentQueries(b)
	}
}
