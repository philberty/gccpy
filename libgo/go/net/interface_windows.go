// Copyright 2011 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package net

import (
	"os"
	"syscall"
	"unsafe"
)

func bytePtrToString(p *uint8) string {
	a := (*[10000]uint8)(unsafe.Pointer(p))
	i := 0
	for a[i] != 0 {
		i++
	}
	return string(a[:i])
}

func getAdapterList() (*syscall.IpAdapterInfo, error) {
	b := make([]byte, 1000)
	l := uint32(len(b))
	a := (*syscall.IpAdapterInfo)(unsafe.Pointer(&b[0]))
	// TODO(mikio): GetAdaptersInfo returns IP_ADAPTER_INFO that
	// contains IPv4 address list only. We should use another API
	// for fetching IPv6 stuff from the kernel.
	err := syscall.GetAdaptersInfo(a, &l)
	if err == syscall.ERROR_BUFFER_OVERFLOW {
		b = make([]byte, l)
		a = (*syscall.IpAdapterInfo)(unsafe.Pointer(&b[0]))
		err = syscall.GetAdaptersInfo(a, &l)
	}
	if err != nil {
		return nil, os.NewSyscallError("GetAdaptersInfo", err)
	}
	return a, nil
}

func getInterfaceList() ([]syscall.InterfaceInfo, error) {
	s, err := sysSocket(syscall.AF_INET, syscall.SOCK_DGRAM, syscall.IPPROTO_UDP)
	if err != nil {
		return nil, os.NewSyscallError("Socket", err)
	}
	defer syscall.Closesocket(s)

	ii := [20]syscall.InterfaceInfo{}
	ret := uint32(0)
	size := uint32(unsafe.Sizeof(ii))
	err = syscall.WSAIoctl(s, syscall.SIO_GET_INTERFACE_LIST, nil, 0, (*byte)(unsafe.Pointer(&ii[0])), size, &ret, nil, 0)
	if err != nil {
		return nil, os.NewSyscallError("WSAIoctl", err)
	}
	c := ret / uint32(unsafe.Sizeof(ii[0]))
	return ii[:c-1], nil
}

// If the ifindex is zero, interfaceTable returns mappings of all
// network interfaces.  Otherwise it returns a mapping of a specific
// interface.
func interfaceTable(ifindex int) ([]Interface, error) {
	ai, err := getAdapterList()
	if err != nil {
		return nil, err
	}

	ii, err := getInterfaceList()
	if err != nil {
		return nil, err
	}

	var ift []Interface
	for ; ai != nil; ai = ai.Next {
		index := ai.Index
		if ifindex == 0 || ifindex == int(index) {
			var flags Flags

			row := syscall.MibIfRow{Index: index}
			e := syscall.GetIfEntry(&row)
			if e != nil {
				return nil, os.NewSyscallError("GetIfEntry", e)
			}

			for _, ii := range ii {
				ip := (*syscall.RawSockaddrInet4)(unsafe.Pointer(&ii.Address)).Addr
				ipv4 := IPv4(ip[0], ip[1], ip[2], ip[3])
				ipl := &ai.IpAddressList
				for ipl != nil {
					ips := bytePtrToString(&ipl.IpAddress.String[0])
					if ipv4.Equal(parseIPv4(ips)) {
						break
					}
					ipl = ipl.Next
				}
				if ipl == nil {
					continue
				}
				if ii.Flags&syscall.IFF_UP != 0 {
					flags |= FlagUp
				}
				if ii.Flags&syscall.IFF_LOOPBACK != 0 {
					flags |= FlagLoopback
				}
				if ii.Flags&syscall.IFF_BROADCAST != 0 {
					flags |= FlagBroadcast
				}
				if ii.Flags&syscall.IFF_POINTTOPOINT != 0 {
					flags |= FlagPointToPoint
				}
				if ii.Flags&syscall.IFF_MULTICAST != 0 {
					flags |= FlagMulticast
				}
			}

			name := bytePtrToString(&ai.AdapterName[0])

			ifi := Interface{
				Index:        int(index),
				MTU:          int(row.Mtu),
				Name:         name,
				HardwareAddr: HardwareAddr(row.PhysAddr[:row.PhysAddrLen]),
				Flags:        flags}
			ift = append(ift, ifi)
		}
	}
	return ift, nil
}

// If the ifi is nil, interfaceAddrTable returns addresses for all
// network interfaces.  Otherwise it returns addresses for a specific
// interface.
func interfaceAddrTable(ifi *Interface) ([]Addr, error) {
	ai, err := getAdapterList()
	if err != nil {
		return nil, err
	}

	var ifat []Addr
	for ; ai != nil; ai = ai.Next {
		index := ai.Index
		if ifi == nil || ifi.Index == int(index) {
			ipl := &ai.IpAddressList
			for ; ipl != nil; ipl = ipl.Next {
				ifa := IPAddr{IP: parseIPv4(bytePtrToString(&ipl.IpAddress.String[0]))}
				ifat = append(ifat, ifa.toAddr())
			}
		}
	}
	return ifat, nil
}

// interfaceMulticastAddrTable returns addresses for a specific
// interface.
func interfaceMulticastAddrTable(ifi *Interface) ([]Addr, error) {
	// TODO(mikio): Implement this like other platforms.
	return nil, nil
}
