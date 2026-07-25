package tui

import (
	"strings"

	"akira-companion/internal/i18n"
	"akira-companion/internal/psn"
	"akira-companion/internal/state"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
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
	message       string
	accountInfo   *state.AccountInfo
}

func NewLoginModel(s *state.AppState) LoginModel {
	redirectInput := textinput.New()
	redirectInput.Placeholder = i18n.T("login.placeholder_npsso")
	redirectInput.CharLimit = 2048
	redirectInput.Width = 60

	loginURL := psn.SSOCookieURL

	loginState := loginStateShowURL
	var accountInfo *state.AccountInfo
	if tokenInfo := s.GetTokenInfo(); tokenInfo.HasAccessToken && !tokenInfo.IsExpired {
		loginState = loginStateSuccess
		accountInfo = s.GetAccountInfo()
	}

	return LoginModel{
		state:         s,
		loginState:    loginState,
		redirectInput: redirectInput,
		loginURL:      loginURL,
		accountInfo:   accountInfo,
	}
}

func (m LoginModel) Init() tea.Cmd {
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
			m.message = i18n.T("login.msg_success")
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
		case "b":
			return m, GoBackStep
		}

	case loginStateWaitingForRedirect:
		switch msg.String() {
		case "esc":
			m.loginState = loginStateShowURL
			m.redirectInput.Blur()
			return m, nil
		case "enter":
			npsso := strings.TrimSpace(m.redirectInput.Value())
			if npsso != "" {
				m.loginState = loginStateExchanging
				return m, m.loginWithNpsso(npsso)
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

func (m LoginModel) loginWithNpsso(npsso string) tea.Cmd {
	return func() tea.Msg {
		code, err := psn.GetAuthCodeFromNpsso(npsso, m.state.GetDUID())
		if err != nil {
			return tokenExchangeResultMsg{err: err}
		}

		tokens, err := psn.ExchangeCodeForTokens(code)
		if err != nil {
			return tokenExchangeResultMsg{err: err}
		}

		m.state.SetTokens(tokens)
		m.state.SetNpsso(npsso)

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
		b.WriteString(i18n.T("login.title") + "\n\n")
		b.WriteString(i18n.T("login.url_prompt"))
		b.WriteString("\n\n")
		b.WriteString(m.loginURL)
		b.WriteString("\n\n")
		b.WriteString(MutedStyle.Render(i18n.T("login.press_enter_ready")))
		b.WriteString("\n")
		b.WriteString(MutedStyle.Render(i18n.T("login.back_help")))

	case loginStateWaitingForRedirect:
		b.WriteString(i18n.T("login.paste_prompt") + "\n\n")
		b.WriteString(m.redirectInput.View())
		b.WriteString("\n\n")
		b.WriteString(MutedStyle.Render(i18n.T("login.redirect_hint")))
		b.WriteString("\n")
		b.WriteString(MutedStyle.Render(i18n.T("login.submit_help")))

	case loginStateExchanging:
		b.WriteString(i18n.T("login.exchanging") + "\n\n")
		b.WriteString(MutedStyle.Render(i18n.T("login.please_wait")))

	case loginStateSuccess:
		b.WriteString(SuccessStyle.Render(i18n.T("login.success")))
		b.WriteString("\n\n")
		if m.accountInfo != nil {
			b.WriteString(i18n.Tf("login.online_id", map[string]interface{}{"ID": m.accountInfo.OnlineID}) + "\n")
			b.WriteString(i18n.Tf("login.account_id", map[string]interface{}{"ID": m.accountInfo.AccountID}) + "\n")
		}
		tokenInfo := m.state.GetTokenInfo()
		if tokenInfo.HasAccessToken {
			b.WriteString("\n")
			b.WriteString(i18n.Tf("login.access_token", map[string]interface{}{"Token": tokenInfo.AccessToken[:20]}) + "\n")
			b.WriteString(i18n.Tf("login.refresh_token", map[string]interface{}{"Token": tokenInfo.RefreshToken[:20]}) + "\n")
		}
		b.WriteString("\n")
		b.WriteString(MutedStyle.Render(i18n.T("login.continue_help")))

	case loginStateError:
		b.WriteString(ErrorStyle.Render(i18n.T("login.failed")))
		b.WriteString("\n\n")
		b.WriteString(ErrorStyle.Render(m.message))
		b.WriteString("\n\n")
		b.WriteString(MutedStyle.Render(i18n.T("login.retry_help")))
	}

	return b.String()
}
