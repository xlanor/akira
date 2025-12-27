package tui

import (
	"fmt"
	"net"
	"strings"
	"time"

	"akira-companion/internal/server"
	"akira-companion/internal/state"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
)

type ServerModel struct {
	state         *state.AppState
	server        *server.Server
	portInput     textinput.Model
	serverRunning bool
	localIPs      []string
	port          int
	message       string
	isError       bool
}

func NewServerModel(s *state.AppState) ServerModel {
	portInput := textinput.New()
	portInput.Placeholder = "8080"
	portInput.CharLimit = 5
	portInput.Width = 10
	portInput.SetValue("8080")

	return ServerModel{
		state:     s,
		portInput: portInput,
		port:      8080,
		localIPs:  getLocalIPs(),
	}
}

func (m ServerModel) Init() tea.Cmd {
	return nil
}

func (m ServerModel) Update(msg tea.Msg) (ServerModel, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		return m.handleKeyMsg(msg)
	}

	if !m.serverRunning {
		var cmd tea.Cmd
		m.portInput, cmd = m.portInput.Update(msg)
		return m, cmd
	}

	return m, nil
}

func (m ServerModel) handleKeyMsg(msg tea.KeyMsg) (ServerModel, tea.Cmd) {
	switch msg.String() {
	case "s":
		if m.serverRunning {
			m.stopServer()
			m.message = "Server stopped"
			m.isError = false
		} else {
			port := 8080
			if p := m.portInput.Value(); p != "" {
				fmt.Sscanf(p, "%d", &port)
			}
			if port < 1 || port > 65535 {
				m.message = "Invalid port number"
				m.isError = true
				return m, nil
			}
			m.port = port
			if err := m.startServer(); err != nil {
				m.message = err.Error()
				m.isError = true
			} else {
				m.message = fmt.Sprintf("Server started on port %d", port)
				m.isError = false
			}
		}
	case "r":
		m.localIPs = getLocalIPs()
		m.message = "IP addresses refreshed"
		m.isError = false
	}

	return m, nil
}

func (m *ServerModel) startServer() error {
	m.server = server.New(m.state)
	if err := m.server.Start(m.port); err != nil {
		return err
	}
	m.serverRunning = true
	return nil
}

func (m *ServerModel) stopServer() {
	if m.server != nil {
		m.server.Stop()
		m.server = nil
	}
	m.serverRunning = false
}

func (m ServerModel) View() string {
	var b strings.Builder

	b.WriteString("PSN Credentials\n")
	b.WriteString(DividerStyle.Render())
	b.WriteString("\n")

	accountInfo := m.state.GetAccountInfo()
	tokenInfo := m.state.GetTokenInfo()

	if accountInfo != nil {
		b.WriteString(fmt.Sprintf("  Online ID:  %s\n", SuccessStyle.Render(accountInfo.OnlineID)))
		b.WriteString(fmt.Sprintf("  Account ID: %s\n", MutedStyle.Render(accountInfo.AccountID)))
	} else {
		b.WriteString(MutedStyle.Render("  No account info available\n"))
	}

	if tokenInfo.HasAccessToken {
		expiryTime := time.Unix(tokenInfo.ExpiresAt, 0)
		timeUntilExpiry := time.Until(expiryTime)

		var expiryStr string
		if tokenInfo.IsExpired {
			expiryStr = ErrorStyle.Render("EXPIRED")
		} else if timeUntilExpiry < time.Hour {
			expiryStr = WarningStyle.Render(fmt.Sprintf("in %d minutes", int(timeUntilExpiry.Minutes())))
		} else {
			expiryStr = SuccessStyle.Render(expiryTime.Format("Jan 02, 2006 03:04 PM"))
		}
		b.WriteString(fmt.Sprintf("  Token Expires: %s\n", expiryStr))
	}

	b.WriteString("\n")

	b.WriteString("HTTP Server for Nintendo Switch\n")
	b.WriteString(DividerStyle.Render())
	b.WriteString("\n")

	if m.serverRunning {
		b.WriteString(SuccessStyle.Render("● Server Running"))
		b.WriteString(fmt.Sprintf(" on port %d\n\n", m.port))
	} else {
		b.WriteString(MutedStyle.Render("○ Server Stopped"))
		b.WriteString("\n\n")
		b.WriteString("Port: ")
		b.WriteString(m.portInput.View())
		b.WriteString("\n\n")
	}

	b.WriteString("Local IP Addresses:\n")
	if len(m.localIPs) == 0 {
		b.WriteString(MutedStyle.Render("  No network interfaces found"))
	} else {
		for _, ip := range m.localIPs {
			if m.serverRunning {
				b.WriteString(fmt.Sprintf("  http://%s:%d\n", ip, m.port))
			} else {
				b.WriteString(fmt.Sprintf("  %s\n", ip))
			}
		}
	}

	if m.serverRunning {
		b.WriteString("\nEndpoints for Switch:\n")
		b.WriteString(MutedStyle.Render("  GET /status  - Check companion status\n"))
		b.WriteString(MutedStyle.Render("  GET /account - Get PSN account info\n"))
		b.WriteString(MutedStyle.Render("  GET /token   - Get PSN tokens\n"))
		b.WriteString(MutedStyle.Render("  GET /duid    - Get current DUID\n"))
	}

	b.WriteString("\n")
	if m.serverRunning {
		b.WriteString(MutedStyle.Render("Press 's' to stop server, 'r' to refresh IPs"))
	} else {
		b.WriteString(MutedStyle.Render("Press 's' to start server, 'r' to refresh IPs"))
	}

	if m.message != "" {
		b.WriteString("\n\n")
		if m.isError {
			b.WriteString(ErrorStyle.Render(m.message))
		} else {
			b.WriteString(SuccessStyle.Render(m.message))
		}
	}

	return b.String()
}

func getLocalIPs() []string {
	var ips []string

	interfaces, err := net.Interfaces()
	if err != nil {
		return ips
	}

	for _, iface := range interfaces {
		if iface.Flags&net.FlagLoopback != 0 || iface.Flags&net.FlagUp == 0 {
			continue
		}

		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}

			if ip != nil && ip.To4() != nil {
				ips = append(ips, ip.String())
			}
		}
	}

	return ips
}
