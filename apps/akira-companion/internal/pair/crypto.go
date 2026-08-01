package pair

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/binary"
	"errors"
	"io"

	"golang.org/x/crypto/hkdf"
)

const (
	protoMagic   = "AKPR"
	protoVersion = 1
	hkdfInfo     = "akira-pair-v1"

	pubKeyLen    = 64
	saltNonceLen = 16
	ivLen        = 16
	macLen       = 32
	keyLen       = 32

	helloLen  = 5 + pubKeyLen + saltNonceLen
	sealedMin = 5 + pubKeyLen + ivLen + 4 + macLen
)

var (
	ErrBadMagic    = errors.New("pair: bad magic")
	ErrBadVersion  = errors.New("pair: unsupported protocol version")
	ErrShortHello  = errors.New("pair: malformed hello message")
	ErrShortSealed = errors.New("pair: malformed sealed message")
	ErrBadPubKey   = errors.New("pair: invalid public key")
	ErrBadMAC      = errors.New("pair: authentication failed (wrong code or tampered payload)")
)

type Hello struct {
	SwitchPub []byte
	SaltNonce []byte
}

type Sealed struct {
	DesktopPub []byte
	IV         []byte
	Ciphertext []byte
	MAC        []byte
}

func (h Hello) Encode() []byte {
	buf := make([]byte, 0, helloLen)
	buf = append(buf, protoMagic...)
	buf = append(buf, protoVersion)
	buf = append(buf, h.SwitchPub...)
	buf = append(buf, h.SaltNonce...)
	return buf
}

func DecodeHello(b []byte) (Hello, error) {
	if len(b) != helloLen {
		return Hello{}, ErrShortHello
	}
	if string(b[:4]) != protoMagic {
		return Hello{}, ErrBadMagic
	}
	if b[4] != protoVersion {
		return Hello{}, ErrBadVersion
	}
	off := 5
	return Hello{
		SwitchPub: append([]byte(nil), b[off:off+pubKeyLen]...),
		SaltNonce: append([]byte(nil), b[off+pubKeyLen:off+pubKeyLen+saltNonceLen]...),
	}, nil
}

func (s Sealed) encodePrefix() []byte {
	buf := make([]byte, 0, sealedMin-macLen+len(s.Ciphertext))
	buf = append(buf, protoMagic...)
	buf = append(buf, protoVersion)
	buf = append(buf, s.DesktopPub...)
	buf = append(buf, s.IV...)
	var lenb [4]byte
	binary.BigEndian.PutUint32(lenb[:], uint32(len(s.Ciphertext)))
	buf = append(buf, lenb[:]...)
	buf = append(buf, s.Ciphertext...)
	return buf
}

func (s Sealed) Encode() []byte {
	return append(s.encodePrefix(), s.MAC...)
}

func DecodeSealed(b []byte) (Sealed, error) {
	if len(b) < sealedMin {
		return Sealed{}, ErrShortSealed
	}
	if string(b[:4]) != protoMagic {
		return Sealed{}, ErrBadMagic
	}
	if b[4] != protoVersion {
		return Sealed{}, ErrBadVersion
	}
	off := 5
	desktopPub := b[off : off+pubKeyLen]
	off += pubKeyLen
	iv := b[off : off+ivLen]
	off += ivLen
	ctLen := int(binary.BigEndian.Uint32(b[off : off+4]))
	off += 4
	if len(b) != off+ctLen+macLen {
		return Sealed{}, ErrShortSealed
	}
	ct := b[off : off+ctLen]
	off += ctLen
	return Sealed{
		DesktopPub: append([]byte(nil), desktopPub...),
		IV:         append([]byte(nil), iv...),
		Ciphertext: append([]byte(nil), ct...),
		MAC:        append([]byte(nil), b[off:off+macLen]...),
	}, nil
}

func marshalPub(pub *ecdh.PublicKey) []byte {
	return pub.Bytes()[1:]
}

func parsePub(raw []byte) (*ecdh.PublicKey, error) {
	if len(raw) != pubKeyLen {
		return nil, ErrBadPubKey
	}
	sec1 := make([]byte, 1+pubKeyLen)
	sec1[0] = 4
	copy(sec1[1:], raw)
	pub, err := ecdh.P256().NewPublicKey(sec1)
	if err != nil {
		return nil, ErrBadPubKey
	}
	return pub, nil
}

func deriveKeys(shared, saltNonce []byte, code string) (keyEnc, keyMac []byte, err error) {
	salt := make([]byte, 0, len(saltNonce)+len(code))
	salt = append(salt, saltNonce...)
	salt = append(salt, []byte(code)...)
	r := hkdf.New(sha256.New, shared, salt, []byte(hkdfInfo))
	okm := make([]byte, 2*keyLen)
	if _, err := io.ReadFull(r, okm); err != nil {
		return nil, nil, err
	}
	return okm[:keyLen], okm[keyLen:], nil
}

func GenerateHello() (Hello, *ecdh.PrivateKey, error) {
	priv, err := ecdh.P256().GenerateKey(rand.Reader)
	if err != nil {
		return Hello{}, nil, err
	}
	saltNonce := make([]byte, saltNonceLen)
	if _, err := rand.Read(saltNonce); err != nil {
		return Hello{}, nil, err
	}
	return Hello{SwitchPub: marshalPub(priv.PublicKey()), SaltNonce: saltNonce}, priv, nil
}

func Seal(hello Hello, code string, payload []byte) (Sealed, error) {
	switchPub, err := parsePub(hello.SwitchPub)
	if err != nil {
		return Sealed{}, err
	}
	priv, err := ecdh.P256().GenerateKey(rand.Reader)
	if err != nil {
		return Sealed{}, err
	}
	shared, err := priv.ECDH(switchPub)
	if err != nil {
		return Sealed{}, err
	}
	keyEnc, keyMac, err := deriveKeys(shared, hello.SaltNonce, code)
	if err != nil {
		return Sealed{}, err
	}
	iv := make([]byte, ivLen)
	if _, err := rand.Read(iv); err != nil {
		return Sealed{}, err
	}
	block, err := aes.NewCipher(keyEnc)
	if err != nil {
		return Sealed{}, err
	}
	ct := make([]byte, len(payload))
	cipher.NewCTR(block, iv).XORKeyStream(ct, payload)
	s := Sealed{DesktopPub: marshalPub(priv.PublicKey()), IV: iv, Ciphertext: ct}
	mac := hmac.New(sha256.New, keyMac)
	mac.Write(s.encodePrefix())
	s.MAC = mac.Sum(nil)
	return s, nil
}

func Open(hello Hello, switchPriv *ecdh.PrivateKey, sealed Sealed, code string) ([]byte, error) {
	desktopPub, err := parsePub(sealed.DesktopPub)
	if err != nil {
		return nil, err
	}
	shared, err := switchPriv.ECDH(desktopPub)
	if err != nil {
		return nil, err
	}
	keyEnc, keyMac, err := deriveKeys(shared, hello.SaltNonce, code)
	if err != nil {
		return nil, err
	}
	mac := hmac.New(sha256.New, keyMac)
	mac.Write(sealed.encodePrefix())
	if subtle.ConstantTimeCompare(mac.Sum(nil), sealed.MAC) != 1 {
		return nil, ErrBadMAC
	}
	block, err := aes.NewCipher(keyEnc)
	if err != nil {
		return nil, err
	}
	pt := make([]byte, len(sealed.Ciphertext))
	cipher.NewCTR(block, sealed.IV).XORKeyStream(pt, sealed.Ciphertext)
	return pt, nil
}
