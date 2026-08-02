package pair

import (
	"encoding/json"

	"akira-companion/internal/state"
)

type Payload struct {
	DUID         string `json:"duid"`
	AccountID    string `json:"account_id"`
	OnlineID     string `json:"online_id"`
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int    `json:"expires_in"`
	ExpiresAt    int64  `json:"expires_at"`
	IsExpired    bool   `json:"is_expired"`

	MobileAccessToken  string `json:"psn_mobile_sso_access_token,omitempty"`
	MobileRefreshToken string `json:"psn_mobile_sso_refresh_token,omitempty"`
	MobileExpiresAt    int64  `json:"psn_mobile_sso_expires_at,omitempty"`
}

func BuildPayload(s *state.AppState) ([]byte, error) {
	p := Payload{DUID: s.GetDUID()}

	if acct := s.GetAccountInfo(); acct != nil {
		p.AccountID = acct.AccountID
		p.OnlineID = acct.OnlineID
	}

	ti := s.GetTokenInfo()
	p.AccessToken = ti.AccessToken
	p.RefreshToken = ti.RefreshToken
	p.ExpiresIn = ti.ExpiresIn
	p.ExpiresAt = ti.ExpiresAt
	p.IsExpired = ti.IsExpired

	if mob := s.GetMobileTokens(); mob != nil {
		p.MobileAccessToken = mob.AccessToken
		p.MobileRefreshToken = mob.RefreshToken
		if mob.ObtainedAt > 0 && mob.ExpiresIn > 0 {
			p.MobileExpiresAt = mob.ObtainedAt + int64(mob.ExpiresIn)
		}
	}

	return json.Marshal(p)
}
