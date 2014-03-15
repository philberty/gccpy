// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package testing provides support for automated testing of Go packages.
// It is intended to be used in concert with the ``go test'' command, which automates
// execution of any function of the form
//     func TestXxx(*testing.T)
// where Xxx can be any alphanumeric string (but the first letter must not be in
// [a-z]) and serves to identify the test routine.
// These TestXxx routines should be declared within the package they are testing.
//
// Tests and benchmarks may be skipped if not applicable like this:
//     func TestTimeConsuming(t *testing.T) {
//         if testing.Short() {
//             t.Skip("skipping test in short mode.")
//         }
//         ...
//     }
//
// Benchmarks
//
// Functions of the form
//     func BenchmarkXxx(*testing.B)
// are considered benchmarks, and are executed by the "go test" command when
// the -test.bench flag is provided. Benchmarks are run sequentially.
//
// For a description of the testing flags, see
// http://golang.org/cmd/go/#Description_of_testing_flags.
//
// A sample benchmark function looks like this:
//     func BenchmarkHello(b *testing.B) {
//         for i := 0; i < b.N; i++ {
//             fmt.Sprintf("hello")
//         }
//     }
//
// The benchmark function must run the target code b.N times.
// The benchmark package will vary b.N until the benchmark function lasts
// long enough to be timed reliably.  The output
//     BenchmarkHello    10000000    282 ns/op
// means that the loop ran 10000000 times at a speed of 282 ns per loop.
//
// If a benchmark needs some expensive setup before running, the timer
// may be reset:
//     func BenchmarkBigLen(b *testing.B) {
//         big := NewBig()
//         b.ResetTimer()
//         for i := 0; i < b.N; i++ {
//             big.Len()
//         }
//     }
//
// Examples
//
// The package also runs and verifies example code. Example functions may
// include a concluding line comment that begins with "Output:" and is compared with
// the standard output of the function when the tests are run. (The comparison
// ignores leading and trailing space.) These are examples of an example:
//
//     func ExampleHello() {
//             fmt.Println("hello")
//             // Output: hello
//     }
//
//     func ExampleSalutations() {
//             fmt.Println("hello, and")
//             fmt.Println("goodbye")
//             // Output:
//             // hello, and
//             // goodbye
//     }
//
// Example functions without output comments are compiled but not executed.
//
// The naming convention to declare examples for a function F, a type T and
// method M on type T are:
//
//     func ExampleF() { ... }
//     func ExampleT() { ... }
//     func ExampleT_M() { ... }
//
// Multiple example functions for a type/function/method may be provided by
// appending a distinct suffix to the name. The suffix must start with a
// lower-case letter.
//
//     func ExampleF_suffix() { ... }
//     func ExampleT_suffix() { ... }
//     func ExampleT_M_suffix() { ... }
//
// The entire test file is presented as the example when it contains a single
// example function, at least one other function, type, variable, or constant
// declaration, and no test or benchmark functions.
package testing

import (
	"bytes"
	"flag"
	"fmt"
	"os"
	"runtime"
	"runtime/pprof"
	"strconv"
	"strings"
	"sync"
	"time"
)

var (
	// The short flag requests that tests run more quickly, but its functionality
	// is provided by test writers themselves.  The testing package is just its
	// home.  The all.bash installation script sets it to make installation more
	// efficient, but by default the flag is off so a plain "go test" will do a
	// full test of the package.
	short = flag.Bool("test.short", false, "run smaller test suite to save time")

	// Report as tests are run; default is silent for success.
	chatty           = flag.Bool("test.v", false, "verbose: print additional output")
	match            = flag.String("test.run", "", "regular expression to select tests and examples to run")
	memProfile       = flag.String("test.memprofile", "", "write a memory profile to the named file after execution")
	memProfileRate   = flag.Int("test.memprofilerate", 0, "if >=0, sets runtime.MemProfileRate")
	cpuProfile       = flag.String("test.cpuprofile", "", "write a cpu profile to the named file during execution")
	blockProfile     = flag.String("test.blockprofile", "", "write a goroutine blocking profile to the named file after execution")
	blockProfileRate = flag.Int("test.blockprofilerate", 1, "if >= 0, calls runtime.SetBlockProfileRate()")
	timeout          = flag.Duration("test.timeout", 0, "if positive, sets an aggregate time limit for all tests")
	cpuListStr       = flag.String("test.cpu", "", "comma-separated list of number of CPUs to use for each test")
	parallel         = flag.Int("test.parallel", runtime.GOMAXPROCS(0), "maximum test parallelism")

	haveExamples bool // are there examples?

	cpuList []int
)

// common holds the elements common between T and B and
// captures common methods such as Errorf.
type common struct {
	mu      sync.RWMutex // guards output and failed
	output  []byte       // Output generated by test or benchmark.
	failed  bool         // Test or benchmark has failed.
	skipped bool         // Test of benchmark has been skipped.

	start    time.Time // Time test or benchmark started
	duration time.Duration
	self     interface{}      // To be sent on signal channel when done.
	signal   chan interface{} // Output for serial tests.
}

// Short reports whether the -test.short flag is set.
func Short() bool {
	return *short
}

// Verbose reports whether the -test.v flag is set.
func Verbose() bool {
	return *chatty
}

// decorate prefixes the string with the file and line of the call site
// and inserts the final newline if needed and indentation tabs for formatting.
func decorate(s string) string {
	_, file, line, ok := runtime.Caller(3) // decorate + log + public function.
	if ok {
		// Truncate file name at last file name separator.
		if index := strings.LastIndex(file, "/"); index >= 0 {
			file = file[index+1:]
		} else if index = strings.LastIndex(file, "\\"); index >= 0 {
			file = file[index+1:]
		}
	} else {
		file = "???"
		line = 1
	}
	buf := new(bytes.Buffer)
	// Every line is indented at least one tab.
	buf.WriteByte('\t')
	fmt.Fprintf(buf, "%s:%d: ", file, line)
	lines := strings.Split(s, "\n")
	if l := len(lines); l > 1 && lines[l-1] == "" {
		lines = lines[:l-1]
	}
	for i, line := range lines {
		if i > 0 {
			// Second and subsequent lines are indented an extra tab.
			buf.WriteString("\n\t\t")
		}
		buf.WriteString(line)
	}
	buf.WriteByte('\n')
	return buf.String()
}

// T is a type passed to Test functions to manage test state and support formatted test logs.
// Logs are accumulated during execution and dumped to standard error when done.
type T struct {
	common
	name          string    // Name of test.
	startParallel chan bool // Parallel tests will wait on this.
}

// Fail marks the function as having failed but continues execution.
func (c *common) Fail() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.failed = true
}

// Failed reports whether the function has failed.
func (c *common) Failed() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.failed
}

// FailNow marks the function as having failed and stops its execution.
// Execution will continue at the next test or benchmark.
// FailNow must be called from the goroutine running the
// test or benchmark function, not from other goroutines
// created during the test. Calling FailNow does not stop
// those other goroutines.
func (c *common) FailNow() {
	c.Fail()

	// Calling runtime.Goexit will exit the goroutine, which
	// will run the deferred functions in this goroutine,
	// which will eventually run the deferred lines in tRunner,
	// which will signal to the test loop that this test is done.
	//
	// A previous version of this code said:
	//
	//	c.duration = ...
	//	c.signal <- c.self
	//	runtime.Goexit()
	//
	// This previous version duplicated code (those lines are in
	// tRunner no matter what), but worse the goroutine teardown
	// implicit in runtime.Goexit was not guaranteed to complete
	// before the test exited.  If a test deferred an important cleanup
	// function (like removing temporary files), there was no guarantee
	// it would run on a test failure.  Because we send on c.signal during
	// a top-of-stack deferred function now, we know that the send
	// only happens after any other stacked defers have completed.
	runtime.Goexit()
}

// log generates the output. It's always at the same stack depth.
func (c *common) log(s string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.output = append(c.output, decorate(s)...)
}

// Log formats its arguments using default formatting, analogous to Println,
// and records the text in the error log. The text will be printed only if
// the test fails or the -test.v flag is set.
func (c *common) Log(args ...interface{}) { c.log(fmt.Sprintln(args...)) }

// Logf formats its arguments according to the format, analogous to Printf,
// and records the text in the error log. The text will be printed only if
// the test fails or the -test.v flag is set.
func (c *common) Logf(format string, args ...interface{}) { c.log(fmt.Sprintf(format, args...)) }

// Error is equivalent to Log followed by Fail.
func (c *common) Error(args ...interface{}) {
	c.log(fmt.Sprintln(args...))
	c.Fail()
}

// Errorf is equivalent to Logf followed by Fail.
func (c *common) Errorf(format string, args ...interface{}) {
	c.log(fmt.Sprintf(format, args...))
	c.Fail()
}

// Fatal is equivalent to Log followed by FailNow.
func (c *common) Fatal(args ...interface{}) {
	c.log(fmt.Sprintln(args...))
	c.FailNow()
}

// Fatalf is equivalent to Logf followed by FailNow.
func (c *common) Fatalf(format string, args ...interface{}) {
	c.log(fmt.Sprintf(format, args...))
	c.FailNow()
}

// Skip is equivalent to Log followed by SkipNow.
func (c *common) Skip(args ...interface{}) {
	c.log(fmt.Sprintln(args...))
	c.SkipNow()
}

// Skipf is equivalent to Logf followed by SkipNow.
func (c *common) Skipf(format string, args ...interface{}) {
	c.log(fmt.Sprintf(format, args...))
	c.SkipNow()
}

// SkipNow marks the test as having been skipped and stops its execution.
// Execution will continue at the next test or benchmark. See also FailNow.
// SkipNow must be called from the goroutine running the test, not from
// other goroutines created during the test. Calling SkipNow does not stop
// those other goroutines.
func (c *common) SkipNow() {
	c.skip()
	runtime.Goexit()
}

func (c *common) skip() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.skipped = true
}

// Skipped reports whether the test was skipped.
func (c *common) Skipped() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.skipped
}

// Parallel signals that this test is to be run in parallel with (and only with)
// other parallel tests.
func (t *T) Parallel() {
	t.signal <- (*T)(nil) // Release main testing loop
	<-t.startParallel     // Wait for serial tests to finish
}

// An internal type but exported because it is cross-package; part of the implementation
// of the "go test" command.
type InternalTest struct {
	Name string
	F    func(*T)
}

func tRunner(t *T, test *InternalTest) {
	t.start = time.Now()

	// When this goroutine is done, either because test.F(t)
	// returned normally or because a test failure triggered
	// a call to runtime.Goexit, record the duration and send
	// a signal saying that the test is done.
	defer func() {
		t.duration = time.Now().Sub(t.start)
		// If the test panicked, print any test output before dying.
		if err := recover(); err != nil {
			t.Fail()
			t.report()
			panic(err)
		}
		t.signal <- t
	}()

	test.F(t)
}

// An internal function but exported because it is cross-package; part of the implementation
// of the "go test" command.
func Main(matchString func(pat, str string) (bool, error), tests []InternalTest, benchmarks []InternalBenchmark, examples []InternalExample) {
	flag.Parse()
	parseCpuList()

	before()
	startAlarm()
	haveExamples = len(examples) > 0
	testOk := RunTests(matchString, tests)
	exampleOk := RunExamples(matchString, examples)
	if !testOk || !exampleOk {
		fmt.Println("FAIL")
		os.Exit(1)
	}
	fmt.Println("PASS")
	stopAlarm()
	RunBenchmarks(matchString, benchmarks)
	after()
}

func (t *T) report() {
	tstr := fmt.Sprintf("(%.2f seconds)", t.duration.Seconds())
	format := "--- %s: %s %s\n%s"
	if t.Failed() {
		fmt.Printf(format, "FAIL", t.name, tstr, t.output)
	} else if *chatty {
		if t.Skipped() {
			fmt.Printf(format, "SKIP", t.name, tstr, t.output)
		} else {
			fmt.Printf(format, "PASS", t.name, tstr, t.output)
		}
	}
}

func RunTests(matchString func(pat, str string) (bool, error), tests []InternalTest) (ok bool) {
	ok = true
	if len(tests) == 0 && !haveExamples {
		fmt.Fprintln(os.Stderr, "testing: warning: no tests to run")
		return
	}
	for _, procs := range cpuList {
		runtime.GOMAXPROCS(procs)
		// We build a new channel tree for each run of the loop.
		// collector merges in one channel all the upstream signals from parallel tests.
		// If all tests pump to the same channel, a bug can occur where a test
		// kicks off a goroutine that Fails, yet the test still delivers a completion signal,
		// which skews the counting.
		var collector = make(chan interface{})

		numParallel := 0
		startParallel := make(chan bool)

		for i := 0; i < len(tests); i++ {
			matched, err := matchString(*match, tests[i].Name)
			if err != nil {
				fmt.Fprintf(os.Stderr, "testing: invalid regexp for -test.run: %s\n", err)
				os.Exit(1)
			}
			if !matched {
				continue
			}
			testName := tests[i].Name
			if procs != 1 {
				testName = fmt.Sprintf("%s-%d", tests[i].Name, procs)
			}
			t := &T{
				common: common{
					signal: make(chan interface{}),
				},
				name:          testName,
				startParallel: startParallel,
			}
			t.self = t
			if *chatty {
				fmt.Printf("=== RUN %s\n", t.name)
			}
			go tRunner(t, &tests[i])
			out := (<-t.signal).(*T)
			if out == nil { // Parallel run.
				go func() {
					collector <- <-t.signal
				}()
				numParallel++
				continue
			}
			t.report()
			ok = ok && !out.Failed()
		}

		running := 0
		for numParallel+running > 0 {
			if running < *parallel && numParallel > 0 {
				startParallel <- true
				running++
				numParallel--
				continue
			}
			t := (<-collector).(*T)
			t.report()
			ok = ok && !t.Failed()
			running--
		}
	}
	return
}

// before runs before all testing.
func before() {
	if *memProfileRate > 0 {
		runtime.MemProfileRate = *memProfileRate
	}
	if *cpuProfile != "" {
		f, err := os.Create(*cpuProfile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "testing: %s", err)
			return
		}
		if err := pprof.StartCPUProfile(f); err != nil {
			fmt.Fprintf(os.Stderr, "testing: can't start cpu profile: %s", err)
			f.Close()
			return
		}
		// Could save f so after can call f.Close; not worth the effort.
	}
	if *blockProfile != "" && *blockProfileRate >= 0 {
		runtime.SetBlockProfileRate(*blockProfileRate)
	}
}

// after runs after all testing.
func after() {
	if *cpuProfile != "" {
		pprof.StopCPUProfile() // flushes profile to disk
	}
	if *memProfile != "" {
		f, err := os.Create(*memProfile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "testing: %s", err)
			return
		}
		if err = pprof.WriteHeapProfile(f); err != nil {
			fmt.Fprintf(os.Stderr, "testing: can't write %s: %s", *memProfile, err)
		}
		f.Close()
	}
	if *blockProfile != "" && *blockProfileRate >= 0 {
		f, err := os.Create(*blockProfile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "testing: %s", err)
			return
		}
		if err = pprof.Lookup("block").WriteTo(f, 0); err != nil {
			fmt.Fprintf(os.Stderr, "testing: can't write %s: %s", *blockProfile, err)
		}
		f.Close()
	}
}

var timer *time.Timer

// startAlarm starts an alarm if requested.
func startAlarm() {
	if *timeout > 0 {
		timer = time.AfterFunc(*timeout, alarm)
	}
}

// stopAlarm turns off the alarm.
func stopAlarm() {
	if *timeout > 0 {
		timer.Stop()
	}
}

// alarm is called if the timeout expires.
func alarm() {
	panic("test timed out")
}

func parseCpuList() {
	if len(*cpuListStr) == 0 {
		cpuList = append(cpuList, runtime.GOMAXPROCS(-1))
	} else {
		for _, val := range strings.Split(*cpuListStr, ",") {
			cpu, err := strconv.Atoi(val)
			if err != nil || cpu <= 0 {
				fmt.Fprintf(os.Stderr, "testing: invalid value %q for -test.cpu", val)
				os.Exit(1)
			}
			cpuList = append(cpuList, cpu)
		}
	}
}
