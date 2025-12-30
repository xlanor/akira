package tui

import (
	"fmt"
	"strings"

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
	return Model{
		state:       s,
		currentStep: StepDUID,
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
	if m.state.GetDUID() != "" {
		m.currentStep = StepLogin
		tokenInfo := m.state.GetTokenInfo()
		if tokenInfo.HasAccessToken && !tokenInfo.IsExpired {
			m.currentStep = StepServer
		}
	}
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

	b.WriteString(TitleStyle.Render("AKIRA COMPANION"))
	b.WriteString("\n\n")

	b.WriteString(m.renderNATInfo())
	b.WriteString("\n\n")

	b.WriteString(m.renderStepIndicator(StepDUID, "Configure DUID"))
	b.WriteString("\n")
	b.WriteString(m.renderStepIndicator(StepLogin, "PSN Login"))
	b.WriteString("\n")
	b.WriteString(m.renderStepIndicator(StepServer, "Start Server"))
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
	b.WriteString(HelpStyle.Render("Press q to quit"))

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

func (m Model) renderNATInfo() string {
	if m.natLoading {
		return MutedStyle.Render("Network: ") + MutedStyle.Render("Detecting NAT type...")
	}

	if m.natResult == nil {
		return MutedStyle.Render("Network: ") + MutedStyle.Render("Unknown")
	}

	if m.natResult.Error != nil {
		return MutedStyle.Render("Network: ") + ErrorStyle.Render("Failed: "+m.natResult.Error.Error())
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

	result := MutedStyle.Render("Mapping:   ") + mappingStyle.Render(m.natResult.Mapping.String()) + "\n"
	result += MutedStyle.Render("Filtering: ") + filteringStyle.Render(m.natResult.Filtering.String()) + "\n"
	result += MutedStyle.Render("External:  ") + MutedStyle.Render(m.natResult.ExternalIP)
	return result
}
