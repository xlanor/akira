package psn

import (
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
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

func newCID() string {
	b := make([]byte, 16)
	rand.Read(b)
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	h := hex.EncodeToString(b)
	return h[0:8] + "-" + h[8:12] + "-" + h[12:16] + "-" + h[16:20] + "-" + h[20:32]
}

func codeFromLocation(location string) string {
	if location == "" {
		return ""
	}
	if i := strings.IndexByte(location, '?'); i >= 0 {
		if q, err := url.ParseQuery(location[i+1:]); err == nil {
			if code := q.Get("code"); code != "" {
				return code
			}
		}
	}
	if parsed, err := url.Parse(location); err == nil {
		return parsed.Query().Get("code")
	}
	return ""
}

func GetAuthCodeFromNpsso(npsso, duid string) (string, error) {
	npsso = strings.TrimSpace(npsso)
	if npsso == "" {
		return "", fmt.Errorf("npsso token is empty")
	}

	authURL := BuildNpssoAuthorizeURL(duid, newCID())

	req, err := http.NewRequest("GET", authURL, nil)
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
	if code := codeFromLocation(resp.Request.URL.String()); code != "" {
		return code, nil
	}

	body, _ := io.ReadAll(resp.Body)
	return "", fmt.Errorf("no authorization code returned (status %d); the npsso token may be invalid or expired: %s", resp.StatusCode, string(body))
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
	data.Set("client_id", ClientID)
	data.Set("client_secret", ClientSecret)
	data.Set("redirect_uri", RedirectURI)
	data.Set("scope", Scopes)

	req, err := http.NewRequest("POST", NpssoTokenURL, strings.NewReader(data.Encode()))
	if err != nil {
		return nil, err
	}

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
		AccountID: accountIDBase64,
		OnlineID:  accountResp.OnlineID,
		RawUserID: accountResp.UserID,
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
