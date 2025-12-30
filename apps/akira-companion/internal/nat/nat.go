package nat

import (
	"bufio"
	"fmt"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/pion/stun/v3"
)

type MappingType int

const (
	MappingUnknown MappingType = iota
	MappingEndpointIndependent
	MappingAddressDependent
	MappingAddressPortDependent
)

func (m MappingType) String() string {
	switch m {
	case MappingEndpointIndependent:
		return "Endpoint-Independent"
	case MappingAddressDependent:
		return "Address-Dependent"
	case MappingAddressPortDependent:
		return "Address and Port-Dependent"
	default:
		return "Unknown"
	}
}

type FilteringType int

const (
	FilteringUnknown FilteringType = iota
	FilteringEndpointIndependent
	FilteringAddressDependent
	FilteringAddressPortDependent
)

func (f FilteringType) String() string {
	switch f {
	case FilteringEndpointIndependent:
		return "Endpoint-Independent"
	case FilteringAddressDependent:
		return "Address-Dependent"
	case FilteringAddressPortDependent:
		return "Address and Port-Dependent"
	default:
		return "Unknown"
	}
}

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
	Mapping    MappingType
	Filtering  FilteringType
	ExternalIP string
	Error      error
}

type mappedAddr struct {
	ip   string
	port int
}

const rfc5780ServerListURL = "https://raw.githubusercontent.com/pradt2/always-online-stun/master/valid_nat_testing_hosts.txt"

func fetchRFC5780Servers() ([]string, error) {
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(rfc5780ServerListURL)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	var servers []string
	scanner := bufio.NewScanner(resp.Body)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" && !strings.HasPrefix(line, "#") {
			servers = append(servers, line)
		}
	}

	if len(servers) == 0 {
		return nil, fmt.Errorf("no RFC 5780 servers found")
	}
	return servers, nil
}

func Detect() DetectionResult {
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{})
	if err != nil {
		return DetectionResult{Type: NATUnknown, Mapping: MappingUnknown, Filtering: FilteringUnknown, Error: err}
	}
	defer conn.Close()

	conn.SetDeadline(time.Now().Add(5 * time.Second))

	addr1, err := queryServer(conn, "stun.l.google.com:19302")
	if err != nil {
		return DetectionResult{Type: NATBlocked, Mapping: MappingUnknown, Filtering: FilteringUnknown, Error: fmt.Errorf("server 1: %w", err)}
	}

	addr2, err := queryServer(conn, "stun1.l.google.com:19302")
	if err != nil {
		return DetectionResult{
			Type:       NATCone,
			Mapping:    MappingEndpointIndependent,
			Filtering:  FilteringUnknown,
			ExternalIP: addr1.ip,
		}
	}

	var natType NATType
	var mapping MappingType
	if addr1.port == addr2.port {
		natType = NATCone
		mapping = MappingEndpointIndependent
	} else {
		natType = NATSymmetric
		mapping = MappingAddressPortDependent
	}

	filtering := detectFiltering()

	return DetectionResult{
		Type:       natType,
		Mapping:    mapping,
		Filtering:  filtering,
		ExternalIP: addr1.ip,
	}
}

func detectFiltering() FilteringType {
	servers, err := fetchRFC5780Servers()
	if err != nil {
		return FilteringUnknown
	}

	for _, server := range servers {
		filtering, err := testFilteringWithServer(server)
		if err == nil {
			return filtering
		}
	}

	return FilteringUnknown
}

func testFilteringWithServer(server string) (FilteringType, error) {
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{})
	if err != nil {
		return FilteringUnknown, err
	}
	defer conn.Close()

	conn.SetDeadline(time.Now().Add(3 * time.Second))

	serverAddr, err := net.ResolveUDPAddr("udp4", server)
	if err != nil {
		return FilteringUnknown, err
	}

	msg, err := stun.Build(stun.TransactionID, stun.BindingRequest)
	if err != nil {
		return FilteringUnknown, err
	}

	_, err = conn.WriteToUDP(msg.Raw, serverAddr)
	if err != nil {
		return FilteringUnknown, err
	}

	buf := make([]byte, 1024)
	n, _, err := conn.ReadFromUDP(buf)
	if err != nil {
		return FilteringUnknown, err
	}

	resp := new(stun.Message)
	resp.Raw = buf[:n]
	if err := resp.Decode(); err != nil {
		return FilteringUnknown, err
	}

	var otherAddr stun.OtherAddress
	if err := otherAddr.GetFrom(resp); err != nil {
		return FilteringUnknown, fmt.Errorf("server does not support RFC 5780: %w", err)
	}

	changeRequest := stun.RawAttribute{
		Type:  stun.AttrChangeRequest,
		Value: []byte{0x00, 0x00, 0x00, 0x04},
	}

	msg2, err := stun.Build(stun.TransactionID, stun.BindingRequest, changeRequest)
	if err != nil {
		return FilteringUnknown, err
	}

	_, err = conn.WriteToUDP(msg2.Raw, serverAddr)
	if err != nil {
		return FilteringUnknown, err
	}

	conn.SetDeadline(time.Now().Add(2 * time.Second))
	n, fromAddr, err := conn.ReadFromUDP(buf)
	if err != nil {
		return FilteringAddressDependent, nil
	}

	resp2 := new(stun.Message)
	resp2.Raw = buf[:n]
	if err := resp2.Decode(); err != nil {
		return FilteringAddressDependent, nil
	}

	if fromAddr.IP.String() != serverAddr.IP.String() {
		return FilteringEndpointIndependent, nil
	}

	return FilteringAddressDependent, nil
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
