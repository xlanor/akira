package pair

import (
	"errors"
	"fmt"
	"io"
	"net"
	"time"
)

type PushResult int

const (
	PushImported PushResult = iota
	PushBadCode
	PushError
)

var ErrNoAck = errors.New("pair: no acknowledgement from Switch")

func Push(host string, port int, code string, payload []byte, timeout time.Duration) (PushResult, error) {
	addr := net.JoinHostPort(host, fmt.Sprintf("%d", port))
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return PushError, err
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(timeout))

	helloBytes := make([]byte, helloLen)
	if _, err := io.ReadFull(conn, helloBytes); err != nil {
		return PushError, fmt.Errorf("read hello: %w", err)
	}
	hello, err := DecodeHello(helloBytes)
	if err != nil {
		return PushError, fmt.Errorf("decode hello: %w", err)
	}

	sealed, err := Seal(hello, code, payload)
	if err != nil {
		return PushError, fmt.Errorf("seal: %w", err)
	}
	if _, err := conn.Write(sealed.Encode()); err != nil {
		return PushError, fmt.Errorf("write sealed: %w", err)
	}

	ack := make([]byte, 1)
	if _, err := io.ReadFull(conn, ack); err != nil {
		return PushError, ErrNoAck
	}
	switch ack[0] {
	case 0x01:
		return PushImported, nil
	case 0x00:
		return PushBadCode, nil
	default:
		return PushError, nil
	}
}
