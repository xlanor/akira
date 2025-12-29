package nat

import (
	"fmt"
	"net"
	"time"

	"github.com/pion/stun/v3"
)

type NATType int

const (
	NATUnknown NATType = iota
	NATCone
	NATSymmetric
	NATBlocked
)

func (n NATType) String() string {
	switch n {
	case NATCone:
		return "Endpoint-Independent Mapping"
	case NATSymmetric:
		return "Address & Port-Dependent Mapping"
	case NATBlocked:
		return "UDP Blocked"
	default:
		return "Unknown"
	}
}

type DetectionResult struct {
	Type       NATType
	ExternalIP string
	Error      error
}

type mappedAddr struct {
	ip   string
	port int
}

func Detect() DetectionResult {
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{})
	if err != nil {
		return DetectionResult{Type: NATUnknown, Error: err}
	}
	defer conn.Close()

	conn.SetDeadline(time.Now().Add(5 * time.Second))

	addr1, err := queryServer(conn, "stun.l.google.com:19302")
	if err != nil {
		return DetectionResult{Type: NATBlocked, Error: fmt.Errorf("server 1: %w", err)}
	}

	addr2, err := queryServer(conn, "stun1.l.google.com:19302")
	if err != nil {
		return DetectionResult{
			Type:       NATCone,
			ExternalIP: addr1.ip,
		}
	}

	if addr1.port == addr2.port {
		return DetectionResult{
			Type:       NATCone,
			ExternalIP: addr1.ip,
		}
	}

	return DetectionResult{
		Type:       NATSymmetric,
		ExternalIP: addr1.ip,
	}
}

func queryServer(conn *net.UDPConn, server string) (*mappedAddr, error) {
	serverAddr, err := net.ResolveUDPAddr("udp4", server)
	if err != nil {
		return nil, err
	}

	msg, err := stun.Build(stun.TransactionID, stun.BindingRequest)
	if err != nil {
		return nil, err
	}

	_, err = conn.WriteToUDP(msg.Raw, serverAddr)
	if err != nil {
		return nil, err
	}

	buf := make([]byte, 1024)
	n, _, err := conn.ReadFromUDP(buf)
	if err != nil {
		return nil, err
	}

	resp := new(stun.Message)
	resp.Raw = buf[:n]
	if err := resp.Decode(); err != nil {
		return nil, err
	}

	var xorAddr stun.XORMappedAddress
	if err := xorAddr.GetFrom(resp); err != nil {
		return nil, err
	}

	return &mappedAddr{
		ip:   xorAddr.IP.String(),
		port: xorAddr.Port,
	}, nil
}
