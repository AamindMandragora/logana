#include <cassert>
#include <iostream>
#include <cstring>

#include "logana/frame.hpp"

using namespace logana;

// tests that FrameHeader is exactly 8 bytes
void test_header_size() {
    assert(sizeof(FrameHeader) == 8);

    std::cout << "test_header_size passed" << std::endl;
}

// tests that magic bytes are written correctly
void test_magic_bytes() {
    // serializes a header and inspects raw bytes
    auto header = serialize(0, 0, FrameType::heartbeat_ping);
    auto* raw = reinterpret_cast<const uint8_t*>(&header);
    // first two bytes should be 'L' 'N' (0x4C 0x4E)
    assert(raw[0] == 0x4C);
    assert(raw[1] == 0x4E);

    std::cout << "test_magic_bytes passed" << std::endl;
}

// tests that corrupted magic causes deserialize to reject the header
void test_invalid_magic() {
    // serializes a valid header then corrupts the first magic byte
    auto header = serialize(0, 0, FrameType::heartbeat_ping);
    auto* raw = reinterpret_cast<uint8_t*>(&header);
    raw[0] = 0xFF;

    // deserialize should return nullopt
    auto result = deserialize(raw, sizeof(FrameHeader));
    assert(!result.has_value());

    std::cout << "test_invalid_magic passed" << std::endl;
}

// tests that corrupting the second magic byte also fails
void test_invalid_magic_second_byte() {
    auto header = serialize(0, 0, FrameType::ack);
    auto* raw = reinterpret_cast<uint8_t*>(&header);
    raw[1] = 0x00;

    auto result = deserialize(raw, sizeof(FrameHeader));
    assert(!result.has_value());

    std::cout << "test_invalid_magic_second_byte passed" << std::endl;
}

// tests that deserialize rejects a buffer smaller than FrameHeader
void test_truncated_header() {
    auto header = serialize(0, 100, FrameType::ack);
    // pass only 4 bytes instead of 8
    auto result = deserialize(reinterpret_cast<const uint8_t*>(&header), 4);
    assert(!result.has_value());

    std::cout << "test_truncated_header passed" << std::endl;
}

// tests that a single-byte buffer is rejected
void test_truncated_header_one_byte() {
    auto header = serialize(0, 0, FrameType::heartbeat_ping);
    auto result = deserialize(reinterpret_cast<const uint8_t*>(&header), 1);
    assert(!result.has_value());

    std::cout << "test_truncated_header_one_byte passed" << std::endl;
}

// tests that a zero-byte buffer is rejected
void test_truncated_header_zero_bytes() {
    uint8_t dummy = 0;
    auto result = deserialize(&dummy, 0);
    assert(!result.has_value());

    std::cout << "test_truncated_header_zero_bytes passed" << std::endl;
}

// tests serialize/deserialize roundtrip for every frame type
void test_serialize_roundtrip_all_types() {
    FrameType types[] = {
        FrameType::auth_challenge,
        FrameType::auth_response,
        FrameType::message_send,
        FrameType::ref_push,
        FrameType::payload_fetch_request,
        FrameType::payload_fetch_response,
        FrameType::ack,
        FrameType::heartbeat_ping,
        FrameType::heartbeat_pong,
        FrameType::permission_update,
        FrameType::error
    };

    for (auto type : types) {
        uint32_t payload_size = 42;
        uint8_t flags = 0x03;
        // serialize then deserialize
        auto header = serialize(flags, payload_size, type);
        auto result = deserialize(reinterpret_cast<const uint8_t*>(&header), sizeof(FrameHeader));
        // assert roundtrip preserves type and payload length
        assert(result.has_value());
        auto [len, ft] = *result;
        assert(len == payload_size);
        assert(ft == type);
    }

    std::cout << "test_serialize_roundtrip_all_types passed" << std::endl;
}

// tests zero-length payload roundtrip
void test_zero_payload() {
    auto header = serialize(0, 0, FrameType::heartbeat_ping);
    auto result = deserialize(reinterpret_cast<const uint8_t*>(&header), sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(len == 0);
    assert(ft == FrameType::heartbeat_ping);

    std::cout << "test_zero_payload passed" << std::endl;
}

// tests that large payload lengths survive the htonl/ntohl roundtrip
void test_large_payload_length() {
    // value with bytes in every position to catch byte-order bugs
    uint32_t large = 0x01020304;
    auto header = serialize(0, large, FrameType::message_send);
    auto result = deserialize(reinterpret_cast<const uint8_t*>(&header), sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(len == large);

    std::cout << "test_large_payload_length passed" << std::endl;
}

// tests maximum payload length
void test_max_payload_length() {
    uint32_t max = 0xFFFFFFFF;
    auto header = serialize(0, max, FrameType::message_send);
    auto result = deserialize(reinterpret_cast<const uint8_t*>(&header), sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(len == max);

    std::cout << "test_max_payload_length passed" << std::endl;
}

// tests that flags byte is preserved in the header
void test_flags_preserved() {
    uint8_t flags = 0xAB;
    auto header = serialize(flags, 0, FrameType::ack);
    // flags/version is the 4th byte (magic[2] + type[1] + flags[1])
    auto* raw = reinterpret_cast<const uint8_t*>(&header);
    assert(raw[3] == flags);

    std::cout << "test_flags_preserved passed" << std::endl;
}

// tests that frame type byte is preserved in the header
void test_type_byte_position() {
    auto header = serialize(0, 0, FrameType::auth_challenge);
    // type is the 3rd byte (magic[2] + type[1])
    auto* raw = reinterpret_cast<const uint8_t*>(&header);
    assert(raw[2] == static_cast<uint8_t>(FrameType::auth_challenge));

    std::cout << "test_type_byte_position passed" << std::endl;
}

// tests a complete auth_challenge frame: header + 32-byte nonce payload
void test_auth_challenge_frame() {
    // builds a 32-byte nonce payload
    AuthChallenge challenge{};
    for (int i = 0; i < 32; i++) challenge.nonce[i] = static_cast<uint8_t>(i);

    // serializes header for this payload
    auto header = serialize(0, sizeof(AuthChallenge), FrameType::auth_challenge);

    // assembles complete wire frame
    uint8_t wire[sizeof(FrameHeader) + sizeof(AuthChallenge)];
    std::memcpy(wire, &header, sizeof(FrameHeader));
    std::memcpy(wire + sizeof(FrameHeader), &challenge, sizeof(AuthChallenge));

    // deserializes and checks header
    auto result = deserialize(wire, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::auth_challenge);
    assert(len == sizeof(AuthChallenge));

    // checks payload bytes survived
    auto* payload = reinterpret_cast<const AuthChallenge*>(wire + sizeof(FrameHeader));
    for (int i = 0; i < 32; i++) {
        assert(payload->nonce[i] == static_cast<uint8_t>(i));
    }

    std::cout << "test_auth_challenge_frame passed" << std::endl;
}

// tests a complete auth_response frame: header + 72-byte payload
void test_auth_response_frame() {
    AuthResponse response{};
    // fills signature with known pattern
    for (int i = 0; i < 64; i++) response.signature[i] = static_cast<uint8_t>(i);
    response.account_id = 42;
    response.device_id = 7;

    auto header = serialize(0, sizeof(AuthResponse), FrameType::auth_response);

    uint8_t wire[sizeof(FrameHeader) + sizeof(AuthResponse)];
    std::memcpy(wire, &header, sizeof(FrameHeader));
    std::memcpy(wire + sizeof(FrameHeader), &response, sizeof(AuthResponse));

    auto result = deserialize(wire, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::auth_response);
    assert(len == sizeof(AuthResponse));

    auto* payload = reinterpret_cast<const AuthResponse*>(wire + sizeof(FrameHeader));
    assert(payload->account_id == 42);
    assert(payload->device_id == 7);
    assert(payload->signature[0] == 0);
    assert(payload->signature[63] == 63);

    std::cout << "test_auth_response_frame passed" << std::endl;
}

// tests a complete ref_push frame: header + 24-byte MessageRef payload
void test_ref_push_frame() {
    MessageRef ref{};
    ref.sender_id = 42;
    ref.sequence = 1000;
    ref.tag_id = 7;
    ref.timestamp = 9999;

    auto header = serialize(0, sizeof(MessageRef), FrameType::ref_push);

    uint8_t wire[sizeof(FrameHeader) + sizeof(MessageRef)];
    std::memcpy(wire, &header, sizeof(FrameHeader));
    std::memcpy(wire + sizeof(FrameHeader), &ref, sizeof(MessageRef));

    auto result = deserialize(wire, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::ref_push);
    assert(len == sizeof(MessageRef));

    auto* payload = reinterpret_cast<const MessageRef*>(wire + sizeof(FrameHeader));
    assert(payload->sender_id == 42);
    assert(payload->sequence == 1000);
    assert(payload->tag_id == 7);
    assert(payload->timestamp == 9999);

    std::cout << "test_ref_push_frame passed" << std::endl;
}

// tests a complete ack frame: header + 8-byte sequence payload
void test_ack_frame() {
    Ack ack{};
    ack.sequence = 12345;

    auto header = serialize(0, sizeof(Ack), FrameType::ack);

    uint8_t wire[sizeof(FrameHeader) + sizeof(Ack)];
    std::memcpy(wire, &header, sizeof(FrameHeader));
    std::memcpy(wire + sizeof(FrameHeader), &ack, sizeof(Ack));

    auto result = deserialize(wire, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::ack);
    assert(len == sizeof(Ack));

    auto* payload = reinterpret_cast<const Ack*>(wire + sizeof(FrameHeader));
    assert(payload->sequence == 12345);

    std::cout << "test_ack_frame passed" << std::endl;
}

// tests a complete payload_fetch_request frame: header + 12-byte payload
void test_payload_fetch_request_frame() {
    PayloadFetchRequest req{};
    req.sender_id = 10;
    req.sequence = 500;

    auto header = serialize(0, sizeof(PayloadFetchRequest), FrameType::payload_fetch_request);

    uint8_t wire[sizeof(FrameHeader) + sizeof(PayloadFetchRequest)];
    std::memcpy(wire, &header, sizeof(FrameHeader));
    std::memcpy(wire + sizeof(FrameHeader), &req, sizeof(PayloadFetchRequest));

    auto result = deserialize(wire, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::payload_fetch_request);
    assert(len == sizeof(PayloadFetchRequest));

    auto* payload = reinterpret_cast<const PayloadFetchRequest*>(wire + sizeof(FrameHeader));
    assert(payload->sender_id == 10);
    assert(payload->sequence == 500);

    std::cout << "test_payload_fetch_request_frame passed" << std::endl;
}

// tests a complete permission_update frame: header + 9-byte payload
void test_permission_update_frame() {
    PermissionUpdate update{};
    update.tag_id = 3;
    update.target_id = 15;
    update.permissions = 0x07;

    auto header = serialize(0, sizeof(PermissionUpdate), FrameType::permission_update);

    uint8_t wire[sizeof(FrameHeader) + sizeof(PermissionUpdate)];
    std::memcpy(wire, &header, sizeof(FrameHeader));
    std::memcpy(wire + sizeof(FrameHeader), &update, sizeof(PermissionUpdate));

    auto result = deserialize(wire, sizeof(FrameHeader));
    assert(result.has_value());
    auto [len, ft] = *result;
    assert(ft == FrameType::permission_update);
    assert(len == sizeof(PermissionUpdate));

    std::cout << "test_permission_update_frame passed" << std::endl;
}

// tests heartbeat ping and pong have zero-length payloads
void test_heartbeat_zero_length() {
    auto ping = serialize(0, 0, FrameType::heartbeat_ping);
    auto pong = serialize(0, 0, FrameType::heartbeat_pong);

    auto r1 = deserialize(reinterpret_cast<const uint8_t*>(&ping), sizeof(FrameHeader));
    auto r2 = deserialize(reinterpret_cast<const uint8_t*>(&pong), sizeof(FrameHeader));

    assert(r1.has_value());
    assert(r2.has_value());
    assert(r1->first == 0);
    assert(r2->first == 0);
    assert(r1->second == FrameType::heartbeat_ping);
    assert(r2->second == FrameType::heartbeat_pong);

    std::cout << "test_heartbeat_zero_length passed" << std::endl;
}

// tests that all-zero buffer (no valid magic) is rejected
void test_all_zeros_rejected() {
    uint8_t zeros[sizeof(FrameHeader)] = {};
    auto result = deserialize(zeros, sizeof(FrameHeader));
    assert(!result.has_value());

    std::cout << "test_all_zeros_rejected passed" << std::endl;
}

// tests that garbage buffer is rejected
void test_garbage_rejected() {
    uint8_t garbage[sizeof(FrameHeader)];
    std::memset(garbage, 0xFF, sizeof(garbage));
    auto result = deserialize(garbage, sizeof(FrameHeader));
    assert(!result.has_value());

    std::cout << "test_garbage_rejected passed" << std::endl;
}

// tests payload struct sizes match the wire protocol spec
void test_payload_struct_sizes() {
    assert(sizeof(FrameHeader) == 8);
    assert(sizeof(AuthChallenge) == 32);
    assert(sizeof(AuthResponse) == 72);
    assert(sizeof(PayloadFetchRequest) == 12);
    assert(sizeof(Ack) == 8);
    assert(sizeof(PermissionUpdate) == 9);
    assert(sizeof(MessageRef) == 24);

    std::cout << "test_payload_struct_sizes passed" << std::endl;
}

// runs all tests
int main() {
    test_header_size();
    test_magic_bytes();
    test_invalid_magic();
    test_invalid_magic_second_byte();
    test_truncated_header();
    test_truncated_header_one_byte();
    test_truncated_header_zero_bytes();
    test_serialize_roundtrip_all_types();
    test_zero_payload();
    test_large_payload_length();
    test_max_payload_length();
    test_flags_preserved();
    test_type_byte_position();
    test_auth_challenge_frame();
    test_auth_response_frame();
    test_ref_push_frame();
    test_ack_frame();
    test_payload_fetch_request_frame();
    test_permission_update_frame();
    test_heartbeat_zero_length();
    test_all_zeros_rejected();
    test_garbage_rejected();
    test_payload_struct_sizes();

    std::cout << std::endl << "All frame tests passed!" << std::endl;
    return 0;
}