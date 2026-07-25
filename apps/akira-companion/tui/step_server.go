package tui

import (
	"fmt"
	"net"
	"strings"
	"time"

	"akira-companion/internal/i18n"
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
			m.message = i18n.T("server.msg_stopped")
			m.isError = false
		} else {
			port := 8080
			if p := m.portInput.Value(); p != "" {
				fmt.Sscanf(p, "%d", &port)
			}
			if port < 1 || port > 65535 {
				m.message = i18n.T("server.msg_invalid_port")
				m.isError = true
				return m, nil
			}
			m.port = port
			if err := m.startServer(); err != nil {
				m.message = err.Error()
				m.isError = true
			} else {
				m.message = i18n.Tf("server.msg_started", map[string]interface{}{"Port": port})
				m.isError = false
			}
		}
	case "r":
		m.localIPs = getLocalIPs()
		m.message = i18n.T("server.msg_ips_refreshed")
		m.isError = false
	case "b":
		if m.serverRunning {
			m.stopServer()
		}
		return m, GoBackStep
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

	b.WriteString(i18n.T("server.credentials_title") + "\n")
	b.WriteString(DividerStyle.Render())
	b.WriteString("\n")

	accountInfo := m.state.GetAccountInfo()
	tokenInfo := m.state.GetTokenInfo()

	if accountInfo != nil {
		b.WriteString(i18n.Tf("server.online_id", map[string]interface{}{"ID": SuccessStyle.Render(accountInfo.OnlineID)}) + "\n")
		b.WriteString(i18n.Tf("server.account_id", map[string]interface{}{"ID": MutedStyle.Render(accountInfo.AccountID)}) + "\n")
	} else {
		b.WriteString(MutedStyle.Render(i18n.T("server.no_account_info")) + "\n")
	}

	if tokenInfo.HasAccessToken {
		expiryTime := time.Unix(tokenInfo.ExpiresAt, 0)
		timeUntilExpiry := time.Until(expiryTime)

		var expiryStr string
		if tokenInfo.IsExpired {
			expiryStr = ErrorStyle.Render(i18n.T("server.token_expired"))
		} else if timeUntilExpiry < time.Hour {
			expiryStr = WarningStyle.Render(i18n.Tf("server.token_expires_minutes", map[string]interface{}{"Minutes": int(timeUntilExpiry.Minutes())}))
		} else {
			expiryStr = SuccessStyle.Render(expiryTime.Format("Jan 02, 2006 03:04 PM"))
		}
		b.WriteString(i18n.Tf("server.token_expires_label", map[string]interface{}{"Expiry": expiryStr}) + "\n")
	}

	b.WriteString("\n")

	b.WriteString(i18n.T("server.http_title") + "\n")
	b.WriteString(DividerStyle.Render())
	b.WriteString("\n")

	if m.serverRunning {
		b.WriteString(SuccessStyle.Render(i18n.T("server.running")))
		b.WriteString(i18n.Tf("server.running_port", map[string]interface{}{"Port": m.port}) + "\n\n")
	} else {
		b.WriteString(MutedStyle.Render(i18n.T("server.stopped")))
		b.WriteString("\n\n")
		b.WriteString(i18n.T("server.port_label"))
		b.WriteString(m.portInput.View())
		b.WriteString("\n\n")
	}

	b.WriteString(i18n.T("server.local_ips_title") + "\n")
	if len(m.localIPs) == 0 {
		b.WriteString(MutedStyle.Render(i18n.T("server.no_interfaces")))
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
		b.WriteString("\n" + i18n.T("server.endpoints_title") + "\n")
		b.WriteString(MutedStyle.Render(i18n.T("server.endpoint_status")) + "\n")
		b.WriteString(MutedStyle.Render(i18n.T("server.endpoint_account")) + "\n")
		b.WriteString(MutedStyle.Render(i18n.T("server.endpoint_token")) + "\n")
		b.WriteString(MutedStyle.Render(i18n.T("server.endpoint_duid")) + "\n")
	}

	b.WriteString("\n")
	if m.serverRunning {
		b.WriteString(MutedStyle.Render(i18n.T("server.help_running")))
	} else {
		b.WriteString(MutedStyle.Render(i18n.T("server.help_stopped")))
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
