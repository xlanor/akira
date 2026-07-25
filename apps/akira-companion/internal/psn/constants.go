package psn

import "net/url"

const (
	DUIDPrefix = "0000000700410080"

	ClientID     = "ba495a24-818c-472b-b12d-ff231c1b5745"
	ClientSecret = "mvaiZkRsAsI1IBkY"

	TokenURL = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token"

	NpssoAuthorizeURL = "https://ca.account.sony.com/api/authz/v3/oauth/authorize"
	NpssoTokenURL     = "https://ca.account.sony.com/api/authz/v3/oauth/token"

	SSOCookieURL = "https://ca.account.sony.com/api/v1/ssocookie"

	RedirectURI = "https://remoteplay.dl.playstation.net/remoteplay/redirect"

	Scopes = "psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update"
)

var npssoAuthorizeURL = NpssoAuthorizeURL

func BuildNpssoAuthorizeURL(duid, cid string) string {
	params := url.Values{}
	params.Set("client_id", ClientID)
	params.Set("redirect_uri", RedirectURI)
	params.Set("scope", Scopes)
	params.Set("response_type", "code")
	params.Set("service_entity", "urn:service-entity:psn")
	params.Set("access_type", "offline")
	params.Set("duid", duid)
	params.Set("smcid", "remoteplay")
	params.Set("layout_type", "popup")
	params.Set("PlatformPrivacyWs1", "minimal")
	params.Set("no_captcha", "true")
	params.Set("cid", cid)

	return npssoAuthorizeURL + "?" + params.Encode()
}
