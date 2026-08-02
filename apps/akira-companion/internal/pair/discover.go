package pair

import (
	"context"
	"time"

	"github.com/grandcat/zeroconf"
)

const ServiceType = "_akira-pair._tcp"

type SwitchInfo struct {
	Name string `json:"name"`
	Host string `json:"host"`
	Port int    `json:"port"`
}

func Discover(timeout time.Duration) ([]SwitchInfo, error) {
	resolver, err := zeroconf.NewResolver(nil)
	if err != nil {
		return nil, err
	}

	entries := make(chan *zeroconf.ServiceEntry)
	seen := map[string]SwitchInfo{}
	done := make(chan struct{})

	go func() {
		for e := range entries {
			if len(e.AddrIPv4) == 0 {
				continue
			}
			ip := e.AddrIPv4[0].String()
			seen[ip] = SwitchInfo{Name: e.Instance, Host: ip, Port: e.Port}
		}
		close(done)
	}()

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	if err := resolver.Browse(ctx, ServiceType, "local.", entries); err != nil {
		return nil, err
	}

	<-ctx.Done()
	<-done

	out := make([]SwitchInfo, 0, len(seen))
	for _, v := range seen {
		out = append(out, v)
	}
	return out, nil
}
