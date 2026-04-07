#include <cassert>
#include <iostream>
#include <vector>

#include "logana/device_registry.hpp"

using namespace logana;

// tests registering devices under accounts
void test_register_device() {
    DeviceRegistry registry;
    // registers two devices to account 100 and one to account 200
    uint32_t d1 = registry.register_device(100);
    uint32_t d2 = registry.register_device(100);
    uint32_t d3 = registry.register_device(200);

    // asserts none of the devices are the same
    assert(d1 != d2);
    assert(d2 != d3);

    (void)d1;
    (void)d2;
    (void)d3;

    // gets the devices for account 100 and asserts that there are two of them
    auto devices_100 = registry.get_account_devices(100);
    assert(devices_100.size() == 2);

    // gets the devices for account 200 and asserts that there's only one
    auto devices_200 = registry.get_account_devices(200);
    assert(devices_200.size() == 1);

    std::cout << "test_register_device passed" << std::endl;
}

// tests getting the devices for a nonexistent account
void test_get_account_devices_nonexistent() {
    DeviceRegistry registry;
    // gets the vector of devices from fake account and asserts that there are none
    auto devices = registry.get_account_devices(999);
    assert(devices.empty());

    std::cout << "test_get_account_devices_nonexistent passed" << std::endl;
}

// tests connecting a device to a file descriptor
void test_connect_device() {
    DeviceRegistry registry;
    // registers device under account 100
    uint32_t d1 = registry.register_device(100);

    // gets the object associated with the device id, asserts that it exists and is offline
    DeviceState* state = registry.get_device(d1);
    assert(state != nullptr);
    assert(!state->is_online());

    // asserts that the device can be connected to fd 42
    assert(registry.connect_device(d1, 42));

    // regrabs the device state, assert that it is online and connected to the right fd
    state = registry.get_device(d1);
    assert(state->is_online());
    assert(state->connection == 42);

    (void)state;

    std::cout << "test_connect_device passed" << std::endl;
}

// test connecting a device on a nonexistent device
void test_connect_nonexistent_device() {
    DeviceRegistry registry;
    // asserts that you can't connect a fake device to an fd
    assert(!registry.connect_device(999, 42));

    std::cout << "test_connect_nonexistent_device passed" << std::endl;
}

// tests disconnecting a decvice
void test_disconnect_device() {
    DeviceRegistry registry;
    // registers and connects device 100 to fd 42
    uint32_t d1 = registry.register_device(100);
    registry.connect_device(d1, 42);

    // disconnects device
    assert(registry.disconnect_device(d1));

    // gets the object associated with the device and asserts offline
    DeviceState* state = registry.get_device(d1);
    assert(!state->is_online());

    (void)state;

    std::cout << "test_disconnect_device passed" << std::endl;
}

// tests disconnecting a nonexistent device
void test_disconnect_nonexistent_device() {
    DeviceRegistry registry;
    // asserts that you can't disconnect a device that doesn't exist
    assert(!registry.disconnect_device(999));

    std::cout << "test_disconnect_nonexistent_device passed" << std::endl;
}

// tests getting a device
void test_get_device() {
    DeviceRegistry registry;
    // registers device to account 100
    uint32_t d1 = registry.register_device(100);

    // gets the state of the device and asserts that the fields are correct
    DeviceState* state = registry.get_device(d1);
    assert(state != nullptr);
    assert(state->device_id == d1);
    assert(state->account_id == 100);

    (void)state;

    std::cout << "test_get_device passed" << std::endl;
}

// tests getting a nonexistent device
void test_get_device_nonexistent() {
    DeviceRegistry registry;
    // asserts getting a fake device returns nullptr
    assert(registry.get_device(999) == nullptr);

    std::cout << "test_get_device_nonexistent passed" << std::endl;
}

// tests updating the sequence of several devices
void test_update_sequence() {
    DeviceRegistry registry;
    // registers a device to account 100 and updates its sequence of sender 50 to 100
    uint32_t d1 = registry.register_device(100);

    registry.update_sequence(d1, 50, 100);

    // gets the state of the device and assert that user 50 exists and its sequence is 100
    DeviceState* state = registry.get_device(d1);
    assert(state->read_sequences.contains(50));
    assert(state->read_sequences[50] == 100);

    // reupdates the sequence of user 50 to 200 and asserts the change took effect
    registry.update_sequence(d1, 50, 200);
    state = registry.get_device(d1);
    assert(state->read_sequences[50] == 200);

    (void)state;

    std::cout << "test_update_sequence passed" << std::endl;
}

// tests handling of updating the sequence of a fake device
void test_update_sequence_nonexistent_device() {
    DeviceRegistry registry;
    registry.update_sequence(999, 50, 100);

    std::cout << "test_update_sequence_nonexistent_device passed" << std::endl;
}

// tests getting the oldest sequence among devices
void test_get_oldest_sequence() {
    DeviceRegistry registry;
    // registers three devices to three different accounts
    uint32_t d1 = registry.register_device(100);
    uint32_t d2 = registry.register_device(200);
    uint32_t d3 = registry.register_device(300);

    // updates the sequence of account 50 to 100, 50, 200 respectively
    registry.update_sequence(d1, 50, 100);
    registry.update_sequence(d2, 50, 50);
    registry.update_sequence(d3, 50, 200);

    // asserts that the smallest sequence of user 50 is 50
    assert(registry.get_oldest_sequence(50) == 50);

    std::cout << "test_get_oldest_sequence passed" << std::endl;
}

// tests getting the oldest sequence among no devices
void test_get_oldest_sequence_no_cursors() {
    DeviceRegistry registry;
    registry.register_device(100);

    // asserts that the smallest sequence of user 50 is the maximum integer, as no device is reading it
    assert(registry.get_oldest_sequence(50) == UINT64_MAX);

    std::cout << "test_get_oldest_sequence_no_cursors passed" << std::endl;
}

// tests getting the oldest sequence with only one device
void test_get_oldest_sequence_single_device() {
    DeviceRegistry registry;
    // registers a device and updates its sequence for user 50 to 75
    uint32_t d1 = registry.register_device(100);
    registry.update_sequence(d1, 50, 75);

    // asserts that the oldests equence is75
    assert(registry.get_oldest_sequence(50) == 75);

    std::cout << "test_get_oldest_sequence_single_device passed" << std::endl;
}

// tests fan out with online devices
void test_fan_out_online_devices() {
    DeviceRegistry registry;
    // registers devices to user 100
    uint32_t d1 = registry.register_device(100);
    uint32_t d2 = registry.register_device(100);

    // connects both devices to file descriptors
    registry.connect_device(d1, 10);
    registry.connect_device(d2, 20);

    // creates a message reference sent by user 200
    MessageRef ref{};
    ref.sender_id = 200;
    ref.tag_id = 1;
    ref.sequence = 0;
    ref.timestamp = 1000;

    // creates a vector storing the file descriptors for each device that received the message and used that as the callback function for fan_out
    std::vector<int64_t> pushed_to;
    registry.fan_out(100, ref, [&](int64_t conn, MessageRef) {
        pushed_to.push_back(conn);
    });

    // assert two fds got the message
    assert(pushed_to.size() == 2);

    std::cout << "test_fan_out_online_devices passed" << std::endl;
}

// test that fan out skips offline devices
void test_fan_out_skips_offline() {
    DeviceRegistry registry;
    // registers two devices to account 100
    uint32_t d1 = registry.register_device(100);
    uint32_t d2 = registry.register_device(100);

    (void)d2;

    // connects first device to fd 10
    registry.connect_device(d1, 10);

    // creates a message from account 200
    MessageRef ref{};
    ref.sender_id = 200;
    ref.tag_id = 1;
    ref.sequence = 0;
    ref.timestamp = 1000;

    // creates a vector storing the file descriptors for each device that received the message and used that as the callback function for fan_out
    std::vector<int64_t> pushed_to;
    registry.fan_out(100, ref, [&](int64_t conn, MessageRef) {
        pushed_to.push_back(conn);
    });

    // assert we pushed to one thing, fd 10
    assert(pushed_to.size() == 1);
    assert(pushed_to[0] == 10);

    std::cout << "test_fan_out_skips_offline passed" << std::endl;
}

// test fan out with a device to skip
void test_fan_out_skips_device() {
    DeviceRegistry registry;
    // register two devices under account 100
    uint32_t d1 = registry.register_device(100);
    uint32_t d2 = registry.register_device(100);

    // connects both devices to file descriptors
    registry.connect_device(d1, 10);
    registry.connect_device(d2, 20);

    // creates a reference to a message from account 200
    MessageRef ref{};
    ref.sender_id = 200;
    ref.tag_id = 1;
    ref.sequence = 0;
    ref.timestamp = 1000;

    // stores the fds pushed to and performs fan out
    std::vector<int64_t> pushed_to;
    registry.fan_out(100, ref, [&](int64_t conn, MessageRef) {
        pushed_to.push_back(conn);
    }, d1);

    // assert only the second device got the message
    assert(pushed_to.size() == 1);
    assert(pushed_to[0] == 20);

    std::cout << "test_fan_out_skips_device passed" << std::endl;
}

// test fanning out to devices on a fake account
void test_fan_out_nonexistent_account() {
    DeviceRegistry registry;

    // creates a message reference from account 200
    MessageRef ref{};
    ref.sender_id = 200;
    ref.tag_id = 1;
    ref.sequence = 0;
    ref.timestamp = 1000;

    // sends ref to all devices under fake account 99
    int count = 0;
    registry.fan_out(999, ref, [&](int64_t, MessageRef) {
        count++;
    });

    // asserts no device got the ref
    assert(count == 0);

    std::cout << "test_fan_out_nonexistent_account passed" << std::endl;
}

// tests fan out to devices on an account except they're all offline
void test_fan_out_all_offline() {
    DeviceRegistry registry;
    // registers two devices to account 100
    registry.register_device(100);
    registry.register_device(100);

    // creates a message reference from account 200
    MessageRef ref{};
    ref.sender_id = 200;
    ref.tag_id = 1;
    ref.sequence = 0;
    ref.timestamp = 1000;

    // sends the ref to every device on account 100
    int count = 0;
    registry.fan_out(100, ref, [&](int64_t, MessageRef) {
        count++;
    });

    // asserts no device got the ref
    assert(count == 0);

    std::cout << "test_fan_out_all_offline passed" << std::endl;
}

// tests fan out when there are multiple different accounts in the registry
void test_multiple_accounts_independent() {
    DeviceRegistry registry;
    // registers two devices, one each to accounts 100 and 200
    uint32_t d1 = registry.register_device(100);
    uint32_t d2 = registry.register_device(200);

    // connects both devices
    registry.connect_device(d1, 10);
    registry.connect_device(d2, 20);

    // creates a reference to a message sent from account 300
    MessageRef ref{};
    ref.sender_id = 300;
    ref.tag_id = 1;
    ref.sequence = 0;
    ref.timestamp = 1000;

    // sends the message to every device in account 100
    std::vector<int64_t> pushed_to;
    registry.fan_out(100, ref, [&](int64_t conn, MessageRef) {
        pushed_to.push_back(conn);
    });

    // asserts we only pushed to the one device of account 100
    assert(pushed_to.size() == 1);
    assert(pushed_to[0] == 10);

    std::cout << "test_multiple_accounts_independent passed" << std::endl;
}

// runs all tests
int main() {
    test_register_device();
    test_get_account_devices_nonexistent();
    test_connect_device();
    test_connect_nonexistent_device();
    test_disconnect_device();
    test_disconnect_nonexistent_device();
    test_get_device();
    test_get_device_nonexistent();
    test_update_sequence();
    test_update_sequence_nonexistent_device();
    test_get_oldest_sequence();
    test_get_oldest_sequence_no_cursors();
    test_get_oldest_sequence_single_device();
    test_fan_out_online_devices();
    test_fan_out_skips_offline();
    test_fan_out_skips_device();
    test_fan_out_nonexistent_account();
    test_fan_out_all_offline();
    test_multiple_accounts_independent();

    std::cout << std::endl << "All device registry tests passed!" << std::endl;
    return 0;
}