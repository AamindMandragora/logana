#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>

#include "logana/relay_router.hpp"

using namespace logana;

// captures deliveries from fan-out for verification
struct Delivery {
    Connection connection;
    MessageRef ref;
};

// helper to make a payload from a string
const uint8_t* make_payload(const char* str) {
    return reinterpret_cast<const uint8_t*>(str);
}

// tests basic routing between two connected accounts in a tag
void test_route_basic() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    // set up two accounts with one device each
    DeviceId d1 = devices.register_device(100);
    DeviceId d2 = devices.register_device(200);
    devices.connect_device(d1, 10);
    devices.connect_device(d2, 20);

    // connect accounts, create tag, add member
    tags.add_connection(100, 200);
    tags.create_tag(1, 100);
    tags.add_member(1, 100, 200);

    // route a message from 100
    std::vector<Delivery> deliveries;
    const char* text = "hello";
    auto result = router.route_message(100, d1, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    // routing succeeded
    assert(result.has_value());
    assert(result->sender_id == 100);
    assert(result->tag_id == 1);

    // 200's device received the ref
    assert(deliveries.size() == 1);
    assert(deliveries[0].connection == 20);
    assert(deliveries[0].ref.sender_id == 100);
    assert(deliveries[0].ref.tag_id == 1);

    std::cout << "test_route_basic passed" << std::endl;
}

// tests that routing fails when sender lacks write permission
void test_route_permission_denied() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    // set up two accounts with one device each
    DeviceId d1 = devices.register_device(100);
    DeviceId d2 = devices.register_device(200);
    devices.connect_device(d1, 10);
    devices.connect_device(d2, 20);

    // connect accounts, create tag, add 200 but not fully connected and has no FLAG_WRITER
    tags.add_connection(100, 200);
    tags.add_connection(100, 300);
    tags.create_tag(1, 100);
    tags.add_member(1, 100, 200);
    tags.add_member(1, 100, 300);

    // 200 tries to route and denied because not fully connected and no write permission
    std::vector<Delivery> deliveries;
    const char* text = "hello";
    auto result = router.route_message(200, d2, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    assert(!result.has_value());
    assert(deliveries.empty());

    std::cout << "test_route_permission_denied passed" << std::endl;
}

// tests that the sender's originating device is skipped during fan-out
void test_route_skips_origin_device() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    // account 100 has two devices
    DeviceId d1 = devices.register_device(100);
    DeviceId d2 = devices.register_device(100);
    devices.connect_device(d1, 10);
    devices.connect_device(d2, 20);

    // 100 creates a tag (just itself)
    tags.create_tag(1, 100);

    // route from d1 to d1 should be skipped, d2 should receive
    std::vector<Delivery> deliveries;
    const char* text = "hello";
    auto result = router.route_message(100, d1, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    assert(result.has_value());
    assert(deliveries.size() == 1);
    assert(deliveries[0].connection == 20);

    std::cout << "test_route_skips_origin_device passed" << std::endl;
}

// tests that sender's other devices receive the ref
void test_route_sender_other_devices() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    // account 100 has three devices
    DeviceId d1 = devices.register_device(100);
    DeviceId d2 = devices.register_device(100);
    DeviceId d3 = devices.register_device(100);
    devices.connect_device(d1, 10);
    devices.connect_device(d2, 20);
    devices.connect_device(d3, 30);

    tags.create_tag(1, 100);

    // route from d1 to d2 and d3 should receive
    std::vector<Delivery> deliveries;
    const char* text = "sync";
    auto result = router.route_message(100, d1, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    assert(result.has_value());
    assert(deliveries.size() == 2);

    std::cout << "test_route_sender_other_devices passed" << std::endl;
}

// tests that offline devices don't receive the callback
void test_route_offline_devices_skipped() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    DeviceId d1 = devices.register_device(100);
    devices.register_device(200);
    devices.connect_device(d1, 10);
    // d2 stays offline

    tags.add_connection(100, 200);
    tags.create_tag(1, 100);
    tags.add_member(1, 100, 200);

    // no delivery from 100 to 200's offline device
    std::vector<Delivery> deliveries;
    const char* text = "hello";
    auto result = router.route_message(100, d1, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    // routing succeeds (message is stored) but no deliveries
    assert(result.has_value());
    assert(deliveries.empty());

    std::cout << "test_route_offline_devices_skipped passed" << std::endl;
}

// tests fan-out to multiple members in a shared tag
void test_route_shared_tag_fanout() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    // three accounts, one device each
    DeviceId d1 = devices.register_device(100);
    DeviceId d2 = devices.register_device(200);
    DeviceId d3 = devices.register_device(300);
    devices.connect_device(d1, 10);
    devices.connect_device(d2, 20);
    devices.connect_device(d3, 30);

    // fully connect all three
    tags.add_connection(100, 200);
    tags.add_connection(100, 300);
    tags.add_connection(200, 300);
    tags.create_tag(1, 100);
    tags.add_member(1, 100, 200);
    tags.add_member(1, 200, 300);

    // route from 100 to 200 and 300 receive
    std::vector<Delivery> deliveries;
    const char* text = "group message";
    auto result = router.route_message(100, d1, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    assert(result.has_value());
    assert(deliveries.size() == 2);

    std::cout << "test_route_shared_tag_fanout passed" << std::endl;
}

// tests routing to a nonexistent tag
void test_route_nonexistent_tag() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    DeviceId d1 = devices.register_device(100);
    devices.connect_device(d1, 10);

    std::vector<Delivery> deliveries;
    const char* text = "hello";
    auto result = router.route_message(100, d1, 999, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    assert(!result.has_value());
    assert(deliveries.empty());

    std::cout << "test_route_nonexistent_tag passed" << std::endl;
}

// tests that the message is fetchable from the store after routing
void test_route_message_fetchable() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    DeviceId d1 = devices.register_device(100);
    devices.connect_device(d1, 10);
    tags.create_tag(1, 100);

    const char* text = "stored message";
    std::vector<Delivery> deliveries;
    auto result = router.route_message(100, d1, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text), [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    assert(result.has_value());

    // fetch from store using the ref
    auto fetched = store.fetch(100, result->sequence);
    assert(fetched.has_value());
    assert(fetched->header.sender_id == 100);
    assert(fetched->header.payload_size == strlen(text));
    assert(std::memcmp(fetched->payload, text, strlen(text)) == 0);

    std::cout << "test_route_message_fetchable passed" << std::endl;
}

// tests that recipient device IDs don't collide with sender's skipped device
void test_route_no_cross_account_skip() {
    MessageStore store;
    DeviceRegistry devices;
    TagRegistry tags;
    RelayRouter router(store, devices, tags);

    // register 100's device first (gets id 1), then 200's device (gets id 2)
    DeviceId d1 = devices.register_device(100);
    DeviceId d2 = devices.register_device(200);
    devices.connect_device(d1, 10);
    devices.connect_device(d2, 20);

    tags.add_connection(100, 200);
    tags.create_tag(1, 100);
    tags.add_member(1, 100, 200);

    // route from d1 to d2 should NOT be skipped even though skip is active
    std::vector<Delivery> deliveries;
    const char* text = "hello";
    auto result = router.route_message(100, d1, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text),
        [&](Connection conn, MessageRef ref) { deliveries.push_back({conn, ref}); });

    assert(result.has_value());
    assert(deliveries.size() == 1);
    assert(deliveries[0].connection == 20);

    std::cout << "test_route_no_cross_account_skip passed" << std::endl;
}

int main() {
    test_route_basic();
    test_route_permission_denied();
    test_route_skips_origin_device();
    test_route_sender_other_devices();
    test_route_offline_devices_skipped();
    test_route_shared_tag_fanout();
    test_route_nonexistent_tag();
    test_route_message_fetchable();
    test_route_no_cross_account_skip();

    std::cout << std::endl << "All relay router tests passed!" << std::endl;
    return 0;
}
