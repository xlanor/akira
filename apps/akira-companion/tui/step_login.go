package tui

import (
	"bytes"
	"fmt"
	"strings"

	"akira-companion/internal/psn"
	"akira-companion/internal/state"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/mdp/qrterminal/v3"
)

type loginState int

const (
	loginStateShowURL loginState = iota
	loginStateWaitingForRedirect
	loginStateExchanging
	loginStateSuccess
	loginStateError
)

type LoginModel struct {
	state         *state.AppState
	loginState    loginState
	redirectInput textinput.Model
	loginURL      string
	qrCode        string
	message       string
	accountInfo   *state.AccountInfo
}

func NewLoginModel(s *state.AppState) LoginModel {
	redirectInput := textinput.New()
	redirectInput.Placeholder = "Paste the redirect URL here..."
	redirectInput.CharLimit = 2048
	redirectInput.Width = 60

	duid := s.GetDUID()
	loginURL := psn.GenerateLoginURL(duid)

	var qrBuf bytes.Buffer
	qrterminal.GenerateHalfBlock(loginURL, qrterminal.L, &qrBuf)

	return LoginModel{
		state:        s,
		loginState:   loginStateShowURL,
		redirectInput: redirectInput,
		loginURL:     loginURL,
		qrCode:       qrBuf.String(),
	}
}

func (m LoginModel) Init() tea.Cmd {
	tokenInfo := m.state.GetTokenInfo()
	if tokenInfo.HasAccessToken && !tokenInfo.IsExpired {
		m.loginState = loginStateSuccess
		m.accountInfo = m.state.GetAccountInfo()
	}
	return nil
}

func (m LoginModel) Update(msg tea.Msg) (LoginModel, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		return m.handleKeyMsg(msg)

	case tokenExchangeResultMsg:
		if msg.err != nil {
			m.loginState = loginStateError
			m.message = msg.err.Error()
		} else {
			m.loginState = loginStateSuccess
			m.accountInfo = m.state.GetAccountInfo()
			m.message = "Login successful!"
		}
		return m, nil
	}

	if m.loginState == loginStateWaitingForRedirect {
		var cmd tea.Cmd
		m.redirectInput, cmd = m.redirectInput.Update(msg)
		return m, cmd
	}

	return m, nil
}

func (m LoginModel) handleKeyMsg(msg tea.KeyMsg) (LoginModel, tea.Cmd) {
	switch m.loginState {
	case loginStateShowURL:
		switch msg.String() {
		case "enter":
			m.loginState = loginStateWaitingForRedirect
			m.redirectInput.Focus()
			return m, textinput.Blink
		}

	case loginStateWaitingForRedirect:
		switch msg.String() {
		case "esc":
			m.loginState = loginStateShowURL
			m.redirectInput.Blur()
			return m, nil
		case "enter":
			redirectURL := m.redirectInput.Value()
			if redirectURL != "" {
				m.loginState = loginStateExchanging
				return m, m.exchangeToken(redirectURL)
			}
		default:
			var cmd tea.Cmd
			m.redirectInput, cmd = m.redirectInput.Update(msg)
			return m, cmd
		}

	case loginStateSuccess:
		switch msg.String() {
		case "n":
			return m, func() tea.Msg { return StepCompleteMsg{} }
		case "r":
			m.loginState = loginStateShowURL
			m.redirectInput.Reset()
			return m, nil
		}

	case loginStateError:
		switch msg.String() {
		case "enter", "r":
			m.loginState = loginStateShowURL
			m.redirectInput.Reset()
			m.message = ""
			return m, nil
		}
	}

	return m, nil
}

type tokenExchangeResultMsg struct {
	err error
}

func (m LoginModel) exchangeToken(redirectURL string) tea.Cmd {
	return func() tea.Msg {
		code, err := psn.ExtractCodeFromRedirect(redirectURL)
		if err != nil {
			return tokenExchangeResultMsg{err: err}
		}

		tokens, err := psn.ExchangeCodeForTokens(code)
		if err != nil {
			return tokenExchangeResultMsg{err: err}
		}

		m.state.SetTokens(tokens)

		accountInfo, err := psn.GetAccountInfo(tokens.AccessToken)
		if err != nil {
			return tokenExchangeResultMsg{err: err}
		}

		m.state.SetAccountInfo(accountInfo)
		m.state.Save()

		return tokenExchangeResultMsg{err: nil}
	}
}

func (m LoginModel) View() string {
	var b strings.Builder

	switch m.loginState {
	case loginStateShowURL:
		b.WriteString("Login to PlayStation Network\n\n")
		b.WriteString(WarningStyle.Render(">>> OPEN THIS URL IN A WINDOWS BROWSER (SONY BLOCKS LINUX) <<<"))
		b.WriteString("\n\n")
		b.WriteString(m.loginURL)
		b.WriteString("\n\n")
		b.WriteString(WarningStyle.Render(">>> OR SCAN THIS QR CODE WITH YOUR PHONE <<<"))
		b.WriteString("\n")
		b.WriteString(m.qrCode)
		b.WriteString("\n")
		b.WriteString(MutedStyle.Render("Press Enter when ready to paste the redirect URL"))

	case loginStateWaitingForRedirect:
		b.WriteString("Paste the redirect URL after logging in:\n\n")
		b.WriteString(m.redirectInput.View())
		b.WriteString("\n\n")
		b.WriteString(MutedStyle.Render("The URL starts with: https://remoteplay.dl.playstation.net/remoteplay/redirect?..."))
		b.WriteString("\n")
		b.WriteString(MutedStyle.Render("Press Enter to submit, Esc to go back"))

	case loginStateExchanging:
		b.WriteString("Exchanging authorization code for tokens...\n\n")
		b.WriteString(MutedStyle.Render("Please wait..."))

	case loginStateSuccess:
		b.WriteString(SuccessStyle.Render("✓ Login Successful!"))
		b.WriteString("\n\n")
		if m.accountInfo != nil {
			b.WriteString(fmt.Sprintf("Online ID: %s\n", m.accountInfo.OnlineID))
			b.WriteString(fmt.Sprintf("Account ID: %s\n", m.accountInfo.AccountID))
		}
		tokenInfo := m.state.GetTokenInfo()
		if tokenInfo.HasAccessToken {
			b.WriteString(fmt.Sprintf("\nAccess Token: %s...\n", tokenInfo.AccessToken[:20]))
			b.WriteString(fmt.Sprintf("Refresh Token: %s...\n", tokenInfo.RefreshToken[:20]))
		}
		b.WriteString("\n")
		b.WriteString(MutedStyle.Render("Press 'n' to continue, 'r' to redo login"))

	case loginStateError:
		b.WriteString(ErrorStyle.Render("✗ Login Failed"))
		b.WriteString("\n\n")
		b.WriteString(ErrorStyle.Render(m.message))
		b.WriteString("\n\n")
		b.WriteString(MutedStyle.Render("Press Enter or 'r' to retry"))
	}

	return b.String()
}
