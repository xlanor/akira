package main

import (
	"fmt"
	"os"

	"akira-companion/internal/state"
	"akira-companion/tui"

	tea "github.com/charmbracelet/bubbletea"
)

func main() {
	appState := state.NewAppState()
	if err := appState.Load(); err != nil {
		fmt.Printf("Warning: Could not load state: %v\n", err)
	}

	model := tui.NewModel(appState)
	p := tea.NewProgram(model, tea.WithAltScreen())

	if _, err := p.Run(); err != nil {
		fmt.Printf("Error running program: %v\n", err)
		os.Exit(1)
	}
}
