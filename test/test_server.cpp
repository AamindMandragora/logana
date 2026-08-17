#include <cassert>
#include <iostream>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

#include "logana/transport.hpp"
#include "logana/frame.hpp"
#include "logana/crypto.hpp"

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace logana;

// helper: connects a raw TCP client to localhost on the given port
int connect_client(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    int err = connect(fd, (sockaddr*)&addr, sizeof(addr));
    assert(err == 0);
    return fd;
}

// helper: sends a raw frame (header + payload) over a socket
void send_raw_frame(int fd, FrameType type, uint8_t flags, const uint8_t* payload, uint32_t payload_size) {
    auto header = serialize(flags, payload_size, type);
    send(fd, &header, sizeof(FrameHeader), 0);
    if (payload_size > 0) {
        send(fd, payload, payload_size, 0);
    }
}

// helper: receives exactly n bytes from a socket
void recv_exact(int fd, uint8_t* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = recv(fd, buf + total, n - total, 0);
        assert(r > 0);
        total += r;
    }
}

// helper: waits for an atomic bool with a timeout, returns true if the flag was set
bool wait_for(std::atomic<bool>& flag, int timeout_ms = 2000) {
    auto start = std::chrono::steady_clock::now();
    while (!flag.load()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// tests that connecting and disconnecting fires the correct callbacks
void test_connect_disconnect() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<bool> disconnected{false};

    server.set_on_connect([&](Connection) {
        connected.store(true);
    });
    server.set_on_disconnect([&](Connection) {
        disconnected.store(true);
    });

    // starts server on a background thread
    std::thread server_thread([&]() {
        server.start(9100);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // connects a raw TCP client
    int client_fd = connect_client(9100);
    // waits for on_connect to fire
    assert(wait_for(connected));

    // disconnects client
    close(client_fd);
    // waits for on_disconnect to fire
    assert(wait_for(disconnected));

    server.stop();
    server_thread.join();

    std::cout << "test_connect_disconnect passed" << std::endl;
}

// tests that sending a frame from a client fires on_frame with correct type and payload
void test_client_sends_frame() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<bool> frame_received{false};
    FrameType received_type;
    uint32_t received_len = 0;

    server.set_on_connect([&](Connection) {
        connected.store(true);
    });
    server.set_on_frame([&](Connection, FrameType type, const uint8_t*, uint32_t len) {
        received_type = type;
        received_len = len;
        frame_received.store(true);
    });

    std::thread server_thread([&]() {
        server.start(9101);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9101);
    assert(wait_for(connected));

    // sends a heartbeat_ping (zero-length payload)
    send_raw_frame(client_fd, FrameType::heartbeat_ping, 0, nullptr, 0);

    // waits for on_frame to fire
    assert(wait_for(frame_received));
    assert(received_type == FrameType::heartbeat_ping);
    assert(received_len == 0);

    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_client_sends_frame passed" << std::endl;
}

// tests that the server can send a frame and the client receives it correctly
void test_server_sends_frame() {
    Transport server;
    std::atomic<bool> connected{false};
    Connection client_conn;

    server.set_on_connect([&](Connection id) {
        client_conn = id;
        connected.store(true);
    });

    std::thread server_thread([&]() {
        server.start(9102);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9102);
    assert(wait_for(connected));

    // server sends an ack frame to the client
    Ack ack{};
    ack.sequence = 42;
    server.send_frame(client_conn, FrameType::ack, 0,
        reinterpret_cast<const uint8_t*>(&ack), sizeof(Ack));

    // client reads the frame
    uint8_t buf[sizeof(FrameHeader) + sizeof(Ack)];
    recv_exact(client_fd, buf, sizeof(buf));

    // deserializes and verifies
    auto result = deserialize(buf, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::ack);
    assert(len == sizeof(Ack));

    auto* payload = reinterpret_cast<const Ack*>(buf + sizeof(FrameHeader));
    assert(payload->sequence == 42);

    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_server_sends_frame passed" << std::endl;
}

// tests round-trip: client sends frame, server echoes back a response
void test_frame_roundtrip() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<bool> frame_received{false};

    server.set_on_connect([&](Connection) {
        connected.store(true);
    });
    server.set_on_frame([&](Connection id, FrameType, const uint8_t*, uint32_t) {
        frame_received.store(true);
        // echoes back a heartbeat_pong
        server.send_frame(id, FrameType::heartbeat_pong, 0, nullptr, 0);
    });

    std::thread server_thread([&]() {
        server.start(9103);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9103);
    assert(wait_for(connected));

    // sends heartbeat_ping
    send_raw_frame(client_fd, FrameType::heartbeat_ping, 0, nullptr, 0);
    assert(wait_for(frame_received));

    // reads the pong response
    uint8_t buf[sizeof(FrameHeader)];
    recv_exact(client_fd, buf, sizeof(buf));

    auto result = deserialize(buf, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::heartbeat_pong);
    assert(len == 0);

    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_frame_roundtrip passed" << std::endl;
}

// tests sending a frame with a variable-length payload
void test_frame_with_payload() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<bool> frame_received{false};
    uint8_t received_payload[256] = {};
    uint32_t received_len = 0;

    server.set_on_connect([&](Connection) {
        connected.store(true);
    });
    server.set_on_frame([&](Connection, FrameType, const uint8_t* payload, uint32_t len) {
        std::memcpy(received_payload, payload, len);
        received_len = len;
        frame_received.store(true);
    });

    std::thread server_thread([&]() {
        server.start(9104);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9104);
    assert(wait_for(connected));

    // sends an auth_challenge frame with a known nonce pattern
    AuthChallenge challenge{};
    for (int i = 0; i < 32; i++) challenge.nonce[i] = static_cast<uint8_t>(i);
    send_raw_frame(client_fd, FrameType::auth_challenge, 0,
        reinterpret_cast<const uint8_t*>(&challenge), sizeof(AuthChallenge));

    assert(wait_for(frame_received));
    assert(received_len == sizeof(AuthChallenge));
    // verifies payload bytes arrived intact
    for (int i = 0; i < 32; i++) {
        assert(received_payload[i] == static_cast<uint8_t>(i));
    }

    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_frame_with_payload passed" << std::endl;
}

// tests that multiple frames sent in quick succession are all received
void test_multiple_frames() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<int> frame_count{0};

    server.set_on_connect([&](Connection) {
        connected.store(true);
    });
    server.set_on_frame([&](Connection, FrameType, const uint8_t*, uint32_t) {
        frame_count.fetch_add(1);
    });

    std::thread server_thread([&]() {
        server.start(9105);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9105);
    assert(wait_for(connected));

    // sends five heartbeat_pings back to back
    for (int i = 0; i < 5; i++) {
        send_raw_frame(client_fd, FrameType::heartbeat_ping, 0, nullptr, 0);
    }

    // waits until all five are received
    auto start = std::chrono::steady_clock::now();
    while (frame_count.load() < 5) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        assert(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 2000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(frame_count.load() == 5);

    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_multiple_frames passed" << std::endl;
}

// tests that invalid magic causes the server to disconnect the client
void test_invalid_magic_disconnects() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<bool> disconnected{false};

    server.set_on_connect([&](Connection) {
        connected.store(true);
    });
    server.set_on_disconnect([&](Connection) {
        disconnected.store(true);
    });

    std::thread server_thread([&]() {
        server.start(9106);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9106);
    assert(wait_for(connected));

    // sends 8 bytes of garbage (invalid magic)
    uint8_t garbage[8] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    send(client_fd, garbage, 8, 0);

    // server should disconnect on bad magic
    assert(wait_for(disconnected));

    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_invalid_magic_disconnects passed" << std::endl;
}

// tests the full auth handshake flow over TCP
void test_auth_handshake() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<bool> auth_success{false};
    std::atomic<bool> auth_failed{false};

    // generates a keypair and registers it
    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    AccountId test_account = 1;
    DeviceId test_device = 1;
    key_store[test_account] = pk;

    // server state for tracking the nonce it sent
    std::array<uint8_t, 32> server_nonce{};

    server.set_on_connect([&](Connection id) {
        // server sends auth_challenge on connect
        std::array<uint8_t, NONCE_BYTES> nonce;
        generate_nonce(nonce);
        std::memcpy(server_nonce.data(), nonce.data(), 32);
        AuthChallenge challenge{};
        std::memcpy(challenge.nonce, nonce.data(), 32);
        server.send_frame(id, FrameType::auth_challenge, 0,
            reinterpret_cast<const uint8_t*>(&challenge), sizeof(AuthChallenge));
        connected.store(true);
    });

    server.set_on_frame([&](Connection, FrameType type, const uint8_t* payload, uint32_t len) {
        if (type == FrameType::auth_response) {
            assert(len == sizeof(AuthResponse));
            auto* response = reinterpret_cast<const AuthResponse*>(payload);

            // looks up public key by account id
            auto it = key_store.find(response->account_id);
            if (it == key_store.end()) {
                auth_failed.store(true);
                return;
            }

            // reconstructs nonce as std::array for verify_challenge
            std::array<uint8_t, 32> nonce;
            std::memcpy(nonce.data(), server_nonce.data(), 32);

            // reconstructs signature as std::array
            Signature sig;
            std::memcpy(sig.data(), response->signature, 64);

            bool valid = verify_challenge(nonce, sig, it->second);
            if (valid) {
                auth_success.store(true);
            } else {
                auth_failed.store(true);
            }
        }
    });

    std::thread server_thread([&]() {
        server.start(9107);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9107);
    assert(wait_for(connected));

    // client receives auth_challenge
    uint8_t challenge_buf[sizeof(FrameHeader) + sizeof(AuthChallenge)];
    recv_exact(client_fd, challenge_buf, sizeof(challenge_buf));

    auto result = deserialize(challenge_buf, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::auth_challenge);
    assert(len == sizeof(AuthChallenge));

    auto* challenge = reinterpret_cast<const AuthChallenge*>(challenge_buf + sizeof(FrameHeader));

    // client signs the nonce
    std::array<uint8_t, 32> nonce;
    std::memcpy(nonce.data(), challenge->nonce, 32);
    Signature sig;
    sign_challenge(nonce, sk, sig);

    // client builds and sends auth_response
    AuthResponse response{};
    std::memcpy(response.signature, sig.data(), 64);
    response.account_id = test_account;
    response.device_id = test_device;
    send_raw_frame(client_fd, FrameType::auth_response, 0,
        reinterpret_cast<const uint8_t*>(&response), sizeof(AuthResponse));

    // waits for auth to complete
    assert(wait_for(auth_success));
    assert(!auth_failed.load());

    // clean up
    key_store.erase(test_account);
    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_auth_handshake passed" << std::endl;
}

// tests that auth fails with an invalid signature
void test_auth_handshake_bad_signature() {
    Transport server;
    std::atomic<bool> connected{false};
    std::atomic<bool> auth_failed{false};

    PublicKey pk;
    SecretKey sk;
    generate_keypair(pk, sk);
    AccountId test_account = 2;
    key_store[test_account] = pk;

    std::array<uint8_t, 32> server_nonce{};

    server.set_on_connect([&](Connection id) {
        std::array<uint8_t, NONCE_BYTES> nonce;
        generate_nonce(nonce);
        std::memcpy(server_nonce.data(), nonce.data(), 32);
        AuthChallenge challenge{};
        std::memcpy(challenge.nonce, nonce.data(), 32);
        server.send_frame(id, FrameType::auth_challenge, 0,
            reinterpret_cast<const uint8_t*>(&challenge), sizeof(AuthChallenge));
        connected.store(true);
    });

    server.set_on_frame([&](Connection, FrameType type, const uint8_t* payload, uint32_t) {
        if (type == FrameType::auth_response) {
            auto* response = reinterpret_cast<const AuthResponse*>(payload);
            auto it = key_store.find(response->account_id);
            assert(it != key_store.end());

            std::array<uint8_t, 32> nonce;
            std::memcpy(nonce.data(), server_nonce.data(), 32);
            Signature sig;
            std::memcpy(sig.data(), response->signature, 64);

            bool valid = verify_challenge(nonce, sig, it->second);
            if (!valid) {
                auth_failed.store(true);
            }
        }
    });

    std::thread server_thread([&]() {
        server.start(9108);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = connect_client(9108);
    assert(wait_for(connected));

    // client receives challenge
    uint8_t challenge_buf[sizeof(FrameHeader) + sizeof(AuthChallenge)];
    recv_exact(client_fd, challenge_buf, sizeof(challenge_buf));

    // client sends a response with garbage signature
    AuthResponse response{};
    std::memset(response.signature, 0xFF, 64);
    response.account_id = test_account;
    response.device_id = 1;
    send_raw_frame(client_fd, FrameType::auth_response, 0,
        reinterpret_cast<const uint8_t*>(&response), sizeof(AuthResponse));

    // auth should fail
    assert(wait_for(auth_failed));

    key_store.erase(test_account);
    close(client_fd);
    server.stop();
    server_thread.join();

    std::cout << "test_auth_handshake_bad_signature passed" << std::endl;
}

// runs all tests
int main() {
    test_connect_disconnect();
    test_client_sends_frame();
    test_server_sends_frame();
    test_frame_roundtrip();
    test_frame_with_payload();
    test_multiple_frames();
    test_invalid_magic_disconnects();
    test_auth_handshake();
    test_auth_handshake_bad_signature();

    std::cout << std::endl << "All server tests passed!" << std::endl;
    return 0;
}