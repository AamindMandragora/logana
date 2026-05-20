#pragma once

// gives us the fixed-width unsigned int types
#include <cstdint>

// lets us assert that the raw bytes of message headers and refs can be copied with memcpy to get a valid object
#include <type_traits>

namespace logana {
    using AccountId = uint32_t;
    using DeviceId = uint32_t;
    using TagId = uint32_t;
    using Sequence = uint64_t;
    using Timestamp = uint64_t;
    using Connection = int;

    // the default flag
    constexpr uint8_t FLAG_NONE = 0;
    // for tombstone messages
    constexpr uint8_t FLAG_DELETED = 1 << 0;
    // for auto-expiring messages
    constexpr uint8_t FLAG_EPHEMERAL = 1 << 1;
    // indicates payload is e2ee, always set before relay
    constexpr uint8_t FLAG_ENCRYPTED = 1 << 2;

    enum class MessageType : uint8_t {
        // system messages
        Control,
        // regular text messages
        Text,
        // other files (image/audio/video)
        Media,
        // message log update for other devices
        Sync
    };

    struct alignas(64) MessageHeader {
        // the index of the message in the deque
        Sequence sequence;
        // unix millisecond timestamp
        Timestamp timestamp;
        // the userid of sender
        AccountId sender_id;
        // the tag being sent to
        TagId tag_id;
        // how many bytes is the payload
        uint32_t payload_size;
        // tells receiver how to interpret payload
        MessageType msg_type;
        // bitfield for flags
        uint8_t flags;
        // padding bytes, might modify later
        uint8_t reserved[34];
    };

    static_assert(sizeof(MessageHeader) == 64);
    static_assert(std::is_trivially_copyable_v<MessageHeader>);

    struct MessageRef {
        // the index of the message in the deque
        Sequence sequence;
        // unix millisecond timestamp
        Timestamp timestamp;
        // the userid of sender
        AccountId sender_id;
        // the tag being sent to
        TagId tag_id;
    };

    static_assert(std::is_trivially_copyable_v<MessageRef>);

    struct Message {
        // the header for the message
        MessageHeader header;
        // raw bytes of data
        uint8_t *payload;

        // constructor
        Message() : header{}, payload{nullptr} {}
        // deleted copy constructor + assignment operator
        Message(const Message&) = delete;
        Message& operator=(const Message&) = delete;
        // destructor just deletes the data
        ~Message() {
            delete[] payload;
        }

        // double ampersand for rvalue (temporary), noexcept says will never throw exception so std lib containers use it
        Message(Message&& other) noexcept {
            header = other.header;
            payload = other.payload;
            other.payload = nullptr;
        }

        // double ampersand for rvalue (temporary), noexcept says will never throw exception so std lib containers use it
        Message& operator=(Message&& other) noexcept {
            if (this != &other) {
                header = other.header;
                delete[] payload;
                payload = other.payload;
                other.payload = nullptr;
            }
            return *this;
        }

        // check if the message is a tombstone
        bool is_deleted() const {
            return header.flags & FLAG_DELETED;
        }

        // turns the message into a tombstone
        void mark_deleted() {
            header.flags |= FLAG_DELETED;
        }
    };
}