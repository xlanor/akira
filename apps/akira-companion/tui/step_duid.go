package tui

import (
	"fmt"
	"strings"

	"akira-companion/internal/psn"
	"akira-companion/internal/state"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
)

type duidAction int

const (
	actionGenerateRandom duidAction = iota
	actionGenerateFromSeed
	actionEnterManual
)

type DUIDModel struct {
	state         *state.AppState
	currentAction duidAction
	seedInput     textinput.Model
	manualInput   textinput.Model
	inputActive   bool
	currentDUID   string
	message       string
	isError       bool
}

func NewDUIDModel(s *state.AppState) DUIDModel {
	seedInput := textinput.New()
	seedInput.Placeholder = "Enter a seed phrase..."
	seedInput.CharLimit = 256

	manualInput := textinput.New()
	manualInput.Placeholder = "Enter 48-character hex DUID..."
	manualInput.CharLimit = 48

	return DUIDModel{
		state:       s,
		seedInput:   seedInput,
		manualInput: manualInput,
		currentDUID: s.GetDUID(),
	}
}

func (m DUIDModel) Init() tea.Cmd {
	return nil
}

func (m DUIDModel) Update(msg tea.Msg) (DUIDModel, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		if m.inputActive {
			return m.handleInputMode(msg)
		}
		return m.handleMenuMode(msg)
	}

	var cmd tea.Cmd
	if m.inputActive {
		switch m.currentAction {
		case actionGenerateFromSeed:
			m.seedInput, cmd = m.seedInput.Update(msg)
		case actionEnterManual:
			m.manualInput, cmd = m.manualInput.Update(msg)
		}
	}

	return m, cmd
}

func (m DUIDModel) handleMenuMode(msg tea.KeyMsg) (DUIDModel, tea.Cmd) {
	switch msg.String() {
	case "up", "k":
		if m.currentAction > 0 {
			m.currentAction--
		}
	case "down", "j":
		if m.currentAction < actionEnterManual {
			m.currentAction++
		}
	case "enter":
		switch m.currentAction {
		case actionGenerateRandom:
			duid := psn.GenerateRandomDUID()
			m.state.SetDUID(duid)
			m.state.Save()
			m.currentDUID = duid
			m.message = "Random DUID generated!"
			m.isError = false
		case actionGenerateFromSeed:
			m.inputActive = true
			m.seedInput.Focus()
			return m, textinput.Blink
		case actionEnterManual:
			m.inputActive = true
			m.manualInput.Focus()
			return m, textinput.Blink
		}
	case "n":
		if m.currentDUID != "" {
			return m, func() tea.Msg { return StepCompleteMsg{} }
		}
	}
	return m, nil
}

func (m DUIDModel) handleInputMode(msg tea.KeyMsg) (DUIDModel, tea.Cmd) {
	switch msg.String() {
	case "esc":
		m.inputActive = false
		m.seedInput.Blur()
		m.manualInput.Blur()
		return m, nil
	case "enter":
		switch m.currentAction {
		case actionGenerateFromSeed:
			seed := m.seedInput.Value()
			if seed != "" {
				duid := psn.GenerateDUIDFromSeed(seed)
				m.state.SetDUID(duid)
				m.state.SetSeed(seed)
				m.state.Save()
				m.currentDUID = duid
				m.message = "DUID generated from seed!"
				m.isError = false
			}
		case actionEnterManual:
			duid := strings.ToLower(m.manualInput.Value())
			if err := psn.ValidateDUID(duid); err != nil {
				m.message = err.Error()
				m.isError = true
				return m, nil
			}
			m.state.SetDUID(duid)
			m.state.Save()
			m.currentDUID = duid
			m.message = "DUID saved!"
			m.isError = false
		}
		m.inputActive = false
		m.seedInput.Blur()
		m.manualInput.Blur()
		return m, nil
	}

	var cmd tea.Cmd
	switch m.currentAction {
	case actionGenerateFromSeed:
		m.seedInput, cmd = m.seedInput.Update(msg)
	case actionEnterManual:
		m.manualInput, cmd = m.manualInput.Update(msg)
	}
	return m, cmd
}

func (m DUIDModel) View() string {
	var b strings.Builder

	b.WriteString("Configure your Device Unique ID (DUID)\n\n")

	options := []string{
		"Generate random DUID",
		"Generate from seed (reproducible)",
		"Enter DUID manually",
	}

	for i, opt := range options {
		cursor := "  "
		if duidAction(i) == m.currentAction {
			cursor = "> "
		}
		b.WriteString(fmt.Sprintf("%s%s\n", cursor, opt))
	}

	if m.inputActive {
		b.WriteString("\n")
		switch m.currentAction {
		case actionGenerateFromSeed:
			b.WriteString(InputLabelStyle.Render("Seed phrase:"))
			b.WriteString("\n")
			b.WriteString(m.seedInput.View())
		case actionEnterManual:
			b.WriteString(InputLabelStyle.Render("DUID (48 hex characters):"))
			b.WriteString("\n")
			b.WriteString(m.manualInput.View())
		}
		b.WriteString("\n")
		b.WriteString(MutedStyle.Render("Press Enter to confirm, Esc to cancel"))
	}

	b.WriteString("\n\n")
	if m.currentDUID != "" {
		b.WriteString(SuccessStyle.Render("Current DUID: "))
		b.WriteString(m.currentDUID)
		b.WriteString("\n\n")
		b.WriteString(MutedStyle.Render("Press 'n' to continue to next step"))
	} else {
		b.WriteString(MutedStyle.Render("No DUID configured yet"))
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
