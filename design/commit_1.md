# Commit 1: Project Skeleton & Chunked Deque

## The Problem

Every messaging system needs a place to put messages before they reach their destination. When someone sends a message and the recipient is offline, that message has to live *somewhere* until the recipient comes back. The data structure holding those messages is, in many ways, the heartbeat of the entire relay.

So what do we actually need from this buffer? Let's think about the access pattern. Messages arrive one at a time, always appended to the end. Recipients read them sequentially, roughly in order. Once every recipient has read a message, we don't need it anymore and can throw it away, but only from the front, since older messages are consumed first. We never insert into the middle, we never reorder, and we almost never need random access.

Append to the back. Read sequentially. Free from the front. That's the contract.

## Why Not a Ring Buffer?

The first instinct might be a ring buffer, a fixed-size array with head and tail pointers that wrap around. Ring buffers are beautiful in their simplicity. One contiguous allocation, no pointer chasing, and the branch predictor loves the arithmetic. They're the go-to structure in audio processing, kernel drivers, and lock-free producer-consumer queues.

But there's a catch: ring buffers are *fixed-size*. When they fill up, they overwrite the oldest data. For a network card's receive buffer, that's fine, you *want* backpressure. For a messaging relay, it means silently dropping messages that someone hasn't read yet. That's unacceptable.

We could make the ring buffer growable, but then we'd need to reallocate and copy the entire contents on resize. That invalidates any pointers or references into the old buffer, which breaks our architecture, recipients hold lightweight `MessageRef` pointers that reference messages by sequence number and need stable storage behind them.

## Why Not a Linked List?

A linked list gives us O(1) append and O(1) front removal without any of the fixed-size problems. But every message is a separate heap allocation scattered across memory, and every traversal chases a pointer to some random location. Our primary read pattern, sequential scan during replay, would be a cache miss on virtually every message. On modern hardware, cache misses dominate performance. A linked list optimizes for the wrong thing.

## The Chunked Deque

The chunked deque is the middle ground. Instead of one big array (ring buffer) or one node per message (linked list), we allocate fixed-size *chunks*, contiguous arrays of, say, 256 message slots each. A vector holds pointers to these chunks.

Within a chunk, messages are laid out contiguously in memory. Sequential reads within a chunk are cache-friendly, just like a ring buffer. When a chunk fills up, we allocate a new one at the tail, no copying, no invalidation of existing data. When all messages in a front chunk have been consumed, we free the entire chunk in one operation.

Lookup is O(1) via arithmetic. Every chunk holds exactly N messages, and sequence numbers are contiguous, so given a sequence number, the chunk index is just `(sequence - first_chunk_base) / chunk_size`, and the slot within the chunk is `sequence % chunk_size`. No binary search, no tree traversal, just integer division.

## Making It Lock-Free

In the relay, each sender account owns one deque. One thread writes incoming messages into it (the producer), and another thread reads messages out to fan out refs to recipients (the consumer). This is single-producer single-consumer, the simplest possible concurrent access pattern, and one where we can avoid locks entirely.

The key insight is that the producer and consumer are operating on different ends of the data. The producer writes to the tail, the consumer reads from wherever its cursor is, which is always at or behind the tail. They never touch the same slot simultaneously, as long as the consumer can reliably know *how far* the producer has written.

That's where the atomic counter comes in. The producer writes the message data into the slot *first*, then increments the write counter with `memory_order_release`. The consumer reads the counter with `memory_order_acquire`, and if it's advanced, the message data is guaranteed to be visible. Release says "everything I wrote before this point is now committed." Acquire says "show me everything that was committed before this point." Together, they form a happens-before relationship without any mutex, any syscall, or any cache line bouncing beyond the single counter.

Why not `memory_order_seq_cst`, the default and strongest ordering? Because seq_cst enforces a *total global order* across all atomic operations on all threads, a much stronger (and more expensive) guarantee than we need. We only need a pairwise ordering between one producer and one consumer. Acquire/release gives us exactly that and nothing more.

## What Was Built

**types.hpp** defines the core data types:

- `MessageHeader`: a 64-byte, cache-line-aligned struct. `alignas(64)` ensures each header sits on its own cache line, preventing false sharing when different threads access adjacent messages. It's trivially copyable (enforced by `static_assert`), meaning it can be safely `memcpy`'d and used in contexts that require plain data with no hidden behavior.

- `MessageRef`: a 24-byte lightweight pointer containing just enough to identify a message: sender ID, sequence number, tag ID, and timestamp. This is what gets fanned out to recipients instead of the full message.

- `Message`: owns a header and a heap-allocated payload. Move-only (copy is deleted) because duplicating potentially large payloads would be expensive and error-prone.

- Flags are `constexpr uint8_t` values instead of an `enum class` so they can be bitwise-OR'd together: `FLAG_DELETED` for tombstoning, `FLAG_EPHEMERAL` for auto-expiring messages, `FLAG_ENCRYPTED` for E2E encrypted payloads.

**chunked_deque.hpp** implements two class templates:

- `Chunk<size>`: the fixed-size array of message slots with an atomic write counter and a live message count for tombstone tracking.

- `ChunkedDeque<size>`: the growable container of chunks, providing push, read, tombstone, range_read, cursor-based eviction, and sequence management.

**test_chunked_deque.cpp** has 13 tests: 12 single-threaded correctness tests covering push, read, out-of-bounds access, tombstoning, chunk boundaries, range reads, eviction (full, partial, and multi-chunk), and empty deque behavior; plus one multi-threaded SPSC stress test that pushes 100K messages from a producer thread while a consumer thread reads and verifies every single one.

## Tombstoning

When a message is "deleted," we don't physically remove it from the chunk. We flip a flag bit and decrement a live counter. The slot stays occupied.

This sounds wasteful, but consider the alternative: actually removing a message from the middle of a contiguous array means either shifting everything after it (O(n) and invalidates our sequence-to-index arithmetic) or leaving a hole and maintaining a free list (complexity explosion for minimal gain).

The space gets reclaimed naturally. When the eviction cursor advances past a chunk, the entire chunk is freed regardless of which individual messages were tombstoned. The `live_messages_` counter per chunk exists for a future optimization: if every message in a chunk has been tombstoned, we could free it early without waiting for the cursor. But that's not implemented yet.

## Current Limitations

**Concurrent chunk allocation is unsafe.** When the producer appends to a full chunk, it allocates a new one via `emplace_back` on the chunks vector. If the vector reallocates its internal storage, any pointer the consumer is currently following becomes dangling. This is mitigated for now by pre-reserving vector capacity at construction. The proper fix is either a lock around chunk allocation only (it's off the hot path, so contention would be minimal) or a lock-free growable container that guarantees pointer stability.

**Front eviction uses vector erase.** Erasing from the front of a `std::vector` shifts all subsequent elements. An alternative would be to maintain a front offset and adjust index calculations, compacting periodically. At current target scale this doesn't matter, but it's worth revisiting if the system scales to thousands of active sender deques.

**No persistence.** The deque is purely in-memory. If the relay crashes, all buffered messages are lost. Message durability is the client's responsibility, devices store history locally in SQLite and can resync from sibling devices via the append-only changelog.

## What's Next

**Commit 2** will have me build a device registry and tag registry, mapping accounts to devices and tags to members.
