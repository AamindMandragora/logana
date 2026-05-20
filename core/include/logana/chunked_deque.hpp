#pragma once

#include <atomic>
#include <vector>
#include <memory>

#include "types.hpp"

namespace logana {
    // templating chunk to use any size we want
    template <size_t size = 256>
    class Chunk {
        // a constant-size array of messages
        Message messages_[size];
        // the smallest sequence stored in this chunk
        Sequence base_sequence_;
        // total number of messages appended to the chunk
        std::atomic<uint64_t> lifetime_count_{0};
        // the number of currently alive (non-tombstoned) messages
        uint64_t live_messages_{0};
    public:
        // the constructor that takes the base sequence
        Chunk(Sequence base) : base_sequence_(base) {}

        // checks if we've filled the array
        bool is_full() const {
            return lifetime_count_.load(std::memory_order_acquire) == size;
        }

        // appends the rvalue message to the array (means this message can't exist anywhere else)
        void append(Message&& message) {
            // acquires the lifetime count
            uint64_t w = lifetime_count_.load(std::memory_order_acquire);
            // sets the index of the message to base + lifetime
            message.header.sequence = get_base_sequence() + w;
            // steals message and stores it in array
            messages_[w] = std::move(message);
            // increments lifetime count
            lifetime_count_.fetch_add(1, std::memory_order_release);
            // increments live messages
            live_messages_++;
        }

        Message *get(Sequence sequence) {
            // checks if input sequence is within bounds
            if (get_base_sequence() <= sequence && sequence < get_end_sequence()) {
                // calculates the relative index
                uint64_t idx = sequence - get_base_sequence();
                // returns pointer to the message
                return messages_ + idx;
            }
            // otherwise returns nullptr
            return nullptr;
        }

        void tombstone(Sequence sequence) {
            // finds the message corresponding to sequence
            Message *message = get(sequence);
            // if none exists return early
            if (message == nullptr) return;
            // tombstone the message
            message->mark_deleted();
            // no longer alive
            live_messages_--;
        }

        // returns whether no live messages exist
        bool is_empty_live() const {
            return live_messages_ == 0;
        }

        // getter for the base index
        Sequence get_base_sequence() const {
            return base_sequence_;
        }

        // getter for the end sequence
        Sequence get_end_sequence() const {
            return base_sequence_ + lifetime_count_.load(std::memory_order_acquire);
        }
    };

    // more templating to pass to chunk
    template <size_t size = 256>
    class ChunkedDeque {
        // dynamic vector holding unique pointers to chunks
        std::vector<std::unique_ptr<Chunk<size>>> chunks;
        // the index the next message will be assigned
        std::atomic<Sequence> next_sequence_{0};
        // stores the oldest index in the deque
        Sequence oldest_sequence_{0};
    public:
        // constructor that reserves space in vector
        ChunkedDeque(size_t init_cap = 64) {
            chunks.reserve(init_cap);
        }
        // push rvalue message to queue, output must be used
        [[nodiscard]] Sequence push(Message&& message) {
            // gets the index to be assigned
            Sequence next = get_next_sequence();
            // need to make a new chunk if the last one doesn't exist or is full
            if (chunks.empty() || chunks.back()->is_full()) {
                chunks.emplace_back(std::make_unique<Chunk<size>>(next));
            }
            // adds message to chunk
            chunks.back()->append(std::move(message));
            // increments next sequence
            next_sequence_.fetch_add(1, std::memory_order_release);
            return next;
        }

        // returns the message associated with the sequence
        Message *read(Sequence sequence) {
            // there has to be a chunk and the sequence has to be within bounds
            if (!chunks.empty() && chunks.front()->get_base_sequence() <= sequence && sequence < get_next_sequence()) {
                // calculate relative index
                uint64_t idx = (sequence - chunks.front()->get_base_sequence()) / size;
                // if idx within bounds return
                if (idx < get_chunk_count()) return chunks[idx]->get(sequence);
            }
            // else return nullptr
            return nullptr;
        }

        // tombstones the message associated with the sequence
        void tombstone(Sequence sequence) {
            // there has to be a chunk and the sequence has to be within bounds
            if (!chunks.empty() && chunks.front()->get_base_sequence() <= sequence && sequence < get_next_sequence()) {
                // calculate relative index
                uint64_t idx = (sequence - chunks.front()->get_base_sequence()) / size;
                // if idx within bounds tombstone
                if (idx < get_chunk_count()) chunks[idx]->tombstone(sequence);
            }
        }

        // gets a vector of count references to messages starting at sequence start
        std::vector<MessageRef> read_range(Sequence start, size_t count) {
            // initialize return vector
            std::vector<MessageRef> to_return;
            // can't exceed the to-be-assigned sequence
            Sequence next = get_next_sequence();
            // loops while we're within bounds and have less than count messages
            for (Sequence i = start; i < next && to_return.size() < count; i++) {
                // reads the message with sequence i
                Message *m = read(i);
                // if the message exists construct a reference and add to the vector
                if (m != nullptr && !m->is_deleted()) {
                    MessageRef mr;
                    mr.sequence = m->header.sequence;
                    mr.timestamp = m->header.timestamp;
                    mr.sender_id = m->header.sender_id;
                    mr.tag_id = m->header.tag_id;
                    to_return.emplace_back(mr);
                }
            }
            // returns the vector
            return to_return;
        }

        // gets the number of chunks
        size_t get_chunk_count() const {
            return chunks.size();
        }

        // updates the oldest sequence and deletes all older messages
        void update_oldest_sequence(Sequence older) {
            oldest_sequence_ = older;
            while (!chunks.empty() && chunks.front()->get_end_sequence() <= oldest_sequence_) {
                chunks.erase(chunks.begin());
            }
        }

        // gets the to-be-assigned sequence
        Sequence get_next_sequence() const {
            return next_sequence_.load(std::memory_order_acquire);
        }

        // gets the oldest sequence
        Sequence get_oldest_sequence() const {
            if (chunks.empty()) return get_next_sequence();
            return chunks.front()->get_base_sequence();
        }
    };
}