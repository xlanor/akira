package psn

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"

	"akira-companion/internal/state"
)

func mobileBasicAuthHeader() string {
	credentials := MobileClientID + ":" + MobileClientSecret
	return "Basic " + base64.StdEncoding.EncodeToString([]byte(credentials))
}

func GetMobileAuthCodeFromNpsso(npsso string) (string, error) {
	npsso = strings.TrimSpace(npsso)
	if npsso == "" {
		return "", fmt.Errorf("npsso token is empty")
	}

	req, err := http.NewRequest("GET", BuildMobileNpssoAuthorizeURL(), nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
	req.Header.Set("Cookie", "npsso="+npsso)

	client := &http.Client{
		CheckRedirect: func(*http.Request, []*http.Request) error {
			return http.ErrUseLastResponse
		},
	}

	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	if code := codeFromLocation(resp.Header.Get("Location")); code != "" {
		return code, nil
	}

	body, _ := io.ReadAll(resp.Body)
	return "", fmt.Errorf("no mobile authorization code returned (status %d): %s", resp.StatusCode, string(body))
}

func mobileTokenRequest(data url.Values) (*state.Tokens, error) {
	data.Set("token_format", "jwt")

	req, err := http.NewRequest("POST", NpssoTokenURL, strings.NewReader(data.Encode()))
	if err != nil {
		return nil, err
	}

	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("Authorization", mobileBasicAuthHeader())

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("mobile token exchange failed: %d - %s", resp.StatusCode, string(body))
	}

	var tokenResp TokenResponse
	if err := json.NewDecoder(resp.Body).Decode(&tokenResp); err != nil {
		return nil, err
	}

	if tokenResp.AccessToken == "" || tokenResp.RefreshToken == "" {
		return nil, fmt.Errorf("mobile token response was missing tokens")
	}

	return &state.Tokens{
		AccessToken:  tokenResp.AccessToken,
		RefreshToken: tokenResp.RefreshToken,
		ExpiresIn:    tokenResp.ExpiresIn,
	}, nil
}

func ExchangeCodeForMobileTokens(code string) (*state.Tokens, error) {
	data := url.Values{}
	data.Set("grant_type", "authorization_code")
	data.Set("code", code)
	data.Set("redirect_uri", MobileRedirectURI)

	return mobileTokenRequest(data)
}

func RefreshMobileTokens(refreshToken string) (*state.Tokens, error) {
	data := url.Values{}
	data.Set("grant_type", "refresh_token")
	data.Set("refresh_token", refreshToken)
	data.Set("scope", MobileScopes)

	return mobileTokenRequest(data)
}
