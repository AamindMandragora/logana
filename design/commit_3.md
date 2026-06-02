# Commit 3: Message Store & Relay Router

## The Problem

After commit 2, the relay has three components that don't talk to each other. The chunked deque can store messages. The device registry knows who's online and where. The tag registry knows who's allowed to do what. But when a message arrives at the relay, there's no code that says "check permissions, store this, and tell everyone about it." Three organs, no nervous system.

The relay router is the nervous system. Its job is deceptively simple: receive a message, decide if the sender is allowed to post it, store it in the sender's deque, and fan out lightweight references to every device that should know about it. Four steps, one function call. But the details of how those components are wired together — who owns what, who constructs what, who's responsible for the message's lifetime — those are the decisions that matter.

## The Message Store

Before building the router, there's a gap to fill. The chunked deque stores messages for a single producer. But the relay has many producers — one per account. Something needs to manage the collection of deques, creating them on demand, routing stores and fetches to the right one.

That's the message store. It's a thin layer — almost embarrassingly thin — but it earns its existence by owning two things: the deque lifecycle and message construction.

### Who Builds the Message?

The caller could construct a `Message`, fill in the header, allocate the payload, and hand it to the store. But that leaks a detail the caller shouldn't care about: the sequence number. Sequences are assigned by the deque on append — they're a storage concern, not a sender concern. If the caller builds the message, they either leave the sequence field unset (a sentinel value, ugly) or set it to something the store overwrites (wasteful and confusing).

The store takes raw components instead: sender ID, tag ID, message type, flags, timestamp, payload pointer, and payload size. It constructs the `Message` internally, pushes it into the deque, and returns a `MessageRef` with the assigned sequence. The caller says "store this data" and gets back a 24-byte reference. That's the contract.

### One Deque Per Account

The map is `std::unordered_map<AccountId, std::unique_ptr<ChunkedDeque<512>>>`. Two things to unpack here.

First, `unique_ptr`. The natural instinct is value semantics — just store the deque directly in the map. But when an `unordered_map` rehashes (because more accounts joined and the load factor tripped), it relocates every element. If anything holds a pointer or reference into a deque that just moved, it's now pointing at freed memory. `unique_ptr` solves this: the deque lives at a stable heap address, and rehashing just shuffles 8-byte pointers. One extra indirection per access, invisible at this scale.

Second, lazy creation. The deque for an account doesn't exist until that account sends their first message. Pre-allocating a deque for every registered account wastes memory on lurkers who never post. The tradeoff is a heap allocation on the first send — one `make_unique` call, once per account, ever. Every subsequent message is O(1) append into existing capacity. Optimizing away a one-time allocation when the bottleneck is network I/O would be absurd.

### Fetching and Cloning

`Message` has a deleted copy constructor. That's intentional — on the hot path, you don't want accidental payload copies flying around. But the fetch path is different. When an offline device reconnects and requests the actual payload behind a `MessageRef`, the store needs to hand back a `Message` the caller owns. Returning a pointer into the deque is unsafe because eviction could free the underlying chunk at any time.

The solution is a `clone()` method on `Message` — an explicit deep copy that allocates a new payload buffer and memcpys the bytes. The copy constructor stays deleted, so accidental copies are still a compile error. Intentional copies are opt-in. `fetch()` returns `std::optional<Message>`: the clone if the message exists, `nullopt` if the account doesn't exist or the sequence has been evicted.

This is a cold path operation. Fetching happens when a device reconnects and pulls missed messages. The hot path — storing and fanning out refs — never copies a payload.

## The Relay Router

The router itself is almost anticlimactic. It holds references to the message store, device registry, and tag registry — injected via constructor, not owned. Tests set up the registries however they want and hand them in.

One method: `route_message`. Templated on a callback so the caller decides what "deliver a ref to a device" means. In commit 3, that's a lambda that captures a vector. In commit 4, it'll be a function that writes bytes to a TCP socket. The router doesn't care.

### The Pipeline

Step one: permission check. Ask the tag registry `can_write(tag, sender)`. If the answer is no, return `nullopt`. No message is stored, no fan-out happens, the attempt is dead on arrival.

Step two: store. Call `store_and_ref()` on the message store. The message is now in the sender's deque and a `MessageRef` exists.

Step three: fan-out. Get the tag's member list from the tag registry. For each member, call the device registry's `fan_out` to invoke the callback on every online device belonging to that member. The callback receives the device's connection handle and the `MessageRef`.

One subtlety in step three: the originating device. When Alice sends from her phone, her phone already has the message — it doesn't need a ref pushed back. But her laptop, if it's online, *does* need the ref for multi-device sync. So the skip logic is per-account: on the sender's account, skip the originating device. On every other account, deliver to all devices.

Without this split, if a recipient happens to have a device with the same numeric ID as the sender's originating device, it would get incorrectly skipped. The skip must only apply to the sender's device list, not globally.

### Why Callbacks, Not Return Values

The router could build up a list of `(DeviceId, MessageRef)` pairs and return it. The caller would then iterate and dispatch. That's easier to test — you just check the list. But it means allocating a vector on every `route_message` call, and for a shared tag with 50 members averaging 2 devices each, that's a 100-element vector per message. On the hot path, that allocation matters.

The callback approach is zero-allocation. The device registry's `fan_out` iterates devices in-place and calls the callback inline. No intermediate storage, no vector growth, no allocator pressure. The callback pattern is already established in the device registry — the router just threads it through.

Testing is slightly more involved (capture deliveries in a vector inside the test lambda) but not meaningfully harder.

## Typed Aliases

This commit also adds type aliases to `types.hpp`:

```cpp
using AccountId = uint32_t;
using TagId = uint32_t;
using DeviceId = uint32_t;
using Sequence = uint64_t;
using Timestamp = uint64_t;
using Connection = int;
```

These don't add compile-time safety — `AccountId` and `TagId` are both `uint32_t`, so the compiler won't catch you swapping them. But they make every function signature self-documenting. `store_and_ref(AccountId, TagId, ...)` reads immediately. `store_and_ref(uint32_t, uint32_t, ...)` makes you check which is which. If compile-time safety ever matters, the upgrade path is strong typedefs (wrapper structs with explicit construction). Not needed yet.

## Connection Precondition

One more change to existing code: `add_member` in the tag registry now requires a direct connection between the inviter and the invitee before any permission checks run. Without this, anyone with `FLAG_INVITER` on a tag could add any account ID they happen to know about — a spam vector that undermines the "no discovery, no search" privacy model. The check is a single adjacency lookup, O(1) in the connection graph's hash set.

## What Was Built

**types.hpp:**

- Typed aliases for `AccountId`, `DeviceId`, `TagId`, `Sequence`, `Timestamp`, `Connection`.
- `clone()` method on `Message` for explicit deep copy of header and payload.

**message_store.hpp:**

- `MessageStore`: manages per-account chunked deques. `store_and_ref()` takes raw message components, constructs the message internally, pushes to the sender's deque, returns a `MessageRef`. `fetch()` returns `std::optional<Message>` via clone. `evict_before()` forwards to the deque's eviction method.

**relay_router.hpp:**

- `RelayRouter`: holds references to the message store, device registry, and tag registry. `route_message()` implements the full pipeline: permission check, store, fan-out with origin-device skip. Templated on a callback for zero-allocation dispatch.

**test_message_store.cpp:** 9 tests covering ref construction, sequence incrementing, per-account deque isolation, fetch with clone, nonexistent account and sequence, eviction, no-op eviction on nonexistent accounts, and flag preservation through store-and-fetch.

**test_relay_router.cpp:** 9 tests covering basic two-party routing, permission denied, origin device skip, sender's other devices receiving, offline devices skipped, shared tag fan-out, nonexistent tag, message fetchability after routing, and verifying that the skip doesn't leak across accounts.

## Current Limitations

**Single-threaded assumption.** The relay router has no synchronization of its own. It relies on the device registry and tag registry's internal mutexes, but the `route_message` pipeline itself is not thread-safe — concurrent calls could interleave store and fan-out in surprising ways. This is acceptable because the transport layer (commit 4) will define the threading model. The router will likely run on a single event-loop thread with I/O dispatched via io_uring.

**No backpressure.** If a tag has 1000 members and the router fans out to all of them inline, the `route_message` call blocks until every callback has fired. For a callback that does real I/O, that's 1000 writes before the function returns. The fix is asynchronous dispatch — queue refs for delivery and let the I/O layer drain them — but that's a transport concern, not a routing concern.

**No eviction integration.** `evict_before()` exists on the message store but nothing calls it. Cursor-based eviction requires tracking the oldest active cursor across all recipient devices for a given sender, computing the eviction watermark, and triggering eviction. The device registry already has `get_oldest_sequence()` for this, but the orchestration that ties it together doesn't exist yet.

**Fan-out iterates all members.** `get_members()` returns a copy of the member set, and the router loops through it. For large tags this is a per-message allocation. A callback-based member iteration on the tag registry (like the device registry's `fan_out`) would avoid the copy. Not worth optimizing until tag sizes actually become a bottleneck.

## What's Next

**Commit 4** adds raw TCP transport with custom binary framing and io_uring — the point where messages actually move over a wire instead of through function calls.
