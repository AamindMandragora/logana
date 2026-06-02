#pragma once

#include "message_store.hpp"
#include "device_registry.hpp"
#include "tag_registry.hpp"

namespace logana {
    class RelayRouter {
        MessageStore& store_;
        DeviceRegistry& devices_;
        TagRegistry& tags_;
    public:
        RelayRouter(MessageStore& store, DeviceRegistry& devices, TagRegistry& tags)
        : store_(store), devices_(devices), tags_(tags) {}

        template<typename Callback>
        std::optional<MessageRef> route_message(AccountId sender_id, DeviceId sender_device, TagId tag_id, MessageType msg_type, uint8_t flags, Timestamp timestamp, const uint8_t* payload, uint32_t payload_size, Callback&& cb) {
            if (!tags_.can_write(tag_id, sender_id)) return std::nullopt;
            MessageRef ref = store_.store_and_ref(sender_id, tag_id, msg_type, flags, timestamp, payload, payload_size);
            for (auto account : tags_.get_members(tag_id)) {
                if (account == sender_id) devices_.fan_out(account, ref, std::forward<Callback>(cb), sender_device);
                else devices_.fan_out(account, ref, std::forward<Callback>(cb));
            }
            return ref;
        }
    };
}