package server

import (
	"encoding/json"
	"net/http"
)

type StatusResponse struct {
	Status     string `json:"status"`
	HasDUID    bool   `json:"has_duid"`
	HasToken   bool   `json:"has_token"`
	HasAccount bool   `json:"has_account"`
}

func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
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
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	duid := s.state.GetDUID()
	if duid == "" {
		http.Error(w, "DUID not configured", http.StatusNotFound)
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
}

func (s *Server) handleToken(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	tokenInfo := s.state.GetTokenInfo()
	if !tokenInfo.HasAccessToken {
		http.Error(w, "No access token available", http.StatusNotFound)
		return
	}

	writeJSON(w, TokenResponse{
		AccessToken:  tokenInfo.AccessToken,
		RefreshToken: tokenInfo.RefreshToken,
		ExpiresIn:    tokenInfo.ExpiresIn,
		ExpiresAt:    tokenInfo.ExpiresAt,
		IsExpired:    tokenInfo.IsExpired,
	})
}

type AccountResponse struct {
	AccountID string `json:"account_id"`
	OnlineID  string `json:"online_id"`
}

func (s *Server) handleAccount(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	account := s.state.GetAccountInfo()
	if account == nil || account.AccountID == "" {
		http.Error(w, "Account information not available", http.StatusNotFound)
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
