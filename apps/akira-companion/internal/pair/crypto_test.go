package pair

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"testing"
)

func mustPriv(t *testing.T, b byte) *ecdh.PrivateKey {
	t.Helper()
	scalar := bytes.Repeat([]byte{b}, keyLen)
	priv, err := ecdh.P256().NewPrivateKey(scalar)
	if err != nil {
		t.Fatalf("NewPrivateKey: %v", err)
	}
	return priv
}

func TestRoundTrip(t *testing.T) {
	hello, switchPriv, err := GenerateHello()
	if err != nil {
		t.Fatalf("GenerateHello: %v", err)
	}
	payload := []byte(`{"duid":"0000000700410080deadbeefdeadbeefdeadbeefdeadbeef","online_id":"tester"}`)

	sealed, err := Seal(hello, "4729", payload)
	if err != nil {
		t.Fatalf("Seal: %v", err)
	}

	got, err := Open(hello, switchPriv, sealed, "4729")
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("payload mismatch: got %q want %q", got, payload)
	}
}

func TestWrongCodeFails(t *testing.T) {
	hello, switchPriv, err := GenerateHello()
	if err != nil {
		t.Fatalf("GenerateHello: %v", err)
	}
	sealed, err := Seal(hello, "4729", []byte("secret"))
	if err != nil {
		t.Fatalf("Seal: %v", err)
	}
	if _, err := Open(hello, switchPriv, sealed, "0000"); err != ErrBadMAC {
		t.Fatalf("expected ErrBadMAC for wrong code, got %v", err)
	}
}

func TestTamperFails(t *testing.T) {
	hello, switchPriv, err := GenerateHello()
	if err != nil {
		t.Fatalf("GenerateHello: %v", err)
	}
	sealed, err := Seal(hello, "4729", []byte("secret payload"))
	if err != nil {
		t.Fatalf("Seal: %v", err)
	}
	sealed.Ciphertext[0] ^= 0x01
	if _, err := Open(hello, switchPriv, sealed, "4729"); err != ErrBadMAC {
		t.Fatalf("expected ErrBadMAC for tampered ciphertext, got %v", err)
	}
}

func TestWireRoundTrip(t *testing.T) {
	hello, switchPriv, err := GenerateHello()
	if err != nil {
		t.Fatalf("GenerateHello: %v", err)
	}
	decHello, err := DecodeHello(hello.Encode())
	if err != nil {
		t.Fatalf("DecodeHello: %v", err)
	}
	sealed, err := Seal(decHello, "1234", []byte("over the wire"))
	if err != nil {
		t.Fatalf("Seal: %v", err)
	}
	decSealed, err := DecodeSealed(sealed.Encode())
	if err != nil {
		t.Fatalf("DecodeSealed: %v", err)
	}
	got, err := Open(hello, switchPriv, decSealed, "1234")
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	if !bytes.Equal(got, []byte("over the wire")) {
		t.Fatalf("payload mismatch: %q", got)
	}
}

func TestKAT(t *testing.T) {
	switchPriv := mustPriv(t, 0x11)
	desktopPriv := mustPriv(t, 0x22)
	saltNonce := bytes.Repeat([]byte{0x33}, saltNonceLen)
	iv := bytes.Repeat([]byte{0x44}, ivLen)
	code := "4729"
	payload := []byte(`{"duid":"0000000700410080deadbeefdeadbeefdeadbeefdeadbeef","hello":"akira"}`)

	shared, err := desktopPriv.ECDH(switchPriv.PublicKey())
	if err != nil {
		t.Fatalf("ECDH: %v", err)
	}
	shared2, err := switchPriv.ECDH(desktopPriv.PublicKey())
	if err != nil {
		t.Fatalf("ECDH reverse: %v", err)
	}
	if !bytes.Equal(shared, shared2) {
		t.Fatalf("ECDH not symmetric")
	}

	keyEnc, keyMac, err := deriveKeys(shared, saltNonce, code)
	if err != nil {
		t.Fatalf("deriveKeys: %v", err)
	}

	block, err := aes.NewCipher(keyEnc)
	if err != nil {
		t.Fatalf("aes: %v", err)
	}
	ct := make([]byte, len(payload))
	cipher.NewCTR(block, iv).XORKeyStream(ct, payload)

	sealed := Sealed{DesktopPub: marshalPub(desktopPriv.PublicKey()), IV: iv, Ciphertext: ct}
	mac := hmac.New(sha256.New, keyMac)
	mac.Write(sealed.encodePrefix())
	sealed.MAC = mac.Sum(nil)

	t.Logf("KAT switch_pub  = %s", hex.EncodeToString(marshalPub(switchPriv.PublicKey())))
	t.Logf("KAT desktop_pub = %s", hex.EncodeToString(marshalPub(desktopPriv.PublicKey())))
	t.Logf("KAT shared_z    = %s", hex.EncodeToString(shared))
	t.Logf("KAT key_enc     = %s", hex.EncodeToString(keyEnc))
	t.Logf("KAT key_mac     = %s", hex.EncodeToString(keyMac))
	t.Logf("KAT ciphertext  = %s", hex.EncodeToString(ct))
	t.Logf("KAT mac         = %s", hex.EncodeToString(sealed.MAC))

	frozen := []struct{ name, got, want string }{
		{"switch_pub", hex.EncodeToString(marshalPub(switchPriv.PublicKey())), "0217e617f0b6443928278f96999e69a23a4f2c152bdf6d6cdf66e5b80282d4ed194a7debcb97712d2dda3ca85aa8765a56f45fc758599652f2897c65306e5794"},
		{"desktop_pub", hex.EncodeToString(marshalPub(desktopPriv.PublicKey())), "d65a93977caa3d1b081852ff57a79e465f1660577304baead505dd3a48589cf350185e895372df6221ea3a137557e473fddb6755f05bd507c3c533fce9c91285"},
		{"shared_z", hex.EncodeToString(shared), "ccfc261f58193c98ca4ad4a53bbac6f0ee29bc4d48438090446908622ca79af6"},
		{"key_enc", hex.EncodeToString(keyEnc), "36880b0f7643f30e922a3a7863104225e3f3ef5906959ec080153c9f230ed8a7"},
		{"key_mac", hex.EncodeToString(keyMac), "22855abe398d7b565d85ef55bc9f24219cd27a3cb3a08233052676f5b0e6913e"},
		{"ciphertext", hex.EncodeToString(ct), "8a282fbdd682557a80e1c5fe349db4cbaf421ade29717f0157c0803726a50dbc4d67edd12f8b53b285eaeecf0d42386cd6bfae8c847b274541c5cbf94b4be4f7c6d983ed7c417dc5f4f91b"},
		{"mac", hex.EncodeToString(sealed.MAC), "ba5f8878332de6941f9796222c83e4c716a12f114604443b3310e097cb7a1310"},
	}
	for _, f := range frozen {
		if f.got != f.want {
			t.Errorf("KAT %s drift:\n got  %s\n want %s", f.name, f.got, f.want)
		}
	}

	hello := Hello{SwitchPub: marshalPub(switchPriv.PublicKey()), SaltNonce: saltNonce}
	got, err := Open(hello, switchPriv, sealed, code)
	if err != nil {
		t.Fatalf("Open KAT: %v", err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("KAT payload mismatch")
	}
}
