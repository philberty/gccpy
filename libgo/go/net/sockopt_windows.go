// Copyright 2011 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Socket options for Windows

package net

import (
	"os"
	"syscall"
	"time"
)

func setDefaultSockopts(s syscall.Handle, f, t int, ipv6only bool) error {
	switch f {
	case syscall.AF_INET6:
		if ipv6only {
			syscall.SetsockoptInt(s, syscall.IPPROTO_IPV6, syscall.IPV6_V6ONLY, 1)
		} else {
			// Allow both IP versions even if the OS default
			// is otherwise.  Note that some operating systems
			// never admit this option.
			syscall.SetsockoptInt(s, syscall.IPPROTO_IPV6, syscall.IPV6_V6ONLY, 0)
		}
	}
	// Allow broadcast.
	syscall.SetsockoptInt(s, syscall.SOL_SOCKET, syscall.SO_BROADCAST, 1)
	return nil
}

func setDefaultListenerSockopts(s syscall.Handle) error {
	// Windows will reuse recently-used addresses by default.
	// SO_REUSEADDR should not be used here, as it allows
	// a socket to forcibly bind to a port in use by another socket.
	// This could lead to a non-deterministic behavior, where
	// connection requests over the port cannot be guaranteed
	// to be handled by the correct socket.
	return nil
}

func setDefaultMulticastSockopts(s syscall.Handle) error {
	// Allow multicast UDP and raw IP datagram sockets to listen
	// concurrently across multiple listeners.
	err := syscall.SetsockoptInt(s, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
	if err != nil {
		return os.NewSyscallError("setsockopt", err)
	}
	return nil
}

// TODO(dfc) these unused error returns could be removed

func setReadDeadline(fd *netFD, t time.Time) error {
	fd.rdeadline.setTime(t)
	return nil
}

func setWriteDeadline(fd *netFD, t time.Time) error {
	fd.wdeadline.setTime(t)
	return nil
}

func setDeadline(fd *netFD, t time.Time) error {
	setReadDeadline(fd, t)
	setWriteDeadline(fd, t)
	return nil
}
