package psn

import (
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"

	"akira-companion/internal/state"

	qrcode "github.com/skip2/go-qrcode"
)

func getBasicAuthHeader() string {
	credentials := ClientID + ":" + ClientSecret
	encoded := base64.StdEncoding.EncodeToString([]byte(credentials))
	return "Basic " + encoded
}

func ExtractCodeFromRedirect(redirectURL string) (string, error) {
	parsed, err := url.Parse(redirectURL)
	if err != nil {
		return "", fmt.Errorf("invalid URL: %w", err)
	}

	code := parsed.Query().Get("code")
	if code == "" {
		return "", fmt.Errorf("no 'code' parameter found in redirect URL")
	}

	return code, nil
}

type TokenResponse struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int    `json:"expires_in"`
	TokenType    string `json:"token_type"`
}

func ExchangeCodeForTokens(code string) (*state.Tokens, error) {
	data := url.Values{}
	data.Set("grant_type", "authorization_code")
	data.Set("code", code)
	data.Set("scope", Scopes)
	data.Set("redirect_uri", RedirectURI)

	req, err := http.NewRequest("POST", TokenURL, strings.NewReader(data.Encode()))
	if err != nil {
		return nil, err
	}

	req.Header.Set("Authorization", getBasicAuthHeader())
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("token exchange failed: %d - %s", resp.StatusCode, string(body))
	}

	var tokenResp TokenResponse
	if err := json.NewDecoder(resp.Body).Decode(&tokenResp); err != nil {
		return nil, err
	}

	return &state.Tokens{
		AccessToken:  tokenResp.AccessToken,
		RefreshToken: tokenResp.RefreshToken,
		ExpiresIn:    tokenResp.ExpiresIn,
	}, nil
}

func RefreshTokens(refreshToken string) (*state.Tokens, error) {
	data := url.Values{}
	data.Set("grant_type", "refresh_token")
	data.Set("refresh_token", refreshToken)
	data.Set("scope", Scopes)
	data.Set("redirect_uri", RedirectURI)

	req, err := http.NewRequest("POST", TokenURL, strings.NewReader(data.Encode()))
	if err != nil {
		return nil, err
	}

	req.Header.Set("Authorization", getBasicAuthHeader())
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("token refresh failed: %d - %s", resp.StatusCode, string(body))
	}

	var tokenResp TokenResponse
	if err := json.NewDecoder(resp.Body).Decode(&tokenResp); err != nil {
		return nil, err
	}

	return &state.Tokens{
		AccessToken:  tokenResp.AccessToken,
		RefreshToken: tokenResp.RefreshToken,
		ExpiresIn:    tokenResp.ExpiresIn,
	}, nil
}

type AccountInfoResponse struct {
	UserID   string `json:"user_id"`
	OnlineID string `json:"online_id"`
}

func GetAccountInfo(accessToken string) (*state.AccountInfo, error) {
	apiURL := TokenURL + "/" + accessToken

	req, err := http.NewRequest("GET", apiURL, nil)
	if err != nil {
		return nil, err
	}

	req.Header.Set("Authorization", getBasicAuthHeader())
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("account info request failed: %d - %s", resp.StatusCode, string(body))
	}

	var accountResp AccountInfoResponse
	if err := json.NewDecoder(resp.Body).Decode(&accountResp); err != nil {
		return nil, err
	}

	accountIDBase64 := userIDToBase64(accountResp.UserID)

	return &state.AccountInfo{
		AccountID:  accountIDBase64,
		OnlineID:   accountResp.OnlineID,
		RawUserID:  accountResp.UserID,
	}, nil
}

func userIDToBase64(userID string) string {
	var userIDInt uint64
	fmt.Sscanf(userID, "%d", &userIDInt)

	buf := make([]byte, 8)
	binary.LittleEndian.PutUint64(buf, userIDInt)

	return base64.StdEncoding.EncodeToString(buf)
}

func GenerateQRCode(content string) ([]byte, error) {
	png, err := qrcode.Encode(content, qrcode.Medium, 256)
	if err != nil {
		return nil, err
	}
	return png, nil
}
