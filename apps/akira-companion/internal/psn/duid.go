package psn

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"regexp"
)

func GenerateRandomDUID() string {
	bytes := make([]byte, 16)
	rand.Read(bytes)
	return DUIDPrefix + hex.EncodeToString(bytes)
}

func GenerateDUIDFromSeed(seed string) string {
	hash := sha256.Sum256([]byte(seed))
	suffix := hash[:16]
	return DUIDPrefix + hex.EncodeToString(suffix)
}

func GenerateDUIDFromBytes(raw []byte) (string, error) {
	if len(raw) != 16 {
		return "", fmt.Errorf("raw bytes must be exactly 16 bytes, got %d", len(raw))
	}
	return DUIDPrefix + hex.EncodeToString(raw), nil
}

func ValidateDUID(duid string) error {
	if len(duid) != 48 {
		return fmt.Errorf("DUID must be 48 characters, got %d", len(duid))
	}

	matched, _ := regexp.MatchString("^[0-9a-fA-F]{48}$", duid)
	if !matched {
		return fmt.Errorf("DUID must contain only hexadecimal characters")
	}

	return nil
}

func GenerateLoginURL(duid string) string {
	return BuildLoginURL(duid)
}
