package state

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSaveDoesNotPersistSecrets(t *testing.T) {
	s := &AppState{filePath: filepath.Join(t.TempDir(), "state.json")}
	s.SetDUID("000000070041008000112233445566778899aabbccddeeff")
	s.SetNpsso("secret-npsso")
	s.SetTokens(&Tokens{AccessToken: "secret-access", RefreshToken: "secret-refresh", ExpiresIn: 3600})
	s.SetMobileTokens(&Tokens{AccessToken: "secret-mobile"})
	s.SetAccountInfo(&AccountInfo{AccountID: "secret-acct", OnlineID: "user"})

	if err := s.Save(); err != nil {
		t.Fatalf("Save: %v", err)
	}

	raw, err := os.ReadFile(s.filePath)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	for _, secret := range []string{"secret-npsso", "secret-access", "secret-refresh", "secret-mobile", "secret-acct"} {
		if strings.Contains(string(raw), secret) {
			t.Fatalf("state.json leaked secret %q:\n%s", secret, raw)
		}
	}
	if !strings.Contains(string(raw), "000000070041008000112233445566778899aabbccddeeff") {
		t.Fatalf("state.json missing DUID:\n%s", raw)
	}
}

func TestLoadDropsLegacySecrets(t *testing.T) {
	path := filepath.Join(t.TempDir(), "state.json")
	legacy := `{"duid":"000000070041008000112233445566778899aabbccddeeff","npsso":"old","tokens":{"access_token":"old-access"},"psn_mobile_sso":{"access_token":"old-mobile"},"account":{"account_id":"a","online_id":"o"}}`
	if err := os.WriteFile(path, []byte(legacy), 0600); err != nil {
		t.Fatal(err)
	}

	s := &AppState{filePath: path}
	if err := s.Load(); err != nil {
		t.Fatalf("Load: %v", err)
	}

	if s.GetDUID() == "" {
		t.Fatal("DUID should still load")
	}
	if s.GetNpsso() != "" || s.GetAccessToken() != "" || s.GetMobileTokens() != nil || s.GetAccountInfo() != nil {
		t.Fatal("secrets must not load from disk")
	}
}
