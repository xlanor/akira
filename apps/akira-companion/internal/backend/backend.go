package backend

import (
	"fmt"
	"time"

	"akira-companion/internal/nat"
	"akira-companion/internal/pair"
	"akira-companion/internal/psn"
	"akira-companion/internal/state"
)

type Backend struct {
	state *state.AppState
}

func New(s *state.AppState) *Backend {
	return &Backend{state: s}
}

type Status struct {
	DUID       string `json:"duid"`
	HasToken   bool   `json:"hasToken"`
	TokenValid bool   `json:"tokenValid"`
	OnlineID   string `json:"onlineId"`
	AccountID  string `json:"accountId"`
}

func (b *Backend) Status() Status {
	ti := b.state.GetTokenInfo()
	st := Status{
		DUID:       b.state.GetDUID(),
		HasToken:   ti.HasAccessToken,
		TokenValid: ti.HasAccessToken && !ti.IsExpired,
	}
	if acct := b.state.GetAccountInfo(); acct != nil {
		st.OnlineID = acct.OnlineID
		st.AccountID = acct.AccountID
	}
	return st
}

func (b *Backend) RegenerateDUID() (Status, error) {
	duid := psn.GenerateRandomDUID()
	if err := psn.ValidateDUID(duid); err != nil {
		return b.Status(), err
	}

	b.state.SetDUID(duid)
	b.state.ClearSession()
	if err := b.state.Save(); err != nil {
		return b.Status(), err
	}

	return b.Status(), nil
}

func (b *Backend) PsnLoginURL() string {
	return "https://www.playstation.com/"
}

func (b *Backend) NpssoPageURL() string {
	return psn.SSOCookieURL
}

func (b *Backend) Login(npsso string) (Status, error) {
	duid := b.state.GetDUID()

	code, err := psn.GetAuthCodeFromNpsso(npsso, duid)
	if err != nil {
		return Status{}, err
	}
	tokens, err := psn.ExchangeCodeForTokens(code)
	if err != nil {
		return Status{}, err
	}
	b.state.SetTokens(tokens)
	b.state.SetNpsso(npsso)

	if mobileCode, err := psn.GetMobileAuthCodeFromNpsso(npsso); err == nil {
		if mobileTokens, err := psn.ExchangeCodeForMobileTokens(mobileCode); err == nil {
			b.state.SetMobileTokens(mobileTokens)
		}
	}

	account, err := psn.GetAccountInfo(tokens.AccessToken)
	if err != nil {
		return Status{}, err
	}
	b.state.SetAccountInfo(account)
	b.state.Save()

	return b.Status(), nil
}

type PushOutcome struct {
	Result  string `json:"result"`
	Message string `json:"message"`
}

func (b *Backend) PushToSwitch(host string, port int, code string) (PushOutcome, error) {
	if host == "" {
		return PushOutcome{Result: "error", Message: "missing host"}, fmt.Errorf("missing host")
	}
	if port <= 0 || port > 65535 {
		port = 8080
	}

	payload, err := pair.BuildPayload(b.state)
	if err != nil {
		return PushOutcome{Result: "error", Message: err.Error()}, err
	}

	res, err := pair.Push(host, port, code, payload, 15*time.Second)
	if err != nil {
		return PushOutcome{Result: "error", Message: err.Error()}, nil
	}
	switch res {
	case pair.PushImported:
		return PushOutcome{Result: "imported"}, nil
	case pair.PushBadCode:
		return PushOutcome{Result: "bad_code"}, nil
	default:
		return PushOutcome{Result: "error"}, nil
	}
}

func (b *Backend) DiscoverSwitches() []pair.SwitchInfo {
	list, err := pair.Discover(4 * time.Second)
	if err != nil {
		return []pair.SwitchInfo{}
	}
	return list
}

type NatInfo struct {
	Mapping    string `json:"mapping"`
	Filtering  string `json:"filtering"`
	ExternalIP string `json:"externalIp"`
	Error      string `json:"error,omitempty"`
}

func (b *Backend) DetectNAT() NatInfo {
	r := nat.Detect()
	info := NatInfo{ExternalIP: r.ExternalIP}
	if r.Error != nil {
		info.Error = r.Error.Error()
	}
	info.Mapping = mappingName(r.Mapping)
	info.Filtering = filteringName(r.Filtering)
	return info
}

func mappingName(m nat.MappingType) string {
	switch m {
	case nat.MappingEndpointIndependent:
		return "endpoint-independent"
	case nat.MappingAddressDependent:
		return "address-dependent"
	case nat.MappingAddressPortDependent:
		return "address-port-dependent"
	default:
		return "unknown"
	}
}

func filteringName(f nat.FilteringType) string {
	switch f {
	case nat.FilteringEndpointIndependent:
		return "endpoint-independent"
	case nat.FilteringAddressDependent:
		return "address-dependent"
	case nat.FilteringAddressPortDependent:
		return "address-port-dependent"
	default:
		return "unknown"
	}
}
