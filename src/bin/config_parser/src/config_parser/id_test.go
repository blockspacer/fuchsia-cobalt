package config_parser

import "testing"

func TestIdFromName(t *testing.T) {
	id := IdFromName("test_name")
	expected := uint32(0x8b85b08d)
	if id != expected {
		t.Errorf("%x != %x", id, expected)
	}
}

// This records a string that is a valid name and hashes to 0.
// This string is used for testing purposes.
func TestIdFromNameZero(t *testing.T) {
	zeroName := "NRaMinLNcqiYmgEypLLVGnXymNpxJzqabtbbjLycCMEohvVzZtAYpah"
	if IdFromName(zeroName) != 0 {
		t.Errorf("Zero name should hash to 0.")
	}
}

// This records the two specified strings collide. These stirngs are used in tests.
func TestIdFromNameCollision(t *testing.T) {
	n1 := "TpzQweXFfRXpQrDWvplhfXFbJptlKmkIlHBAzjPnADtJWVAawVrbPGg"
	n2 := "zDnlfjrXpwuQYpBrNTeCbtsRBydKuKdCvEjlGwdRJlxMjbYOSGPjhif"
	if IdFromName(n1) != IdFromName(n2) {
		t.Errorf("These two strings were supposed to collide.")
	}
}
