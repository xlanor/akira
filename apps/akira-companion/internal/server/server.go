package server

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"sync"
	"time"

	"akira-companion/internal/state"
)

type Status struct {
	Running   bool     `json:"running"`
	Port      int      `json:"port"`
	Addresses []string `json:"addresses"`
	StartedAt int64    `json:"started_at,omitempty"`
}

type Server struct {
	mu         sync.RWMutex
	state      *state.AppState
	httpServer *http.Server
	port       int
	startedAt  int64
}

func New(appState *state.AppState) *Server {
	return &Server{
		state: appState,
	}
}

func (s *Server) Start(port int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.httpServer != nil {
		return fmt.Errorf("server already running")
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/status", s.handleStatus)
	mux.HandleFunc("/duid", s.handleDUID)
	mux.HandleFunc("/token", s.handleToken)
	mux.HandleFunc("/account", s.handleAccount)

	s.httpServer = &http.Server{
		Addr:    fmt.Sprintf(":%d", port),
		Handler: corsMiddleware(mux),
	}
	s.port = port
	s.startedAt = time.Now().Unix()

	go func() {
		if err := s.httpServer.ListenAndServe(); err != http.ErrServerClosed {
			fmt.Printf("HTTP server error: %v\n", err)
		}
	}()

	return nil
}

func (s *Server) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.httpServer == nil {
		return nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := s.httpServer.Shutdown(ctx)
	s.httpServer = nil
	s.port = 0
	s.startedAt = 0

	return err
}

func (s *Server) GetStatus() *Status {
	s.mu.RLock()
	defer s.mu.RUnlock()

	status := &Status{
		Running:   s.httpServer != nil,
		Port:      s.port,
		StartedAt: s.startedAt,
	}

	if status.Running {
		status.Addresses = getLocalAddresses()
	}

	return status
}

func corsMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusOK)
			return
		}

		next.ServeHTTP(w, r)
	})
}

func getLocalAddresses() []string {
	var addresses []string

	interfaces, err := net.Interfaces()
	if err != nil {
		return addresses
	}

	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}

		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		for _, addr := range addrs {
			if ipnet, ok := addr.(*net.IPNet); ok {
				if ip4 := ipnet.IP.To4(); ip4 != nil {
					addresses = append(addresses, ip4.String())
				}
			}
		}
	}

	return addresses
}
