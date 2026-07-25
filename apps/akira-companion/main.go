package main

import (
	"flag"
	"fmt"
	"os"

	"akira-companion/internal/i18n"
	"akira-companion/internal/psn"
	"akira-companion/internal/state"
	"akira-companion/tui"

	tea "github.com/charmbracelet/bubbletea"
)

func main() {
	lang := flag.String("lang", "en", "language code (e.g. en, ja, zh)")
	flag.Parse()

	i18n.Init(*lang)

	appState := state.NewAppState()
	if err := appState.Load(); err != nil {
		fmt.Printf("%s\n", i18n.Tf("app.warning_load_state", map[string]interface{}{"Error": err}))
	}

	if appState.GetDUID() == "" {
		appState.SetDUID(psn.GenerateRandomDUID())
		appState.Save()
	}

	model := tui.NewModel(appState)
	p := tea.NewProgram(model, tea.WithAltScreen())

	if _, err := p.Run(); err != nil {
		fmt.Printf("%s\n", i18n.Tf("app.error_running", map[string]interface{}{"Error": err}))
		os.Exit(1)
	}
}
