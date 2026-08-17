#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <array>
#include <utility>
#include <sodium.h>
#include "types.hpp"

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

namespace logana {
    // sizes of keys and signatures
    constexpr size_t PUBLIC_KEY_SIZE = crypto_sign_ed25519_PUBLICKEYBYTES;
    constexpr size_t SECRET_KEY_SIZE = crypto_sign_ed25519_SECRETKEYBYTES;
    constexpr size_t SIGNATURE_SIZE = crypto_sign_ed25519_BYTES;
    constexpr size_t NONCE_BYTES = 32;

    // representing keys and signatures as arrays of bytes
    using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
    using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;
    using Signature = std::array<uint8_t, SIGNATURE_SIZE>;

    // types of frames being sent over the wire
    enum FrameType : uint8_t {
        auth_challenge = 0x00, // requesting authentication from the client
        auth_response = 0x01, // client responding to the challenge with a signature
        message_send = 0x02, // client sending a message to the server
        ref_push = 0x03, // server pushing a message reference to the client
        payload_fetch_request = 0x04, // client requesting the payload of a message from the server
        payload_fetch_response = 0x05, // server responding to the client with the payload of a message
        ack = 0x06, // client acknowledging receipt of a message reference or payload
        heartbeat_ping = 0x07, // client sending a heartbeat ping to the server
        heartbeat_pong = 0x08, // server responding to the client with a heartbeat pong
        permission_update = 0x09, // server notifying the client of a permission update
        error = 0x0A // server notifying the client of an error
    };

    // we're using a uint8_t for a byte-size FrameType
    static_assert(sizeof(FrameType) == 1);

    // auth challenge holds a nonce for the client to sign with its secret key
    #pragma pack(push, 1)
    struct AuthChallenge {
        uint8_t nonce[32];
    };
    #pragma pack(pop)

    static_assert(sizeof(AuthChallenge) == 32);

    // auth response holds the nonce signature and account + device ids
    #pragma pack(push, 1)
    struct AuthResponse {
        uint8_t signature[SIGNATURE_SIZE];
        AccountId account_id;
        DeviceId device_id;
    };
    #pragma pack(pop)

    static_assert(sizeof(AuthResponse) == 72);

    // sent message holds tag id, message type, flags, and timestamp
    #pragma pack(push, 1)
    struct MessageSend {
        TagId tag_id;
        MessageType msg_type;
        uint8_t flags;
        Timestamp timestamp;
    };
    #pragma pack(pop)

    static_assert(sizeof(MessageSend) == 14);

    // fetch req holds sender id and sequence of desired message
    #pragma pack(push, 1)
    struct PayloadFetchRequest {
        AccountId sender_id;
        Sequence sequence;
    };
    #pragma pack(pop)

    static_assert(sizeof(PayloadFetchRequest) == 12);

    // fetch res holds account id, sequence, message type, and flags
    #pragma pack(push, 1)
    struct PayloadFetchResponse {
        AccountId sender_id;
        Sequence sequence;
        MessageType msg_type;
        uint8_t flags;
    };
    #pragma pack(pop)

    static_assert(sizeof(PayloadFetchResponse) == 14);

    // ack holds recieved message sequence
    #pragma pack(push, 1)
    struct Ack {
        Sequence sequence;
    };
    #pragma pack(pop)

    static_assert(sizeof(Ack) == 8);

    // update holds the tag and account ids plus the new set of permissions
    #pragma pack(push, 1)
    struct PermissionUpdate {
        TagId tag_id;
        AccountId target_id;
        uint8_t permissions;
    };
    #pragma pack(pop)

    static_assert(sizeof(PermissionUpdate) == 9);

    // errors just hold the code
    #pragma pack(push, 1)
    struct Error {
        uint16_t error_code;
    };
    #pragma pack(pop)

    static_assert(sizeof(Error) == 2);

    // frame header holds the magic bytes, frame type, flags, and payload length
    #pragma pack(push, 1)
    struct FrameHeader {
        uint8_t magic[2];
        FrameType type;
        uint8_t flags;
        uint32_t payload_length;
    };
    #pragma pack(pop)

    static_assert(sizeof(FrameHeader) == 8);

    // checks thhe magic bytes and frame type, return host order payload length and frame type if valid, else nullopt
    inline std::optional<std::pair<uint32_t, FrameType>> validate_header(const FrameHeader& header) {
        if (header.magic[0] == 0x4C && header.magic[1] == 0x4E && header.type <= 0x0A) {
            return std::make_optional<std::pair<uint32_t, FrameType>>({ntohl(header.payload_length), header.type});
        }
        return std::nullopt;
    }

    // creates a frame header with the given flags, payload length, and frame type
    inline FrameHeader serialize(uint8_t flags, uint32_t payload_length, FrameType type) {
        FrameHeader header;
        header.magic[0] = 0x4C;
        header.magic[1] = 0x4E;
        header.flags = flags;
        header.payload_length = htonl(payload_length);
        header.type = type;
        return header;
    }

    // copies buffer into header if it's long enough, then returns validated copy
    inline std::optional<std::pair<uint32_t, FrameType>> deserialize(const uint8_t* buffer, size_t len) {
        if (len < sizeof(FrameHeader)) return std::nullopt;
        FrameHeader header;
        std::memcpy(&header, buffer, sizeof(FrameHeader));
        return validate_header(header);
    }
}
