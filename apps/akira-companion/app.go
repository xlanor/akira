package main

import (
	"context"

	"akira-companion/internal/backend"
	"akira-companion/internal/pair"
	"akira-companion/internal/psn"
	"akira-companion/internal/state"

	"github.com/wailsapp/wails/v2/pkg/runtime"
)

type App struct {
	ctx     context.Context
	backend *backend.Backend
	state   *state.AppState
}

func NewApp() *App {
	s := state.NewAppState()
	s.Load()
	if s.GetDUID() == "" {
		s.SetDUID(psn.GenerateRandomDUID())
	}
	s.Save()
	return &App{
		backend: backend.New(s),
		state:   s,
	}
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
}

func (a *App) Status() backend.Status {
	return a.backend.Status()
}

func (a *App) Login(npsso string) (backend.Status, error) {
	return a.backend.Login(npsso)
}

func (a *App) RegenerateDUID() (backend.Status, error) {
	return a.backend.RegenerateDUID()
}

func (a *App) PushToSwitch(host string, port int, code string) (backend.PushOutcome, error) {
	return a.backend.PushToSwitch(host, port, code)
}

func (a *App) DetectNAT() backend.NatInfo {
	return a.backend.DetectNAT()
}

func (a *App) DiscoverSwitches() []pair.SwitchInfo {
	return a.backend.DiscoverSwitches()
}

func (a *App) OpenPsnLogin() {
	runtime.BrowserOpenURL(a.ctx, a.backend.PsnLoginURL())
}

func (a *App) OpenNpssoPage() {
	runtime.BrowserOpenURL(a.ctx, a.backend.NpssoPageURL())
}
