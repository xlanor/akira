package pair

import (
	"encoding/json"
	"testing"

	"akira-companion/internal/state"
)

func TestBuildPayloadIncludesNpssoWhenAvailable(t *testing.T) {
	s := &state.AppState{}
	s.SetDUID("000000070041008000112233445566778899aabbccddeeff")
	s.SetNpsso("secret-npsso")
	s.SetTokens(&state.Tokens{
		AccessToken:  "access",
		RefreshToken: "refresh",
		ExpiresIn:    3600,
		ObtainedAt:   123,
	})

	raw, err := BuildPayload(s)
	if err != nil {
		t.Fatalf("BuildPayload: %v", err)
	}

	var payload Payload
	if err := json.Unmarshal(raw, &payload); err != nil {
		t.Fatalf("Unmarshal: %v", err)
	}

	if payload.NPSSO != "secret-npsso" {
		t.Fatalf("NPSSO = %q, want %q", payload.NPSSO, "secret-npsso")
	}
	if payload.AccessToken != "access" || payload.RefreshToken != "refresh" {
		t.Fatalf("token fields missing: %+v", payload)
	}
}
