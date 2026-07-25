package tui

import (
	"fmt"
	"strings"

	"akira-companion/internal/i18n"
	"akira-companion/internal/nat"
	"akira-companion/internal/state"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type Step int

const (
	StepDUID Step = iota
	StepLogin
	StepServer
)

type Model struct {
	state       *state.AppState
	currentStep Step
	width       int
	height      int

	duidModel   DUIDModel
	loginModel  LoginModel
	serverModel ServerModel

	natLoading bool
	natResult  *nat.DetectionResult
}

func NewModel(s *state.AppState) Model {
	currentStep := StepDUID
	if s.GetDUID() != "" {
		currentStep = StepLogin
		tokenInfo := s.GetTokenInfo()
		if tokenInfo.HasAccessToken && !tokenInfo.IsExpired {
			currentStep = StepServer
		}
	}

	return Model{
		state:       s,
		currentStep: currentStep,
		duidModel:   NewDUIDModel(s),
		loginModel:  NewLoginModel(s),
		serverModel: NewServerModel(s),
		natLoading:  true,
	}
}

type NATDetectedMsg struct {
	Result nat.DetectionResult
}

func detectNAT() tea.Msg {
	result := nat.Detect()
	return NATDetectedMsg{Result: result}
}

func (m Model) Init() tea.Cmd {
	return tea.Batch(m.currentStepInit(), detectNAT)
}

func (m Model) currentStepInit() tea.Cmd {
	switch m.currentStep {
	case StepDUID:
		return m.duidModel.Init()
	case StepLogin:
		return m.loginModel.Init()
	case StepServer:
		return m.serverModel.Init()
	}
	return nil
}

func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+c", "q":
			if m.serverModel.serverRunning {
				m.serverModel.stopServer()
			}
			return m, tea.Quit
		}

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height

	case NATDetectedMsg:
		m.natLoading = false
		m.natResult = &msg.Result
		return m, nil

	case StepCompleteMsg:
		switch m.currentStep {
		case StepDUID:
			m.currentStep = StepLogin
			m.loginModel = NewLoginModel(m.state)
			return m, m.loginModel.Init()
		case StepLogin:
			m.currentStep = StepServer
			m.serverModel = NewServerModel(m.state)
			return m, m.serverModel.Init()
		}
	}

	var cmd tea.Cmd
	switch m.currentStep {
	case StepDUID:
		m.duidModel, cmd = m.duidModel.Update(msg)
	case StepLogin:
		m.loginModel, cmd = m.loginModel.Update(msg)
	case StepServer:
		m.serverModel, cmd = m.serverModel.Update(msg)
	}

	return m, cmd
}

func (m Model) View() string {
	var b strings.Builder

	b.WriteString(TitleStyle.Render(i18n.T("app.title")))
	b.WriteString("\n\n")

	b.WriteString(m.renderNATInfo())
	b.WriteString("\n\n")

	b.WriteString(m.renderStepIndicator(StepDUID, i18n.T("step.configure_duid")))
	b.WriteString("\n")
	b.WriteString(m.renderStepIndicator(StepLogin, i18n.T("step.psn_login")))
	b.WriteString("\n")
	b.WriteString(m.renderStepIndicator(StepServer, i18n.T("step.start_server")))
	b.WriteString("\n\n")

	b.WriteString(DividerStyle.Render())
	b.WriteString("\n\n")

	switch m.currentStep {
	case StepDUID:
		b.WriteString(m.duidModel.View())
	case StepLogin:
		b.WriteString(m.loginModel.View())
	case StepServer:
		b.WriteString(m.serverModel.View())
	}

	b.WriteString("\n\n")
	b.WriteString(HelpStyle.Render(i18n.T("app.quit_help")))

	return lipgloss.NewStyle().Padding(1, 2).Render(b.String())
}

func (m Model) renderStepIndicator(step Step, label string) string {
	var indicator string
	var labelStyle lipgloss.Style

	if step < m.currentStep {
		indicator = StepCompleteStyle.Render()
		labelStyle = StepLabelStyle.Copy().Foreground(successColor)
	} else if step == m.currentStep {
		indicator = StepCurrentStyle.Render()
		labelStyle = StepLabelActiveStyle
	} else {
		indicator = StepPendingStyle.Render()
		labelStyle = StepLabelStyle
	}

	return fmt.Sprintf("%s%s", indicator, labelStyle.Render(label))
}

type StepCompleteMsg struct{}

func CompleteStep() tea.Msg {
	return StepCompleteMsg{}
}

func mappingLabel(m nat.MappingType) string {
	switch m {
	case nat.MappingEndpointIndependent:
		return i18n.T("nat.endpoint_independent")
	case nat.MappingAddressDependent:
		return i18n.T("nat.address_dependent")
	case nat.MappingAddressPortDependent:
		return i18n.T("nat.address_port_dependent")
	default:
		return i18n.T("nat.unknown")
	}
}

func filteringLabel(f nat.FilteringType) string {
	switch f {
	case nat.FilteringEndpointIndependent:
		return i18n.T("nat.endpoint_independent")
	case nat.FilteringAddressDependent:
		return i18n.T("nat.address_dependent")
	case nat.FilteringAddressPortDependent:
		return i18n.T("nat.address_port_dependent")
	default:
		return i18n.T("nat.unknown")
	}
}

func (m Model) renderNATInfo() string {
	if m.natLoading {
		return MutedStyle.Render(i18n.T("nat.network_label")) + MutedStyle.Render(i18n.T("nat.detecting"))
	}

	if m.natResult == nil {
		return MutedStyle.Render(i18n.T("nat.network_label")) + MutedStyle.Render(i18n.T("nat.unknown"))
	}

	if m.natResult.Error != nil {
		return MutedStyle.Render(i18n.T("nat.network_label")) + ErrorStyle.Render(i18n.Tf("nat.failed", map[string]interface{}{"Error": m.natResult.Error.Error()}))
	}

	var mappingStyle, filteringStyle lipgloss.Style
	warningStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("#F59E0B"))

	switch m.natResult.Mapping {
	case nat.MappingEndpointIndependent:
		mappingStyle = SuccessStyle
	case nat.MappingAddressPortDependent:
		mappingStyle = ErrorStyle
	default:
		mappingStyle = warningStyle
	}

	switch m.natResult.Filtering {
	case nat.FilteringEndpointIndependent:
		filteringStyle = SuccessStyle
	case nat.FilteringAddressDependent, nat.FilteringAddressPortDependent:
		filteringStyle = warningStyle
	default:
		filteringStyle = MutedStyle
	}

	result := MutedStyle.Render(i18n.T("nat.mapping_label")) + mappingStyle.Render(mappingLabel(m.natResult.Mapping)) + "\n"
	result += MutedStyle.Render(i18n.T("nat.filtering_label")) + filteringStyle.Render(filteringLabel(m.natResult.Filtering)) + "\n"
	result += MutedStyle.Render(i18n.T("nat.external_label")) + MutedStyle.Render(m.natResult.ExternalIP)
	return result
}
