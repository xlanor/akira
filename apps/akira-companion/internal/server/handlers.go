package server

import (
	"encoding/json"
	"net/http"

	"akira-companion/internal/i18n"
)

type StatusResponse struct {
	Status     string `json:"status"`
	HasDUID    bool   `json:"has_duid"`
	HasToken   bool   `json:"has_token"`
	HasAccount bool   `json:"has_account"`
}

func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, i18n.T("server.err_method_not_allowed"), http.StatusMethodNotAllowed)
		return
	}

	tokenInfo := s.state.GetTokenInfo()
	accountInfo := s.state.GetAccountInfo()

	resp := StatusResponse{
		Status:     "ready",
		HasDUID:    s.state.GetDUID() != "",
		HasToken:   tokenInfo.HasAccessToken && !tokenInfo.IsExpired,
		HasAccount: accountInfo != nil && accountInfo.AccountID != "",
	}

	writeJSON(w, resp)
}

type DUIDResponse struct {
	DUID string `json:"duid"`
}

func (s *Server) handleDUID(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, i18n.T("server.err_method_not_allowed"), http.StatusMethodNotAllowed)
		return
	}

	duid := s.state.GetDUID()
	if duid == "" {
		http.Error(w, i18n.T("server.err_duid_not_configured"), http.StatusNotFound)
		return
	}

	writeJSON(w, DUIDResponse{DUID: duid})
}

type TokenResponse struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int    `json:"expires_in"`
	ExpiresAt    int64  `json:"expires_at"`
	IsExpired    bool   `json:"is_expired"`

	MobileAccessToken  string `json:"psn_mobile_sso_access_token,omitempty"`
	MobileRefreshToken string `json:"psn_mobile_sso_refresh_token,omitempty"`
	MobileExpiresAt    int64  `json:"psn_mobile_sso_expires_at,omitempty"`
}

func (s *Server) handleToken(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, i18n.T("server.err_method_not_allowed"), http.StatusMethodNotAllowed)
		return
	}

	tokenInfo := s.state.GetTokenInfo()
	if !tokenInfo.HasAccessToken {
		http.Error(w, i18n.T("server.err_no_token"), http.StatusNotFound)
		return
	}

	response := TokenResponse{
		AccessToken:  tokenInfo.AccessToken,
		RefreshToken: tokenInfo.RefreshToken,
		ExpiresIn:    tokenInfo.ExpiresIn,
		ExpiresAt:    tokenInfo.ExpiresAt,
		IsExpired:    tokenInfo.IsExpired,
	}

	if mobile := s.state.GetMobileTokens(); mobile != nil {
		response.MobileAccessToken = mobile.AccessToken
		response.MobileRefreshToken = mobile.RefreshToken
		if mobile.ObtainedAt > 0 && mobile.ExpiresIn > 0 {
			response.MobileExpiresAt = mobile.ObtainedAt + int64(mobile.ExpiresIn)
		}
	}

	writeJSON(w, response)
}

type AccountResponse struct {
	AccountID string `json:"account_id"`
	OnlineID  string `json:"online_id"`
}

func (s *Server) handleAccount(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, i18n.T("server.err_method_not_allowed"), http.StatusMethodNotAllowed)
		return
	}

	account := s.state.GetAccountInfo()
	if account == nil || account.AccountID == "" {
		http.Error(w, i18n.T("server.err_no_account"), http.StatusNotFound)
		return
	}

	writeJSON(w, AccountResponse{
		AccountID: account.AccountID,
		OnlineID:  account.OnlineID,
	})
}

func writeJSON(w http.ResponseWriter, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(data)
}
