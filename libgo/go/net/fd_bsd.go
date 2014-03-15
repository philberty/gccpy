// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build freebsd netbsd openbsd

// Waiting for FDs via kqueue/kevent.

package net

import (
	"os"
	"syscall"
)

type pollster struct {
	kq       int
	eventbuf [10]syscall.Kevent_t
	events   []syscall.Kevent_t

	// An event buffer for AddFD/DelFD.
	// Must hold pollServer lock.
	kbuf [1]syscall.Kevent_t
}

func newpollster() (p *pollster, err error) {
	p = new(pollster)
	if p.kq, err = syscall.Kqueue(); err != nil {
		return nil, os.NewSyscallError("kqueue", err)
	}
	syscall.CloseOnExec(p.kq)
	p.events = p.eventbuf[0:0]
	return p, nil
}

// First return value is whether the pollServer should be woken up.
// This version always returns false.
func (p *pollster) AddFD(fd int, mode int, repeat bool) (bool, error) {
	// pollServer is locked.

	var kmode int
	if mode == 'r' {
		kmode = syscall.EVFILT_READ
	} else {
		kmode = syscall.EVFILT_WRITE
	}
	ev := &p.kbuf[0]
	// EV_ADD - add event to kqueue list
	// EV_ONESHOT - delete the event the first time it triggers
	flags := syscall.EV_ADD
	if !repeat {
		flags |= syscall.EV_ONESHOT
	}
	syscall.SetKevent(ev, fd, kmode, flags)

	n, err := syscall.Kevent(p.kq, p.kbuf[:], nil, nil)
	if err != nil {
		return false, os.NewSyscallError("kevent", err)
	}
	if n != 1 || (ev.Flags&syscall.EV_ERROR) == 0 || int(ev.Ident) != fd || int(ev.Filter) != kmode {
		return false, os.NewSyscallError("kqueue phase error", err)
	}
	if ev.Data != 0 {
		return false, syscall.Errno(int(ev.Data))
	}
	return false, nil
}

// Return value is whether the pollServer should be woken up.
// This version always returns false.
func (p *pollster) DelFD(fd int, mode int) bool {
	// pollServer is locked.

	var kmode int
	if mode == 'r' {
		kmode = syscall.EVFILT_READ
	} else {
		kmode = syscall.EVFILT_WRITE
	}
	ev := &p.kbuf[0]
	// EV_DELETE - delete event from kqueue list
	syscall.SetKevent(ev, fd, kmode, syscall.EV_DELETE)
	syscall.Kevent(p.kq, p.kbuf[:], nil, nil)
	return false
}

func (p *pollster) WaitFD(s *pollServer, nsec int64) (fd int, mode int, err error) {
	var t *syscall.Timespec
	for len(p.events) == 0 {
		if nsec > 0 {
			if t == nil {
				t = new(syscall.Timespec)
			}
			*t = syscall.NsecToTimespec(nsec)
		}

		s.Unlock()
		n, err := syscall.Kevent(p.kq, nil, p.eventbuf[:], t)
		s.Lock()

		if err != nil {
			if err == syscall.EINTR {
				continue
			}
			return -1, 0, os.NewSyscallError("kevent", err)
		}
		if n == 0 {
			return -1, 0, nil
		}
		p.events = p.eventbuf[:n]
	}
	ev := &p.events[0]
	p.events = p.events[1:]
	fd = int(ev.Ident)
	if ev.Filter == syscall.EVFILT_READ {
		mode = 'r'
	} else {
		mode = 'w'
	}
	return fd, mode, nil
}

func (p *pollster) Close() error { return os.NewSyscallError("close", syscall.Close(p.kq)) }
