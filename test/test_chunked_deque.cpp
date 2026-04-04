#include <cassert>
#include <iostream>
#include <thread>
#include <cstring>

#include "logana/chunked_deque.hpp"

using namespace logana;

// creates a dummy message
Message make_message(uint32_t sender_id, uint32_t tag_id, const char* text) {
    Message msg;
    msg.header.sender_id = sender_id;
    msg.header.tag_id = tag_id;
    msg.header.timestamp = 1000 + sender_id;
    msg.header.payload_size = static_cast<uint32_t>(strlen(text));
    msg.header.msg_type = MessageType::Text;
    msg.header.flags = FLAG_NONE;
    msg.payload = new uint8_t[strlen(text)];
    memcpy(msg.payload, text, strlen(text));
    return msg;
}

// tests pushing a single message to the deque
void test_push_single() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;
    // pushes hello message to deque
    uint64_t seq = deque.push(make_message(1, 10, "hello"));
    // asserts it's the zeroth message, next message will be first, there's one chunk in deque
    assert(seq == 0);
    (void)seq;
    assert(deque.get_next_sequence() == 1);
    assert(deque.get_chunk_count() == 1);

    // reads the message from deque
    Message* m = deque.read(0);
    // asserts message exists, has correct fields
    assert(m != nullptr);
    assert(m->header.sender_id == 1);
    assert(m->header.tag_id == 10);
    assert(m->header.sequence == 0);
    assert(m->header.payload_size == 5);
    (void)m;

    std::cout << "test_push_single passed" << std::endl;
}

// tests pushing many messages to the deque
void test_push_many() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // pushes twenty dummy messages
    for (uint32_t i = 0; i < 20; i++) {
        uint64_t seq = deque.push(make_message(i, 100, "msg"));
        // asserts each message is the ith message 
        assert(seq == i);
        (void)seq;
    }

    // asserts that the next message will be twentieth and we made five chunks (20 / 4)
    assert(deque.get_next_sequence() == 20);
    assert(deque.get_chunk_count() == 5);

    // reads each message
    for (uint64_t i = 0; i < 20; i++) {
        Message* m = deque.read(i);
        // asserts the message exists and has correct fields
        assert(m != nullptr);
        assert(m->header.sequence == i);
        assert(m->header.sender_id == static_cast<uint32_t>(i));
        (void)m;
    }

    std::cout << "test_push_many passed" << std::endl;
}

// tests attempted read out of bounds
void test_read_out_of_bounds() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // checks out-of-bounds reads fail
    assert(deque.read(0) == nullptr);
    assert(deque.read(999) == nullptr);

    // pushes new message
    (void)deque.push(make_message(1, 10, "hello"));
    // checks if out-of-bounds reads fail
    assert(deque.read(1) == nullptr);
    assert(deque.read(100) == nullptr);

    std::cout << "test_read_out_of_bounds passed" << std::endl;
}

// tests tombstoning a message
void test_tombstone() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // creates five dummy messages
    for (int i = 0; i < 5; i++) {
        (void)deque.push(make_message(i, 10, "msg"));
    }

    // tombstones the message with sequence 2
    deque.tombstone(2);

    // attempts to read sequence 2 message
    Message* m = deque.read(2);
    // asserts read fail and message was tombstoned
    assert(m != nullptr);
    assert(m->is_deleted());
    (void)m;

    // attempts to read sequence 1 message
    Message* m1 = deque.read(1);
    // asserts read succeeded and message wasn't tombstoned
    assert(m1 != nullptr);
    assert(!m1->is_deleted());
    (void)m1;

    std::cout << "test_tombstone passed" << std::endl;
}

// checks that adding a message when chunk is full will create a new chunk count
void test_chunk_boundary() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // writes four messages, filling a chunk
    for (int i = 0; i < 4; i++) {
        (void)deque.push(make_message(i, 10, "msg"));
    }
    // asserts only one chunk exists
    assert(deque.get_chunk_count() == 1);

    // pushes extra message
    (void)deque.push(make_message(99, 10, "overflow"));
    // asserts new chunk created
    assert(deque.get_chunk_count() == 2);

    // attempts to read extra message
    Message* m = deque.read(4);
    // assert message exists and has correct fields
    assert(m != nullptr);
    assert(m->header.sender_id == 99);
    assert(m->header.sequence == 4);
    (void)m;

    std::cout << "test_chunk_boundary passed" << std::endl;
}

// tests reading a range of messages
void test_read_range() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // writes ten messages
    for (uint32_t i = 0; i < 10; i++) {
        (void)deque.push(make_message(i, 50, "data"));
    }

    // attempts to read the first ten messages
    auto refs = deque.read_range(0, 10);
    // checks if we got ten message sback
    assert(refs.size() == 10);
    for (size_t i = 0; i < refs.size(); i++) {
        // asserts each message exists and has the correct fields and is in the correct order
        assert(refs[i].sequence == i);
        assert(refs[i].sender_id == static_cast<uint32_t>(i));
        assert(refs[i].tag_id == 50);
    }

    std::cout << "test_read_range passed" << std::endl;
}

// tests if reading the range skips tombstoned messages
void test_read_range_skips_tombstoned() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // writes ten messages
    for (uint32_t i = 0; i < 10; i++) {
        (void)deque.push(make_message(i, 50, "data"));
    }

    // tombstones two messages
    deque.tombstone(3);
    deque.tombstone(7);

    // attempts to read the first 100 messages
    auto refs = deque.read_range(0, 100);
    // checks that we got the correct number of messages back
    assert(refs.size() == 8);
    for (auto& ref : refs) {
        // asserts that we didn't get one of the tombstoned messages
        assert(ref.sequence != 3);
        assert(ref.sequence != 7);
        (void)ref;
    }

    std::cout << "test_read_range_skips_tombstoned passed" << std::endl;
}

// tests if read range returns messages from multiple chunks
void test_read_range_across_chunks() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // writes ten messages
    for (uint32_t i = 0; i < 10; i++) {
        (void)deque.push(make_message(i, 50, "data"));
    }

    // attempts to read six messages after index 2 inclusive
    auto refs = deque.read_range(2, 6);
    // asserts we get six messages back, that the first one we get back was 2 and the last was 7
    assert(refs.size() == 6);
    assert(refs[0].sequence == 2);
    assert(refs[5].sequence == 7);

    std::cout << "test_read_range_across_chunks passed" << std::endl;
}

// tests that updating the oldest sequence evicts older messages
void test_eviction() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // writes twelve messages
    for (uint32_t i = 0; i < 12; i++) {
        (void)deque.push(make_message(i, 10, "msg"));
    }
    // asserts that we have three chunks
    assert(deque.get_chunk_count() == 3);

    // makes the oldest sequence four, removing sequences zero through three
    deque.update_oldest_sequence(4);
    // asserts we got rid of a chunk
    assert(deque.get_chunk_count() == 2);
    // asserts we correctly set oldest sequence and that older messages got removed
    assert(deque.read(0) == nullptr);
    assert(deque.read(3) == nullptr);
    assert(deque.read(4) != nullptr);
    assert(deque.get_oldest_sequence() == 4);

    std::cout << "test_eviction passed" << std::endl;
}

// tests that making oldest sequence inside a chunk frees only ones before it
void test_eviction_partial() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // writes twelve messages
    for (uint32_t i = 0; i < 12; i++) {
        (void)deque.push(make_message(i, 10, "msg"));
    }

    // updates the oldest sequence to two
    deque.update_oldest_sequence(2);
    // asserts no chunks or messages got removed as the oldest sequence isn't larger than any chunk
    assert(deque.get_chunk_count() == 3);
    assert(deque.read(0) != nullptr);

    std::cout << "test_eviction_partial passed" << std::endl;
}

// tests that evicting multiple chunks works
void test_eviction_multiple_chunks() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // writes twelve messages
    for (uint32_t i = 0; i < 12; i++) {
        (void)deque.push(make_message(i, 10, "msg"));
    }

    // makes oldest sequence eight, kicking out first two chunks
    deque.update_oldest_sequence(8);
    // assert one chunk left, first two chunks gone and last one untouched
    assert(deque.get_chunk_count() == 1);
    assert(deque.read(7) == nullptr);
    assert(deque.read(8) != nullptr);

    std::cout << "test_eviction_multiple_chunks passed" << std::endl;
}

// tests that empty deque doesn't leak anything
void test_empty_deque() {
    // initializes deque with chunks of four messages
    ChunkedDeque<4> deque;

    // assert can't read, next sequence is oldest sequence is zero, no chunks
    assert(deque.read(0) == nullptr);
    assert(deque.get_next_sequence() == 0);
    assert(deque.get_oldest_sequence() == 0);
    assert(deque.get_chunk_count() == 0);

    // assert can't get any messages because none exist
    auto refs = deque.read_range(0, 100);
    assert(refs.empty());

    // tries to tombstone and update oldest sequence
    deque.tombstone(0);
    deque.update_oldest_sequence(100);

    std::cout << "test_empty_deque passed" << std::endl;
}

// attempts a stress test on deque
void test_stress_spsc() {
    constexpr size_t NUM_MESSAGES = 100000;
    // initializes deque with chunks of 256 messages
    ChunkedDeque<256> deque(512);

    // makes a producer thread that writes 100k messages to the deque
    std::thread producer([&]() {
        for (uint32_t i = 0; i < NUM_MESSAGES; i++) {
            (void)deque.push(make_message(i, 1, "x"));
        }
    });

    // creates atomic variable tracking number of messages read
    std::atomic<uint64_t> consumed{0};

    // makes a producer thread that reads messages until there are nothing left
    std::thread consumer([&]() {
        uint64_t next_to_read = 0;
        while (next_to_read < NUM_MESSAGES) {
            Message* m = deque.read(next_to_read);
            if (m != nullptr) {
                assert(m->header.sequence == next_to_read);
                assert(m->header.sender_id == static_cast<uint32_t>(next_to_read));
                next_to_read++;
                consumed.store(next_to_read, std::memory_order_relaxed);
            }
        }
    });

    // joins both threads
    producer.join();
    consumer.join();

    // assert we read all the messages and the next one would have sequence NUM_MESSAGES
    assert(consumed.load() == NUM_MESSAGES);
    assert(deque.get_next_sequence() == NUM_MESSAGES);

    std::cout << "test_stress_spsc passed" << std::endl;
}

// runs all tests
int main() {
    test_push_single();
    test_push_many();
    test_read_out_of_bounds();
    test_tombstone();
    test_chunk_boundary();
    test_read_range();
    test_read_range_skips_tombstoned();
    test_read_range_across_chunks();
    test_eviction();
    test_eviction_partial();
    test_eviction_multiple_chunks();
    test_empty_deque();
    test_stress_spsc();

    std::cout << std::endl << "All tests passed!" << std::endl;
    return 0;
}