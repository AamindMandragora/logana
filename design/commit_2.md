# Commit 2: Device Registry & Tag Registry

## The Problem

The chunked deque knows how to store messages. But it doesn't know *who* those messages are for. When a message arrives at the relay, someone has to answer two questions: "which devices should receive this?" and "is the sender allowed to post here?" That's the job of the device registry and the tag registry.

## Devices and Accounts

A person using Logana has an account. An account can have multiple devices — a phone, a laptop, maybe a tablet. When Alice sends a message to a tag, the relay needs to push it to every device belonging to every member of that tag. But Alice's own phone (the one she sent from) shouldn't get a copy back — she already has it.

So the relay needs a fast way to answer: given an account ID, what are its devices? Which ones are online right now? And which one should I skip because it's the originating device?

That's the device registry. It maps account IDs to their devices, tracks which devices are online (by storing their connection file descriptor), and handles the fan-out logic — iterating an account's devices, calling a callback for each online one, optionally skipping one.

### Why Two Maps?

The obvious structure is a list of accounts, each containing a list of devices. That works for the account-to-devices direction. But the relay also needs the reverse: given a device ID, find its state. When a device reconnects, sends a heartbeat, or disconnects, the relay has a device ID and needs to update that specific device's state.

With a flat list, finding a device means scanning every account's device list. That's O(total devices). With a hash map from device ID to device state, it's O(1). At 100 users this doesn't matter. At 10K users, O(1) vs O(n) on every heartbeat starts to add up.

So the device registry maintains two maps: account ID to account entry (for fan-out), and device ID to device state (for direct device operations). Same data, two access patterns, two indexes.

### Read Cursors

Each device also tracks how far it's read from each sender's deque — a map of sender ID to the last-read sequence number. When a device reconnects, it sends its cursors and the relay replays everything after them. When the relay wants to evict old chunks from a sender's deque, it checks the oldest cursor across all devices — that's the eviction watermark, the point before which no device will ever need to read.

### Threading

The device registry isn't on the hot path the way the chunked deque is. Registration, connection changes, and cursor updates are infrequent compared to message pushes. A mutex on every public method is the pragmatic choice. If profiling later shows contention, the path forward would be RCU (read-copy-update) — lock-free reads with writes taking a lock. But there's no reason to build that now.

## Tags and Permissions

Tags are how messages are addressed. When Alice sends to `#general`, the relay looks up tag `#general`'s member list and fans out to each member's devices. But not everyone in a tag necessarily has the same permissions.

### The Connectivity Model

Logana's permission model is built on a single idea: if you're connected to every other member in a tag, you have full permissions. If you're not, your permissions are explicitly set by someone who does.

This is a binary check. For each member in each tag, the relay caches a boolean: `fully_connected`. If true, the member can write, invite, kick, and modify permissions — no further checks needed. If false, the relay checks a permission byte with individual flags: `FLAG_WRITER`, `FLAG_INVITER`, `FLAG_KICKER`, `FLAG_MODIFIER`.

The fully_connected boolean gets recomputed whenever the social graph changes — when someone joins or leaves a tag, or when a connection between two accounts is added or removed. The recomputation is O(n²) in the number of tag members, but it only runs on these infrequent events. The hot path — checking whether someone can post a message — is a single boolean check or a bitwise AND. O(1).

### Recomputation

For each member in the tag, check if they're connected to every other member. If any connection is missing, they're not fully connected. That's a nested loop over members with an adjacency list lookup in the inner loop.

When does this run? Four triggers:

- A member joins the tag — the new member might not be connected to everyone, and existing members might not be connected to the new member.
- A member leaves — removing someone who wasn't connected to others might restore full connectivity for the remaining members.
- A connection is added between two accounts — both accounts' fully_connected status might improve in every tag they share.
- A connection is removed — both accounts' status might degrade.

For the connection change triggers, the relay needs to find all tags that both accounts share. A reverse index — mapping each account to the set of tags it belongs to — makes this an intersection of two sets rather than a scan of all tags.

### Permission Flags

Four flags, one byte:

- `FLAG_WRITER` (bit 0): can post messages to the tag.
- `FLAG_INVITER` (bit 1): can add new members.
- `FLAG_KICKER` (bit 2): can remove members.
- `FLAG_MODIFIER` (bit 3): can change other members' permission flags.

Four bits used, four reserved for future use. Flags are `constexpr uint8_t` values rather than an enum class, same as the message flags — because they need to be bitwise-OR'd together and checked with bitwise-AND.

When a tag is created, the creator gets all four flags. When a new member is added, they start with `FLAG_DEFAULT` (zero) — they can read but can't do anything else until a fully-connected member or a modifier grants them flags.

### Who Can Do What

Adding a member: the setter must be fully connected or have `FLAG_INVITER`. The target must not already be in the tag.

Removing a member: if setter and target are the same person, it's a self-leave — always allowed unless they're the creator (creator must transfer ownership first). Otherwise, the setter must be fully connected or have `FLAG_KICKER`. The target cannot be the creator.

Setting permissions: the setter must be fully connected or have `FLAG_MODIFIER`. Last-write-wins — if two people with permission disagree about someone's flags, the most recent call takes effect. Conflict resolution is a social problem, not a systems problem.

Ownership transfer: only the current creator can transfer. The new owner must already be a member.

### The Connection Graph

The tag registry maintains an adjacency list of account connections — who is connected to whom. This is the only piece of global social state the relay holds. It's a symmetric relationship (if A is connected to B, B is connected to A), stored as a map from account ID to the set of account IDs it's connected to. Both directions are updated on every add/remove.

## What Was Built

**device_registry.hpp:**

- `DeviceState`: per-device state holding device ID, account ID, connection handle (-1 if offline), and a map of per-sender read cursors.
- `AccountEntry`: account ID and its list of device IDs.
- `DeviceRegistry`: the manager class with mutex-protected operations for registration, connect/disconnect, cursor tracking, oldest-cursor computation, and templated fan-out with optional device skipping.

**tag_registry.hpp:**

- `TagPermission`: a fully_connected boolean and a permission flags byte.
- `TagEntry`: tag ID, creator ID, and a map of member account IDs to their permissions.
- `TagRegistry`: the manager class with mutex-protected operations for tag creation, member add/remove with permission checks, connection management, permission setting with authorization, connectivity recomputation, membership queries, and ownership transfer.

**test_device_registry.cpp:** 19 tests covering registration, connect/disconnect, cursor tracking, oldest-cursor computation, fan-out to online devices, fan-out skipping offline and originating devices, and edge cases with nonexistent accounts and devices.

**test_tag_registry.cpp:** 27 tests covering tag creation, authorized and unauthorized member add/remove, permission flag checks (inviter, kicker, modifier), full and partial connectivity scenarios, connection changes triggering recomputation, self-leave, creator protection, last-write-wins, ownership transfer, and edge cases.

## Current Limitations

**get_device returns a pointer to internal state.** The mutex is released when get_device returns, so the caller holds a pointer that could be invalidated if another thread causes a map rehash. At current scale this is fine. The safe alternative — returning a copy — would copy the read_sequences map on every call, which is wasteful. Documented tradeoff.

**Connectivity recomputation is O(n²).** For a tag with n members, recomputing fully_connected checks every pair. At typical tag sizes (5-50 members) this is negligible. At 1000+ members it could become noticeable, but by then the system should be using a different routing model for large tags anyway.

**No persistence.** Both registries are in-memory. If the relay restarts, all device registrations, connection state, and tag memberships are lost. Clients would need to re-register and rejoin tags. Persistence is a future concern.

**Mutex on every operation.** Simple and correct, but under high contention (many concurrent fan-outs or cursor updates) the mutex becomes a bottleneck. The upgrade path is RCU for read-heavy operations like fan_out and can_write, with writes still taking a lock.

## What's Next

**Commit 3** will tie the chunked deques, device registry, and tag registry together into the relay routing logic — the component that receives a message, stores it in the sender's deque, and fans out MessageRefs to all recipients.
