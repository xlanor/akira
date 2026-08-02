package pair

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"testing"
	"time"
)

func serveOnce(ln net.Listener, code string, out chan<- []byte) {
	conn, err := ln.Accept()
	if err != nil {
		out <- nil
		return
	}
	defer conn.Close()

	hello, switchPriv, err := GenerateHello()
	if err != nil {
		out <- nil
		return
	}
	if _, err := conn.Write(hello.Encode()); err != nil {
		out <- nil
		return
	}

	headerLen := 5 + pubKeyLen + ivLen + 4
	header := make([]byte, headerLen)
	if _, err := io.ReadFull(conn, header); err != nil {
		out <- nil
		return
	}
	ctLen := binary.BigEndian.Uint32(header[5+pubKeyLen+ivLen:])
	rest := make([]byte, int(ctLen)+macLen)
	if _, err := io.ReadFull(conn, rest); err != nil {
		out <- nil
		return
	}
	full := append(append([]byte{}, header...), rest...)

	sealed, err := DecodeSealed(full)
	if err != nil {
		conn.Write([]byte{0x02})
		out <- nil
		return
	}
	pt, err := Open(hello, switchPriv, sealed, code)
	if err != nil {
		conn.Write([]byte{0x00})
		out <- nil
		return
	}
	conn.Write([]byte{0x01})
	out <- pt
}

func listenerPort(t *testing.T, ln net.Listener) int {
	t.Helper()
	_, portStr, err := net.SplitHostPort(ln.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	var port int
	fmt.Sscanf(portStr, "%d", &port)
	return port
}

func TestPushEndToEnd(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	out := make(chan []byte, 1)
	go serveOnce(ln, "4729", out)

	payload := []byte(`{"duid":"0000000700410080deadbeefdeadbeefdeadbeefdeadbeef","access_token":"tok"}`)
	res, err := Push("127.0.0.1", listenerPort(t, ln), "4729", payload, 5*time.Second)
	if err != nil {
		t.Fatalf("push: %v", err)
	}
	if res != PushImported {
		t.Fatalf("want PushImported, got %v", res)
	}
	received := <-out
	if !bytes.Equal(received, payload) {
		t.Fatalf("switch received %q, want %q", received, payload)
	}
}

func TestPushWrongCode(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	out := make(chan []byte, 1)
	go serveOnce(ln, "4729", out)

	res, err := Push("127.0.0.1", listenerPort(t, ln), "0000", []byte(`{"duid":"x"}`), 5*time.Second)
	if err != nil {
		t.Fatalf("push: %v", err)
	}
	if res != PushBadCode {
		t.Fatalf("want PushBadCode, got %v", res)
	}
	<-out
}
