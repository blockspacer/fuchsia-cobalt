// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COBALT_UTIL_CRYPTO_UTIL_CIPHER_H_
#define COBALT_UTIL_CRYPTO_UTIL_CIPHER_H_

#include <memory>
#include <string>
#include <vector>

#include "util/crypto_util/types.h"

namespace cobalt {

namespace crypto {

class CipherContext;
class HybridCipherContext;

// Provides a C++ interface to an AEAD implementation.
//
// An instance of SymmetricCipher may be used repeatedly for multiple
// encryptions or decryptions. The method set_key() must be invoked before
// any other methods or the other operations will fail or crash.
class SymmetricCipher {
 public:
  static const size_t KEY_SIZE = 128 / 8;
  static const size_t NONCE_SIZE = 96 / 8;

  SymmetricCipher();
  ~SymmetricCipher();

  // Sets the secret key for encryption or decryption. This must be invoked
  // at least once before an instance of SymmetricCipher may be used.
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  // |key| Must have length |KEY_SIZE|.
  //
  bool set_key(const byte key[KEY_SIZE]);

  // Performs AEAD encryption.
  //
  // |nonce| must have length |NONCE_SIZE|. It is essential that the same
  // (key, nonce) pair never be used to encrypt two different plain texts.
  // If re-using the same key multiple times you *must* change the nonce
  // or the resulting encryption will not be secure.
  //
  // |ptext| The plain text to be encrypted.
  //
  // |ptext_len| The number of bytes of plain text.
  //
  // |ctext| A pointer to a vector that will be modified to contain
  // the ciphertext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool Encrypt(const byte nonce[NONCE_SIZE], const byte* ptext, size_t ptext_len,
               std::vector<byte>* ctext);

  // Performs AEAD decryption.
  //
  // |nonce| must have length |NONCE_SIZE|.
  //
  // |ctext| The ciphertext to be decrypted.
  //
  // |ctext_len| The number of bytes of ciphertext.
  //
  // |ptext| A pointer to a vector that will be modified to contain the
  // recovered plaintext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool Decrypt(const byte nonce[NONCE_SIZE], const byte* ctext, size_t ctext_len,
               std::vector<byte>* ptext);

 private:
  std::unique_ptr<CipherContext> context_;
};

// Provides a C++ interface to a public-key hybrid encryption using ECDH. The
// algorithm implemented is as follows:
//
// Public key = g^x in an elliptic curve group (NIST P256) represented in
// X9.62 serialization with a compressed point representation.
//
// Private key = x stored in bytes and interpreted as a big-endian number (as
// in the interface of BN_bin2bn in boringssl)
//
// Enc(public key, message):
//
//    1. Samples a fresh EC keypair (g^y, y)
//    2. Samples a salt
//    3. Computes symmetric key = HKDF(g^y, g^xy, salt) with SHA512
//    compression function (also, see Note 2)
//    4. (Symmetric) encrypts message using SymmetricCipher::encrypt with key
//    and all-zero nonce into ciphertext
//    5. Publishes (public_key_part, salt, symmetric_ciphertext) as the hybrid
//    ciphertext, where public_key_part is the X9.62 serialization of g^y.
//
// Dec(private key, hybrid_ciphertext)
//    where hybrid_ciphertext = (public_key_part, salt, symmetric_ciphertext)):
//
//    1. Computes symmetric key = HKDF(g^y, g^xy, salt) with SHA512
//    compression function from private key (x) and public_key_part (g^y)
//    2. (Symmetric) decrypts symmetric_ciphertext using
//    SymmetricCipher::decrypt with key and all-zero nonce.
//
// An instance of HybridCipher may be used repeatedly for multiple
// encryptions or decryptions. The method set_public_key() must be used before
// encryptions and set_private_key() must be used before decryptions.
//
// Note 1: We choose to fix the nonce (to all-zeroes) to save on overhead. This
// is particularly useful when we're encrypting small plaintexts. For more
// information, see https://goto.google.com/aes-gcm-zero-nonce-security
//
// Note 2: This implements the ECIES way of deriving a shared key from ECDH as
// outlined in an ISO standard draft: http://www.shoup.net/iso/std6.pdf. The
// same construction hashing g^y and key length is given in the HEG
// construction in Section 10.1 in http://www.shoup.net/papers/cca2.pdf
//
// TODO(pseudorandom): Can we eliminate salt and save space without
// compromising security? Probably not.
class HybridCipher {
 public:
  // NOTE: All three sizes below specify a number of bytes (not bits.)
  static const size_t PUBLIC_KEY_SIZE = 1 + 256 / 8;  // One byte extra
                                                      // for X9.62 serialization
  static const size_t PRIVATE_KEY_SIZE = 256 / 8;
  static const size_t SALT_SIZE = 128 / 8;  // Salt for HKDF
  static const size_t PUBLIC_KEY_FINGERPRINT_SIZE = 256 / 8;

  // Generates a cryptographically secure public/private key pair appropriate
  // for use by an instance of HybridCipher. Returns true on success.
  static bool GenerateKeyPair(byte public_key[HybridCipher::PUBLIC_KEY_SIZE],
                              byte private_key[HybridCipher::PRIVATE_KEY_SIZE]);

  static bool GenerateKeyPairPEM(std::string* public_key_pem_out, std::string* private_key_pem_out);

  HybridCipher();
  ~HybridCipher();

  // Sets the public key for encryption. One of the two set_public_key*
  // methods must be invoked at least once before Encrypt is called.
  // Using decryption after set_public_key* is undefined behavior.
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.

  // |key| must have length |PUBLIC_KEY_SIZE| and be
  // X9.62 serialized
  bool set_public_key(const byte key[PUBLIC_KEY_SIZE]);

  // Writes the SHA256 fingerprint of this HybridCipher's public key into
  // |fingerprint|. Returns true for success or false for failure.
  bool public_key_fingerprint(byte fingerprint[PUBLIC_KEY_FINGERPRINT_SIZE]);

  // |key_pem| must be a PEM encoding of a public key as would be passed
  // to |set_public_key|.
  bool set_public_key_pem(const std::string& key_pem);

  // Sets the private key for decryption. One of the two set_private_key*
  // methods must be invoked at least once before Decrypt is called. Using
  // encryption after set_private_key* is undefined behavior.
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.

  // |key| must have length |PRIVATE_KEY_SIZE| and will be
  // interpreted as big-endian big integer.
  bool set_private_key(const byte key[PRIVATE_KEY_SIZE]);

  // |key_pem| must be a PEM encoding of a private key as would be passed
  // to |set_private_key|.
  bool set_private_key_pem(const std::string& key_pem);

  // Performs ECDH-based hybrid encryption
  //
  // |ptext| The plain text to be encrypted
  //
  // |ptext_len| The number of bytes of plain text
  //
  // |hybrid_ctext| A pointer to a vector that will be modified to
  // contain the hybrid ciphertext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool Encrypt(const byte* ptext, size_t ptext_len, std::vector<byte>* hybrid_ctext);

  // Performs ECDH-based hybrid decryption.
  //
  // |hybrid_ctext| The hybrid ciphertext to be decrypted.
  //
  // |hybrid_ctext_len| The number of bytes of hybrid ciphertext.
  //
  // |ptext| A pointer to a vector that will be modified to contain the
  // recovered plaintext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool Decrypt(const byte* hybrid_ctext, size_t hybrid_ctext_len, std::vector<byte>* ptext);

 private:
  // Performs ECDH-based hybrid encryption
  //
  // |ptext| The plain text to be encrypted
  //
  // |ptext_len| The number of bytes of plain text
  //
  // The output is represented in three parts which will be written to
  // |public_key_part_out|, |salt_out| and |symmetric_ctext_out| respectively
  //
  // |public_key_part_out| must point to a buffer of size |PUBLIC_KEY_SIZE|.
  // The X9.62 serialization of g^y will be written there
  //
  // |salt| is a pointer to a vector that will be modified to store a random
  // salt of size |SALT_SIZE|
  //
  // |symmetric_ctext_out| A pointer to a vector that will be modified to
  // contain the ciphertext under the derived symmetric key
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool EncryptInternal(const byte* ptext, size_t ptext_len,
                       byte public_key_part_out[PUBLIC_KEY_SIZE], byte salt_out[SALT_SIZE],
                       std::vector<byte>* symmetric_ctext_out);

  // Performs ECDH-based hybrid decryption.
  //
  // |public_key_part| must have length |PUBLIC_KEY_SIZE| and be an X9.62
  // encoded elliptic curve group element. After invoking
  // HybridCipher::Encrypt() the argument |public_key_part_out| will contain
  // such a value
  //
  // |salt| The salt to be used in the HKDF step in decryption. Must have size
  // |SALT_SIZE|
  //
  // |symmetric_ctext| The ciphertext to be decrypted.
  //
  // |symmetric_ctext_len| The number of bytes of symmetric ciphertext.
  //
  // |ptext| A pointer to a vector that will be modified to contain the
  // recovered plaintext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool DecryptInternal(const byte public_key_part[PUBLIC_KEY_SIZE], const byte salt[SALT_SIZE],
                       const byte* symmetric_ctext, size_t symmetric_ctext_len,
                       std::vector<byte>* ptext);

  std::unique_ptr<HybridCipherContext> context_;
  std::unique_ptr<SymmetricCipher> symm_cipher_;
};

}  // namespace crypto

}  // namespace cobalt

#endif  // COBALT_UTIL_CRYPTO_UTIL_CIPHER_H_
