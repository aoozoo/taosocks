package main

import (
	"bufio"
	"crypto/tls"
	"encoding/gob"
	"errors"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"syscall"
	"time"

	"../internal"
)

// Relayer exposes the interfaces for
// relaying a connection
type Relayer interface {
	Begin(addr string, src net.Conn) bool
	Relay() *RelayResult
	ToRemote(b []byte) error
	ToLocal(b []byte) error
}

// RelayResult contains
type RelayResult struct {
	errTx error
	errRx error
	nTx   int64
	nRx   int64
}

// LocalRelayer is a relayer that relays all
// traffics through local network
type LocalRelayer struct {
	src  net.Conn
	dst  net.Conn
	addr string
}

func (r *LocalRelayer) Begin(addr string, src net.Conn) bool {
	r.src = src
	r.addr = addr

	dst, err := net.DialTimeout("tcp", addr, 10*time.Second)
	if err != nil {
		tslog.Red("? Dial host:%s -  %s\n", addr, err)
		return false
	}

	r.dst = dst
	return true
}

func (r *LocalRelayer) ToLocal(b []byte) error {
	r.src.Write(b)
	return nil
}

func (r *LocalRelayer) ToRemote(b []byte) error {
	r.dst.Write(b)
	return nil
}

func (r *LocalRelayer) Relay() *RelayResult {
	tslog.Log("> [Direct] %s", r.addr)

	wg := &sync.WaitGroup{}
	wg.Add(2)

	var tx int64
	var rx int64

	var errTx, errRx error

	go func() {
		tx, errTx = io.Copy(r.dst, r.src)
		wg.Done()
		if errTx != nil {
			r.src.Close()
			r.dst.Close()
		}
	}()

	go func() {
		rx, errRx = io.Copy(r.src, r.dst)
		wg.Done()
		if errRx != nil {
			r.src.Close()
			r.dst.Close()
		}
	}()

	wg.Wait()

	r.src.Close()
	r.dst.Close()

	tslog.Gray("< [Direct] %s [TX:%d, RX:%d]", r.addr, tx, rx)

	return &RelayResult{
		errTx: errTx,
		errRx: errRx,
		nTx:   tx,
		nRx:   rx,
	}
}

const kVersion string = "taosocks/20171218"

// RemoteRelayer is a relayer that relays all
// traffics through remote servers by using
// taosocks protocol
type RemoteRelayer struct {
	src  net.Conn
	dst  net.Conn
	addr string
}

func (r *RemoteRelayer) dialServer() (net.Conn, error) {
	tlscfg := &tls.Config{
		InsecureSkipVerify: config.Insecure,
	}

	conn, err := tls.Dial("tcp4", config.Server, tlscfg)
	if err != nil {
		return nil, err
	}

	// the upgrade request
	req, _ := http.NewRequest("GET", "/", nil)
	req.Host = config.Server
	req.Header.Add("Connection", "upgrade")
	req.Header.Add("Upgrade", kVersion)
	req.Header.Add("Username", config.Username)
	req.Header.Add("Password", config.Password)

	err = req.Write(conn)
	if err != nil {
		conn.Close()
		return nil, err
	}

	bio := bufio.NewReader(conn)

	resp, err := http.ReadResponse(bio, nil)
	if err != nil {
		conn.Close()
		return nil, err
	}

	resp.Body.Close()

	if resp.StatusCode != 101 {
		conn.Close()
		return nil, errors.New("server upgrade protocol error")
	}

	return conn, nil
}

func (r *RemoteRelayer) Begin(addr string, src net.Conn) bool {
	r.src = src
	r.addr = addr

	dst, err := r.dialServer()
	if err != nil {
		tslog.Red("%s", err)
		return false
	}

	r.dst = dst

	enc := gob.NewEncoder(r.dst)
	dec := gob.NewDecoder(r.dst)

	err = enc.Encode(internal.OpenPacket{Addr: r.addr})
	if err != nil {
		return false
	}

	var oapkt internal.OpenAckPacket
	err = dec.Decode(&oapkt)
	if err != nil {
		return false
	}

	if !oapkt.Status {
		return false
	}

	return true
}

func (r *RemoteRelayer) ToLocal(b []byte) error {
	r.src.Write(b)

	return nil
}

func (r *RemoteRelayer) ToRemote(b []byte) error {
	var pkt internal.RelayPacket
	pkt.Data = b

	enc := gob.NewEncoder(r.dst)
	enc.Encode(pkt)

	return nil
}

func (r *RemoteRelayer) Relay() *RelayResult {
	tslog.Log("> [Proxy ] %s", r.addr)

	wg := &sync.WaitGroup{}
	wg.Add(2)

	var tx int64
	var rx int64

	var errTx, errRx error

	go func() {
		tx, errTx = r.src2dst()
		wg.Done()
		if errTx != nil {
			r.src.Close()
			r.dst.Close()
		}
	}()

	go func() {
		rx, errRx = r.dst2src()
		wg.Done()
		if errRx != nil {
			r.src.Close()
			r.dst.Close()
		}
	}()

	wg.Wait()

	r.src.Close()
	r.dst.Close()

	tslog.Gray("< [Proxy ] %s [TX:%d, RX:%d]", r.addr, tx, rx)

	return &RelayResult{
		errTx: errTx,
		errRx: errRx,
		nTx:   tx,
		nRx:   rx,
	}
}

func (r *RemoteRelayer) src2dst() (int64, error) {
	enc := gob.NewEncoder(r.dst)

	buf := make([]byte, 4096)

	var all int64
	var err error

	for {
		n, err := r.src.Read(buf)
		if err != nil {
			break
		}

		var pkt internal.RelayPacket
		pkt.Data = buf[:n]

		err = enc.Encode(pkt)
		if err != nil {
			// log.Printf("server reset: %s\n", err)
			break
		}

		all += int64(n)
	}

	return all, err
}

func (r *RemoteRelayer) dst2src() (int64, error) {
	dec := gob.NewDecoder(r.dst)

	var all int64
	var err error

	for {
		var pkt internal.RelayPacket
		err = dec.Decode(&pkt)
		if err != nil {
			// log.Printf("server reset: %s\n", err)
			break
		}

		_, err = r.src.Write(pkt.Data)
		if err != nil {
			break
		}

		all += int64(len(pkt.Data))
	}

	return all, err
}

type SmartRelayer struct {
}

func (o *SmartRelayer) Relay(host string, conn net.Conn, beforeRelay func(r Relayer) error) error {
	hostname, portstr, _ := net.SplitHostPort(host)
	proxyType := filter.Test(hostname)

	var r Relayer

	switch proxyType {
	case proxyTypeDefault, proxyTypeDirect:
		r = &LocalRelayer{}
	case proxyTypeProxy, proxyTypeAuto:
		r = &RemoteRelayer{}
	case proxyTypeReject:
		return errors.New("host is rejected")
	}

	began := false
	useRemote := false

	if !r.Begin(host, conn) {
		switch r.(type) {
		case *LocalRelayer:
			r = &RemoteRelayer{}
			if r.Begin(host, conn) {
				began = true
				useRemote = true
			}
		}
	} else {
		began = true
	}

	if !began {
		conn.Close()
		return errors.New("no relayer can relay such host")
	}

	if useRemote {
		filter.Add(hostname)
	}

	if beforeRelay != nil {
		if beforeRelay(r) != nil {
			conn.Close()
			return errors.New("beforeRelay returns an error")
		}
	}

	rr := r.Relay()

	if rr.nTx != 0 && rr.nRx == 0 {
		isHTTPPort := portstr == "80" || portstr == "443"
		isReset := false

		if opErr, ok := rr.errRx.(*net.OpError); ok {
			// TODO: 只能比较字符串？
			isReset = strings.Contains(opErr.Err.Error(), syscall.ECONNRESET.Error())
		}

		if isHTTPPort || isReset {
			switch r.(type) {
			case *LocalRelayer:
				filter.Add(hostname)
			case *RemoteRelayer:
				filter.Del(hostname)
			}
		}
	}

	return nil
}
