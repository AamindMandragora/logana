#pragma once

#include <unordered_map>
#include "frame.hpp"

namespace logana {
    // mapping from accounts to public keys
    inline std::unordered_map<AccountId, PublicKey> key_store;

    // generate a random nonce of 32 bytes
    inline void generate_nonce(std::array<uint8_t, NONCE_BYTES>& out) {
        randombytes_buf(out.data(), NONCE_BYTES);
    }

    // sign the nonce with the secret key and and write the signature to the argument
    inline void sign_challenge(const std::array<uint8_t, NONCE_BYTES>& nonce, const SecretKey& secret_key, Signature& signature_out) {
        crypto_sign_ed25519_detached(signature_out.data(), NULL, nonce.data(), NONCE_BYTES, secret_key.data());
    }
    
    // verify the signature of the nonce with the public key
    inline bool verify_challenge(const std::array<uint8_t, NONCE_BYTES>& nonce, const Signature& signature, const PublicKey& public_key) {
        return crypto_sign_ed25519_verify_detached(signature.data(), nonce.data(), NONCE_BYTES, public_key.data()) == 0;
    }

    // generate a new keypair and write the public key and secret key to the arguments
    inline void generate_keypair(PublicKey& pk_out, SecretKey& sk_out) {
        crypto_sign_ed25519_keypair(pk_out.data(), sk_out.data());
    }
}