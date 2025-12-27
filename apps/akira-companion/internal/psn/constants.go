package psn

import "net/url"

const (
	DUIDPrefix = "0000000700410080"

	ClientID     = "ba495a24-818c-472b-b12d-ff231c1b5745"
	ClientSecret = "mvaiZkRsAsI1IBkY"

	TokenURL = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token"

	RedirectURI = "https://remoteplay.dl.playstation.net/remoteplay/redirect"

	Scopes = "psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update"
)

func BuildLoginURL(duid string) string {
	params := url.Values{}
	params.Set("service_entity", "urn:service-entity:psn")
	params.Set("response_type", "code")
	params.Set("client_id", ClientID)
	params.Set("redirect_uri", RedirectURI)
	params.Set("scope", Scopes)
	params.Set("request_locale", "en_US")
	params.Set("ui", "pr")
	params.Set("service_logo", "ps")
	params.Set("layout_type", "popup")
	params.Set("smcid", "remoteplay")
	params.Set("prompt", "always")
	params.Set("PlatformPrivacyWs1", "minimal")
	params.Set("duid", duid)

	return "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?" + params.Encode()
}
