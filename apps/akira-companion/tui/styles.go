package tui

import "github.com/charmbracelet/lipgloss"

var (
	primaryColor   = lipgloss.Color("#7C3AED") // Purple
	secondaryColor = lipgloss.Color("#10B981") // Green
	mutedColor     = lipgloss.Color("#6B7280") // Gray
	errorColor     = lipgloss.Color("#EF4444") // Red
	successColor   = lipgloss.Color("#10B981") // Green

	TitleStyle = lipgloss.NewStyle().
			Bold(true).
			Foreground(primaryColor).
			MarginBottom(1)

	StepCompleteStyle = lipgloss.NewStyle().
				Foreground(successColor).
				SetString("✓")

	StepPendingStyle = lipgloss.NewStyle().
				Foreground(mutedColor).
				SetString("○")

	StepCurrentStyle = lipgloss.NewStyle().
				Foreground(primaryColor).
				Bold(true).
				SetString("●")

	StepLabelStyle = lipgloss.NewStyle().
			PaddingLeft(1)

	StepLabelActiveStyle = lipgloss.NewStyle().
				PaddingLeft(1).
				Bold(true).
				Foreground(primaryColor)

	BoxStyle = lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(primaryColor).
			Padding(1, 2)

	InputLabelStyle = lipgloss.NewStyle().
			Foreground(mutedColor).
			MarginBottom(1)

	InputStyle = lipgloss.NewStyle().
			Border(lipgloss.NormalBorder()).
			BorderForeground(mutedColor).
			Padding(0, 1)

	InputFocusedStyle = lipgloss.NewStyle().
				Border(lipgloss.NormalBorder()).
				BorderForeground(primaryColor).
				Padding(0, 1)

	ButtonStyle = lipgloss.NewStyle().
			Background(primaryColor).
			Foreground(lipgloss.Color("#FFFFFF")).
			Padding(0, 2).
			MarginRight(1)

	ButtonInactiveStyle = lipgloss.NewStyle().
				Background(mutedColor).
				Foreground(lipgloss.Color("#FFFFFF")).
				Padding(0, 2).
				MarginRight(1)

	SuccessStyle = lipgloss.NewStyle().
			Foreground(successColor)

	ErrorStyle = lipgloss.NewStyle().
			Foreground(errorColor)

	MutedStyle = lipgloss.NewStyle().
			Foreground(mutedColor)

	WarningStyle = lipgloss.NewStyle().
			Foreground(lipgloss.Color("#FF0000")).
			Bold(true).
			Blink(true)

	HelpStyle = lipgloss.NewStyle().
			Foreground(mutedColor).
			MarginTop(1)

	DividerStyle = lipgloss.NewStyle().
			Foreground(mutedColor).
			SetString("─────────────────────────────────────────")
)
