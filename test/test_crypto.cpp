#include <cassert>
#include <iostream>
#include <cstring>

#include "logana/crypto.hpp"

using namespace logana;

// tests that generate_keypair produces non-zero keys
void test_generate_keypair() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);

    // checks that public key is not all zeros
    bool pk_nonzero = false;
    for (size_t i = 0; i < pk.size(); i++) {
        if (pk[i] != 0) { pk_nonzero = true; break; }
    }
    assert(pk_nonzero);

    // checks that secret key is not all zeros
    bool sk_nonzero = false;
    for (size_t i = 0; i < sk.size(); i++) {
        if (sk[i] != 0) { sk_nonzero = true; break; }
    }
    assert(sk_nonzero);

    std::cout << "test_generate_keypair passed" << std::endl;
}

// tests that two keypairs are different
void test_keypairs_unique() {
    PublicKey pk1, pk2;
    SecretKey sk1, sk2;
    generate_keypair(pk1, sk1);
    generate_keypair(pk2, sk2);

    // public keys should differ
    assert(pk1 != pk2);
    // secret keys should differ
    assert(sk1 != sk2);

    std::cout << "test_keypairs_unique passed" << std::endl;
}

// tests that generate_nonce produces a 32-byte non-zero nonce
void test_generate_nonce() {
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    // checks nonce is 32 bytes
    assert(nonce.size() == NONCE_BYTES);

    // checks nonce is not all zeros (astronomically unlikely with random)
    bool nonzero = false;
    for (size_t i = 0; i < nonce.size(); i++) {
        if (nonce[i] != 0) { nonzero = true; break; }
    }
    assert(nonzero);

    std::cout << "test_generate_nonce passed" << std::endl;
}

// tests that two nonces are different
void test_nonces_unique() {
    std::array<uint8_t, NONCE_BYTES> n1, n2;
    generate_nonce(n1);
    generate_nonce(n2);

    assert(n1 != n2);

    std::cout << "test_nonces_unique passed" << std::endl;
}

// tests sign then verify roundtrip succeeds
void test_sign_verify_roundtrip() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    // signs the nonce with the secret key
    Signature sig;
    sign_challenge(nonce, sk, sig);

    // verifies the signature against the public key
    bool valid = verify_challenge(nonce, sig, pk);
    assert(valid);

    std::cout << "test_sign_verify_roundtrip passed" << std::endl;
}

// tests that multiple sign/verify cycles with same keypair all succeed
void test_sign_verify_multiple_nonces() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);

    for (int i = 0; i < 10; i++) {
        std::array<uint8_t, NONCE_BYTES> nonce;
        generate_nonce(nonce);
        Signature sig;
        sign_challenge(nonce, sk, sig);
        assert(verify_challenge(nonce, sig, pk));
    }

    std::cout << "test_sign_verify_multiple_nonces passed" << std::endl;
}

// tests that a corrupted signature is rejected
void test_corrupted_signature() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    Signature sig;
    sign_challenge(nonce, sk, sig);

    // flips a byte in the signature
    sig[0] ^= 0xFF;

    bool valid = verify_challenge(nonce, sig, pk);
    assert(!valid);

    std::cout << "test_corrupted_signature passed" << std::endl;
}

// tests that the last byte corrupted is also caught
void test_corrupted_signature_last_byte() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    Signature sig;
    sign_challenge(nonce, sk, sig);

    // flips the last byte
    sig[63] ^= 0x01;

    bool valid = verify_challenge(nonce, sig, pk);
    assert(!valid);

    std::cout << "test_corrupted_signature_last_byte passed" << std::endl;
}

// tests that signing with one key and verifying with a different key fails
void test_wrong_public_key() {
    PublicKey pk1, pk2;
    SecretKey sk1, sk2;
    generate_keypair(pk1, sk1);
    generate_keypair(pk2, sk2);
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    // signs with sk1, verifies against pk2
    Signature sig;
    sign_challenge(nonce, sk1, sig);

    bool valid = verify_challenge(nonce, sig, pk2);
    assert(!valid);

    std::cout << "test_wrong_public_key passed" << std::endl;
}

// tests that a valid signature for one nonce doesn't verify against a different nonce
void test_wrong_nonce() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    std::array<uint8_t, NONCE_BYTES> nonce1, nonce2;
    generate_nonce(nonce1);
    generate_nonce(nonce2);

    // signs nonce1, tries to verify as if it were nonce2
    Signature sig;
    sign_challenge(nonce1, sk, sig);

    bool valid = verify_challenge(nonce2, sig, pk);
    assert(!valid);

    std::cout << "test_wrong_nonce passed" << std::endl;
}

// tests that an all-zero signature is rejected
void test_zero_signature() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    Signature zero_sig{};
    bool valid = verify_challenge(nonce, zero_sig, pk);
    assert(!valid);

    std::cout << "test_zero_signature passed" << std::endl;
}

// tests that signing the same nonce with the same key produces the same signature (Ed25519 is deterministic)
void test_deterministic_signing() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    Signature sig1, sig2;
    sign_challenge(nonce, sk, sig1);
    sign_challenge(nonce, sk, sig2);
    assert(sig1 == sig2);

    std::cout << "test_deterministic_signing passed" << std::endl;
}

// tests the full auth handshake flow end to end
void test_auth_handshake_flow() {
    // server generates keypair for a test account
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    AccountId test_account = 1;
    key_store[test_account] = pk;

    // server generates nonce and would send auth_challenge
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);

    // client signs the nonce with its secret key and would send auth_response
    Signature sig;
    sign_challenge(nonce, sk, sig);

    // server looks up public key by account and verifies
    auto it = key_store.find(test_account);
    assert(it != key_store.end());
    bool valid = verify_challenge(nonce, sig, it->second);
    assert(valid);

    // clean up key_store
    key_store.erase(test_account);

    std::cout << "test_auth_handshake_flow passed" << std::endl;
}

// tests that auth fails when account isn't in key_store
void test_auth_unknown_account() {
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);
    Signature sig;
    sign_challenge(nonce, sk, sig);

    // account 999 is not registered
    auto it = key_store.find(999);
    assert(it == key_store.end());

    std::cout << "test_auth_unknown_account passed" << std::endl;
}

// tests that auth fails when a different account's key is registered
void test_auth_wrong_account_key() {
    PublicKey pk1, pk2;
    SecretKey sk1, sk2;
    generate_keypair(pk1, sk1);
    generate_keypair(pk2, sk2);

    // registers pk1 for account 1
    key_store[1] = pk1;

    std::array<uint8_t, NONCE_BYTES> nonce;
    generate_nonce(nonce);
    // client signs with sk2 (not the key registered for account 1)
    Signature sig;
    sign_challenge(nonce, sk2, sig);

    // server verifies against pk1 — should fail
    bool valid = verify_challenge(nonce, sig, key_store[1]);
    assert(!valid);

    // clean up
    key_store.erase(1);

    std::cout << "test_auth_wrong_account_key passed" << std::endl;
}

// runs all tests
int main() {
    test_generate_keypair();
    test_keypairs_unique();
    test_generate_nonce();
    test_nonces_unique();
    test_sign_verify_roundtrip();
    test_sign_verify_multiple_nonces();
    test_corrupted_signature();
    test_corrupted_signature_last_byte();
    test_wrong_public_key();
    test_wrong_nonce();
    test_zero_signature();
    test_deterministic_signing();
    test_auth_handshake_flow();
    test_auth_unknown_account();
    test_auth_wrong_account_key();

    std::cout << std::endl << "All crypto tests passed!" << std::endl;
    return 0;
}