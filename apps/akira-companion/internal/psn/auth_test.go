package psn

import (
	"net/http"
	"net/http/httptest"
	"regexp"
	"testing"
)

func TestUserIDToBase64(t *testing.T) {
	cases := map[string]string{
		"1234567890123456789":  "FYHpffQQIhE=",
		"0":                    "AAAAAAAAAAA=",
		"1":                    "AQAAAAAAAAA=",
		"18446744073709551615": "//////////8=",
	}
	for userID, want := range cases {
		if got := userIDToBase64(userID); got != want {
			t.Errorf("userIDToBase64(%q) = %q, want %q", userID, got, want)
		}
	}
}

func TestCodeFromLocation(t *testing.T) {
	cases := []struct {
		name     string
		location string
		want     string
	}{
		{"redirect with code", RedirectURI + "?code=abc123&state=x", "abc123"},
		{"code only", RedirectURI + "?code=abc123", "abc123"},
		{"no code", RedirectURI + "?error=access_denied", ""},
		{"no query", RedirectURI, ""},
		{"empty", "", ""},
		{"scheme-relative style", "com.playstation.remoteplay://redirect?code=z9", "z9"},
	}
	for _, c := range cases {
		if got := codeFromLocation(c.location); got != c.want {
			t.Errorf("%s: codeFromLocation(%q) = %q, want %q", c.name, c.location, got, c.want)
		}
	}
}

func TestNewCID(t *testing.T) {
	re := regexp.MustCompile(`^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$`)
	seen := map[string]bool{}
	for i := 0; i < 100; i++ {
		cid := newCID()
		if !re.MatchString(cid) {
			t.Fatalf("newCID() = %q, not a valid UUIDv4", cid)
		}
		if seen[cid] {
			t.Fatalf("newCID() produced duplicate %q", cid)
		}
		seen[cid] = true
	}
}

func TestGetAuthCodeFromNpssoEmpty(t *testing.T) {
	if _, err := GetAuthCodeFromNpsso("   ", "duid"); err == nil {
		t.Fatal("expected error for empty npsso, got nil")
	}
}

func TestGetAuthCodeFromNpssoCapturesCode(t *testing.T) {
	var gotCookie string
	var redirectFollowed bool

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/redirect" {
			redirectFollowed = true
			w.WriteHeader(http.StatusOK)
			return
		}
		gotCookie = r.Header.Get("Cookie")
		http.Redirect(w, r, "/redirect?code=THECODE&state=xyz", http.StatusFound)
	}))
	defer srv.Close()

	orig := npssoAuthorizeURL
	npssoAuthorizeURL = srv.URL + "/authorize"
	defer func() { npssoAuthorizeURL = orig }()

	code, err := GetAuthCodeFromNpsso("mynpsso", "0000000700410080deadbeefdeadbeefdeadbeefdeadbeef")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if code != "THECODE" {
		t.Errorf("code = %q, want %q", code, "THECODE")
	}
	if gotCookie != "npsso=mynpsso" {
		t.Errorf("Cookie header = %q, want %q", gotCookie, "npsso=mynpsso")
	}
	if redirectFollowed {
		t.Error("redirect was followed; it should be captured from the Location header instead")
	}
}
