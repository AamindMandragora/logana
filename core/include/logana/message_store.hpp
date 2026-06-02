#pragma once

#include <unordered_map>
#include <optional>

#include "types.hpp"
#include "chunked_deque.hpp"

namespace logana {
    class MessageStore {
        std::unordered_map<AccountId, std::unique_ptr<ChunkedDeque<512>>> messages_;
    public:
        MessageRef store_and_ref(AccountId sender_id, TagId tag_id, MessageType msg_type, uint8_t flags, Timestamp timestamp, const uint8_t* payload, uint32_t payload_size) {
            Message message{};
            message.header.sender_id = sender_id;
            message.header.tag_id = tag_id;
            message.header.msg_type = msg_type;
            message.header.flags = flags;
            message.header.timestamp = timestamp;
            message.header.payload_size = payload_size;
            message.payload = new uint8_t[payload_size];
            std::memcpy(message.payload, payload, payload_size);
            auto& deque = messages_[sender_id];
            if (!deque) deque = std::make_unique<ChunkedDeque<512>>();
            message.header.sequence = deque->get_next_sequence();
            MessageRef ref;
            ref.sender_id = sender_id;
            ref.tag_id = tag_id;
            ref.timestamp = timestamp;
            ref.sequence = deque->push(std::move(message));
            return ref;
        }

        std::optional<Message> fetch(AccountId account_id, Sequence sequence) {
            auto it = messages_.find(account_id);
            if (it == messages_.end()) return std::nullopt;
            Message *message = it->second->read(sequence);
            if (message && !message->is_deleted()) {
                return message->clone();
            }
            return std::nullopt;
        }

        void evict_before(AccountId account_id, Sequence sequence) {
            auto it = messages_.find(account_id);
            if (it != messages_.end()) it->second->update_oldest_sequence(sequence);
        }
    };
}