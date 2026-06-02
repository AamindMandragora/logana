#include <cassert>
#include <iostream>
#include <cstring>

#include "logana/message_store.hpp"

using namespace logana;

// helper to make a payload from a string
const uint8_t* make_payload(const char* str) {
    return reinterpret_cast<const uint8_t*>(str);
}

// tests storing a message and getting back a correct ref
void test_store_returns_ref() {
    MessageStore store;
    const char* text = "hello";
    MessageRef ref = store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text));

    assert(ref.sender_id == 100);
    assert(ref.tag_id == 1);
    assert(ref.timestamp == 1000);
    assert(ref.sequence == 0);

    std::cout << "test_store_returns_ref passed" << std::endl;
}

// tests that sequential stores increment the sequence
void test_store_increments_sequence() {
    MessageStore store;
    const char* text = "msg";
    MessageRef r0 = store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text));
    MessageRef r1 = store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1001, make_payload(text), strlen(text));
    MessageRef r2 = store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1002, make_payload(text), strlen(text));

    assert(r0.sequence == 0);
    assert(r1.sequence == 1);
    assert(r2.sequence == 2);

    std::cout << "test_store_increments_sequence passed" << std::endl;
}

// tests that different accounts get independent sequence counters
void test_separate_deques_per_account() {
    MessageStore store;
    const char* text = "msg";
    MessageRef r1 = store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text));
    MessageRef r2 = store.store_and_ref(200, 1, MessageType::Text, FLAG_NONE, 1001, make_payload(text), strlen(text));

    // both start at sequence 0 because they're in separate deques
    assert(r1.sequence == 0);
    assert(r2.sequence == 0);

    std::cout << "test_separate_deques_per_account passed" << std::endl;
}

// tests fetching a stored message returns a valid clone
void test_fetch_returns_clone() {
    MessageStore store;
    const char* text = "hello world";
    store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text));

    auto result = store.fetch(100, 0);
    assert(result.has_value());
    assert(result->header.sender_id == 100);
    assert(result->header.tag_id == 1);
    assert(result->header.timestamp == 1000);
    assert(result->header.payload_size == strlen(text));
    assert(std::memcmp(result->payload, text, strlen(text)) == 0);

    std::cout << "test_fetch_returns_clone passed" << std::endl;
}

// tests fetching from a nonexistent account returns nullopt
void test_fetch_nonexistent_account() {
    MessageStore store;
    auto result = store.fetch(999, 0);
    assert(!result.has_value());

    std::cout << "test_fetch_nonexistent_account passed" << std::endl;
}

// tests fetching a sequence that doesn't exist returns nullopt
void test_fetch_nonexistent_sequence() {
    MessageStore store;
    const char* text = "msg";
    store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1000, make_payload(text), strlen(text));

    auto result = store.fetch(100, 999);
    assert(!result.has_value());

    std::cout << "test_fetch_nonexistent_sequence passed" << std::endl;
}

// tests that evict_before makes earlier messages unfetchable
void test_evict_before() {
    MessageStore store;
    const char* text = "msg";
    // store enough messages to fill at least one chunk boundary
    for (int i = 0; i < 600; i++) {
        store.store_and_ref(100, 1, MessageType::Text, FLAG_NONE, 1000 + i, make_payload(text), strlen(text));
    }

    // evict everything before sequence 512 (first chunk should be freed)
    store.evict_before(100, 512);

    // early messages are gone
    assert(!store.fetch(100, 0).has_value());
    assert(!store.fetch(100, 511).has_value());

    // later messages still exist
    assert(store.fetch(100, 512).has_value());
    assert(store.fetch(100, 599).has_value());

    std::cout << "test_evict_before passed" << std::endl;
}

// tests that evicting from a nonexistent account is a no-op
void test_evict_nonexistent_account() {
    MessageStore store;
    store.evict_before(999, 100);

    std::cout << "test_evict_nonexistent_account passed" << std::endl;
}

// tests that flags are preserved through store and fetch
void test_flags_preserved() {
    MessageStore store;
    const char* text = "encrypted";
    store.store_and_ref(100, 1, MessageType::Text, FLAG_ENCRYPTED, 1000, make_payload(text), strlen(text));

    auto result = store.fetch(100, 0);
    assert(result.has_value());
    assert(result->header.flags & FLAG_ENCRYPTED);
    assert(result->header.msg_type == MessageType::Text);

    std::cout << "test_flags_preserved passed" << std::endl;
}

int main() {
    test_store_returns_ref();
    test_store_increments_sequence();
    test_separate_deques_per_account();
    test_fetch_returns_clone();
    test_fetch_nonexistent_account();
    test_fetch_nonexistent_sequence();
    test_evict_before();
    test_evict_nonexistent_account();
    test_flags_preserved();

    std::cout << std::endl << "All message store tests passed!" << std::endl;
    return 0;
}