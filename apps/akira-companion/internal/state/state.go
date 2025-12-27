package state

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"
)

type Tokens struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int    `json:"expires_in"`
	ObtainedAt   int64  `json:"obtained_at"`
}

type TokenInfo struct {
	HasAccessToken  bool   `json:"has_access_token"`
	HasRefreshToken bool   `json:"has_refresh_token"`
	AccessToken     string `json:"access_token,omitempty"`
	RefreshToken    string `json:"refresh_token,omitempty"`
	ExpiresIn       int    `json:"expires_in,omitempty"`
	ExpiresAt       int64  `json:"expires_at,omitempty"`
	IsExpired       bool   `json:"is_expired"`
}

type AccountInfo struct {
	AccountID string `json:"account_id"`
	OnlineID  string `json:"online_id"`
	RawUserID string `json:"raw_user_id"`
}

type AppState struct {
	mu sync.RWMutex

	DUID     string       `json:"duid,omitempty"`
	Seed     string       `json:"seed,omitempty"`
	Tokens   *Tokens      `json:"tokens,omitempty"`
	Account  *AccountInfo `json:"account,omitempty"`
	filePath string       `json:"-"`
}

func NewAppState() *AppState {
	configDir, err := os.UserConfigDir()
	if err != nil {
		configDir = "."
	}

	appDir := filepath.Join(configDir, "akira-companion")
	os.MkdirAll(appDir, 0755)

	return &AppState{
		filePath: filepath.Join(appDir, "state.json"),
	}
}

func (s *AppState) Load() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	data, err := os.ReadFile(s.filePath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}

	return json.Unmarshal(data, s)
}

func (s *AppState) Save() error {
	s.mu.RLock()
	defer s.mu.RUnlock()

	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(s.filePath, data, 0600)
}

func (s *AppState) Clear() {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.DUID = ""
	s.Seed = ""
	s.Tokens = nil
	s.Account = nil
}

func (s *AppState) SetDUID(duid string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.DUID = duid
}

func (s *AppState) GetDUID() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.DUID
}

func (s *AppState) SetSeed(seed string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Seed = seed
}

func (s *AppState) GetSeed() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.Seed
}

func (s *AppState) SetTokens(tokens *Tokens) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tokens.ObtainedAt = time.Now().Unix()
	s.Tokens = tokens
}

func (s *AppState) GetAccessToken() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.Tokens == nil {
		return ""
	}
	return s.Tokens.AccessToken
}

func (s *AppState) GetRefreshToken() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.Tokens == nil {
		return ""
	}
	return s.Tokens.RefreshToken
}

func (s *AppState) GetTokenInfo() *TokenInfo {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if s.Tokens == nil {
		return &TokenInfo{
			HasAccessToken:  false,
			HasRefreshToken: false,
			IsExpired:       true,
		}
	}

	expiresAt := s.Tokens.ObtainedAt + int64(s.Tokens.ExpiresIn)
	isExpired := time.Now().Unix() > expiresAt

	return &TokenInfo{
		HasAccessToken:  s.Tokens.AccessToken != "",
		HasRefreshToken: s.Tokens.RefreshToken != "",
		AccessToken:     s.Tokens.AccessToken,
		RefreshToken:    s.Tokens.RefreshToken,
		ExpiresIn:       s.Tokens.ExpiresIn,
		ExpiresAt:       expiresAt,
		IsExpired:       isExpired,
	}
}

func (s *AppState) SetAccountInfo(account *AccountInfo) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Account = account
}

func (s *AppState) GetAccountInfo() *AccountInfo {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.Account
}
