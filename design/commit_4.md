
# Commit 4: TCP Transport & Crypto

## The Problem

After three commits, Logana has a complete in-memory relay: chunked deques buffer messages, registries track devices and tags, the router enforces permissions and fans out refs. But all of that only works inside a single process. There's no way for a device on the network to connect, authenticate, and exchange messages.

This commit builds the three missing layers: a binary wire protocol that defines how data moves across a TCP connection, a cryptographic handshake that proves identity without passwords, and an async transport layer that handles thousands of simultaneous connections without blocking.

## The Wire Protocol

### Why Binary

The relay targets sub-microsecond routing on the hot path. JSON and Protobuf both require schema-driven parsing: walking the input byte by byte, matching keys, checking types, allocating strings. For our payloads, that work is pointless. A MessageRef is always exactly 24 bytes in exactly the same layout. There's nothing to parse — just copy and go.

A raw binary protocol with packed structs gives us zero-copy on the fast path. The server reads bytes off the wire and reinterprets them directly as the target struct. No parsing, no allocation, no intermediate representation. The trade-off is that every field must have a fixed position and a fixed size, which means no optional fields and no variable-length strings in the core message types. That's fine for a relay protocol where the payload formats are fully specified and both sides are our own code.

### Header Design

Every frame starts with an 8-byte header:

```header
[magic: 2] [type: 1] [flags: 1] [payload_length: 4]
```

**Magic (2 bytes):** `0x4C 0x4E` ("LN"). The first thing the server checks on every frame. If the magic doesn't match, the connection is immediately closed. This catches protocol mismatches, broken framing, and accidental connections from non-Logana clients. Two bytes were chosen over four to keep the header at 8 bytes total, which is a single aligned `uint64_t` load on 64-bit architectures. The magic is stored as `uint8_t[2]` rather than `uint16_t` to avoid endianness ambiguity — byte comparison works regardless of the machine's byte order.

**Type (1 byte):** Identifies the frame type via the `FrameType` enum. Eleven types cover the full protocol: two for the auth handshake (`auth_challenge`, `auth_response`), two for messaging (`message_send`, `ref_push`), two for payload fetching (`payload_fetch_request`, `payload_fetch_response`), one for acknowledgments (`ack`), two for keepalive (`heartbeat_ping`, `heartbeat_pong`), one for permission updates, and one for errors.

**Flags (1 byte):** Reserved for per-frame metadata. Currently used for version negotiation. Provides room for future extensions without breaking the header format.

**Payload length (4 bytes):** The size of the payload following the header, stored in network byte order (`htonl`/`ntohl`). This is the only field that goes through byte-order conversion — everything else is single-byte. A 4-byte length supports payloads up to 4 GiB, more than enough for any foreseeable message.

### Payload Design

Each frame type has a corresponding payload struct, packed with `#pragma pack(push, 1)` and validated at compile time with `static_assert` on size. There are two categories:

**Fixed-size payloads:** The struct *is* the payload. `AuthChallenge` (32 bytes: a nonce), `AuthResponse` (72 bytes: a 64-byte signature, account ID, device ID), `PayloadFetchRequest` (12 bytes: account ID, sequence), `Ack` (8 bytes: a sequence number), `PermissionUpdate` (9 bytes: tag ID, account ID, permission byte), `MessageRef` for `ref_push` (24 bytes: the same trivially-copyable struct used internally).

**Variable-size payloads:** A packed prefix struct contains the fixed fields, and the remaining bytes (determined by `payload_length - sizeof(prefix)`) carry the variable data. `MessageSend` has a 14-byte prefix (tag ID, message type, flags, timestamp) followed by the encrypted message body. `PayloadFetchResponse` has a 14-byte prefix (account ID, sequence, message type, flags) followed by the message body. `Error` has a 2-byte prefix (error code) followed by a UTF-8 error string.

**Heartbeat ping and pong** have zero-length payloads. The header alone carries the meaning.

### Why Packed Structs and Not Manual Serialization

The alternative is writing serialize/deserialize functions that read and write each field individually, handling byte order and alignment manually. For protocols with complex schemas or cross-language requirements, that's the right call. For Logana, both sides are C++, the structs are small and fixed-layout, and we control every field. Packed structs with `static_assert` on size give us the same guarantees with less code and zero runtime overhead.

The one subtlety is alignment. Packed structs may not be naturally aligned, so accessing them via pointer cast from a raw buffer is technically undefined behavior on some architectures. The `deserialize` function sidesteps this by using `memcpy` into a stack-allocated `FrameHeader`, which the compiler optimizes to a register load on x86.

## Authentication

### Why Ed25519

The relay never stores passwords. Instead, each device has an Ed25519 keypair. The public key is registered with the relay when the account is created. Authentication uses a challenge-response protocol: the server proves it knows the client's identity by verifying that the client can sign a random nonce with the corresponding secret key.

Ed25519 was chosen over RSA (large keys, slower signing), ECDSA (malleable signatures without extra care), and HMAC (symmetric, requires shared secrets). Ed25519 gives 128-bit security with 32-byte public keys and 64-byte signatures, it's deterministic (same input always produces the same signature, which eliminates an entire class of nonce-reuse vulnerabilities), and libsodium makes it trivial.

### The Handshake

```arch
Server                          Client
  │                                │
  │──── auth_challenge(nonce) ────>│
  │                                │
  │<── auth_response(sig, id) ─────│
  │                                │
  │  verify sig against stored pk  │
  │                                │
  │  associate connection ←→ device│
```

1. On connect, the server generates a 32-byte random nonce via `randombytes_buf` and sends it in an `auth_challenge` frame.
2. The client signs the nonce with its Ed25519 secret key and responds with an `auth_response` frame containing the signature, account ID, and device ID.
3. The server looks up the public key for the claimed account ID in the key store, verifies the signature, and either associates the connection with the device in the device registry or sends an error frame and closes.

The nonce prevents replay attacks: even if an attacker captures a valid auth_response, they can't reuse it because the next challenge will have a different nonce. The signature proves the client possesses the secret key without ever transmitting it.

### Key Storage

Keys are currently held in an in-memory `std::unordered_map<AccountId, PublicKey>`, declared as an inline global. This is a placeholder. In production, keys would be persisted in the device's local SQLite database (encrypted with SQLCipher) and the relay would store only public keys.

## Async Transport

### Why Not epoll

The relay needs to handle thousands of simultaneous connections, each potentially idle for long periods with occasional bursts of activity. Blocking I/O with one thread per connection doesn't scale: at a thousand connections, you're paying for a thousand thread stacks and a thousand context switches per second just for keepalives.

The traditional Linux solution is `epoll`: register file descriptors, wait for readiness, then issue the actual read/write call yourself. It works, it's battle-tested, and virtually every production server uses it. But it has a fundamental inefficiency: readiness notification and data transfer are separate operations. epoll tells you a socket is readable, then you call `read()`, which is a separate syscall. For every incoming message, you pay two syscalls.

`io_uring` unifies submission and completion into a single interface. You submit a "receive N bytes into this buffer" request and get back a "here are your bytes" completion. One submission, one completion, zero intermediate syscalls. The kernel does the read directly into your buffer via the submission queue. For a relay that processes millions of message refs per day, halving the syscall count is meaningful.

On Windows, IOCP (I/O Completion Ports) has provided this same completion-based model since Windows NT. We support both: IOCP for development (the relay is developed on Windows), io_uring for production (the relay runs on Linux).

### The Submission/Completion Model

Both IOCP and io_uring follow the same abstract pattern:

1. **Submit** an I/O operation (accept, read, write) to the kernel, attaching a tag that identifies the operation.
2. **Wait** for a completion event from the kernel.
3. **Dispatch** based on the tag: call the appropriate handler.
4. **Repeat.**

The tag is the key abstraction, and on both platforms it's a struct named `OpData`. On Windows, `OpData` embeds an `OVERLAPPED` (required by IOCP) plus `OpType`, connection ID, socket, an address buffer sized for `AcceptEx`'s local+remote address pair, and a write buffer pointer. On Linux, `OpData` is lighter weight — just `OpType`, connection ID, fd, and a write buffer pointer — since io_uring doesn't require an embedded kernel struct the way IOCP does. The tag travels with the operation through the kernel and comes back on completion, so the event loop always knows what just finished.

### Platform Differences

Despite the shared model, the two platforms differ in their mechanics:

**Accept:** IOCP requires pre-creating the client socket and passing it to `AcceptEx`. The socket is ready when the completion fires. io_uring takes the listen fd and returns the new fd in `cqe->res`. No pre-creation needed.

**Error reporting:** IOCP reports errors through the return value of `GetQueuedCompletionStatus` — if it returns TRUE, the bytes-transferred count is valid and unsigned. io_uring packs everything into the signed `cqe->res`: positive is bytes transferred, zero is EOF, negative is an errno. This means the Linux read handler checks `<= 0` with `int`, while the Windows handler checks `== 0` with `DWORD`.

**Shutdown:** IOCP uses `PostQueuedCompletionStatus` with a NULL overlapped pointer as a sentinel. io_uring uses a no-op SQE with NULL user data. Both cause the event loop to break.

**Buffer management:** Both allocate write buffers on the heap and stash the pointer in the tag struct. The write completion handler frees the buffer. Read buffers live inside ConnectionState and grow on demand.

### The Frame State Machine

`process_read_buffer` is identical on both platforms — it's pure application logic with no platform calls. TCP is a byte stream, not a message stream: a single `recv` might deliver half a frame, three frames, or two and a half frames. The state machine handles all of these.

Each connection tracks two pieces of state: whether a header has been parsed but its payload hasn't arrived yet (`header_complete`), and the parsed header itself (`pending_header`). The machine loops over the buffer:

1. If no header is pending, try to consume 8 bytes. Validate magic via `deserialize`. If magic is bad, disconnect immediately. Otherwise save the header and advance.
2. If a header is pending, check whether enough payload bytes have arrived. If so, fire `on_frame` with the payload pointer, advance, and reset.
3. If there aren't enough bytes for either step, break and wait for the next read.

After the loop, any unconsumed bytes are `memmove`d to the front of the buffer. This shift is necessary because the next `recv` will append to `bytes_read`, and the buffer must be contiguous from index 0.

## What Was Built

**frame.hpp:** 8-byte `FrameHeader` with packed layout. Eleven `FrameType` enum values. Packed payload structs for each frame type with compile-time size assertions. `serialize()` builds a header by value, `deserialize()` validates magic and extracts payload length and type via `memcpy` to avoid alignment UB. This file also owns the Ed25519 key/signature type aliases (`PublicKey`, `SecretKey`, `Signature`) and their size constants, pulled from `<sodium.h>` directly — they live here rather than in crypto.hpp because `AuthResponse` needs `SIGNATURE_SIZE` for its packed layout.

**crypto.hpp:** Four functions wrapping libsodium: `generate_nonce`, `sign_challenge`, `verify_challenge`, `generate_keypair`. Inline global `key_store` mapping account IDs to public keys. Includes frame.hpp for the key/signature types defined there.

**transport.hpp:** Callback type aliases (`OnConnect`, `OnDisconnect`, `OnFrame`). Compile-time platform selection via `#ifdef __linux__` / `#elif defined(_WIN32)`.

**windows_server.hpp:** IOCP transport. `OverlappedEx` extends `OVERLAPPED` with operation metadata. Full event loop: WSAStartup, completion port creation, socket binding, accept/read/write posting, frame state machine, cleanup on shutdown.

**linux_server.hpp:** io_uring transport. `OpData` carries operation metadata. Full event loop: `io_uring_queue_init`, socket binding, SQE-based accept/read/write submission, CQE-based completion dispatch, frame state machine, cleanup on shutdown.

**test_frame.cpp:** 23 tests covering header size, magic byte validation, corrupted magic, truncated input, roundtrip serialization for all 11 frame types, zero and maximum payload lengths, flag and type byte preservation, complete frame assembly for `auth_challenge`, `auth_response`, `ref_push`, `ack`, `payload_fetch_request`, `permission_update`, and heartbeat types, garbage rejection, and payload struct size assertions.

**test_crypto.cpp:** 15 tests covering keypair generation and uniqueness, nonce generation and uniqueness, sign/verify roundtrip, multiple nonces with the same key, corrupted signatures (first byte, last byte), wrong public key, wrong nonce, zero signature, deterministic signing, full auth handshake flow against the key store, unknown account lookup, and wrong-account-key rejection.

**test_server.cpp:** 9 integration tests covering connect/disconnect callbacks, client-to-server frame delivery, server-to-client frame delivery, ping/pong roundtrip, variable-length payload delivery, multiple back-to-back frames, invalid magic triggering disconnect, full auth handshake over TCP, and auth failure with a bad signature.

## Current Limitations

**`stop()` is not thread-safe on Linux.** `io_uring_get_sqe` and `io_uring_submit` are called from the stopping thread while the event loop runs on another. io_uring's submission queue is not internally synchronized. This works in practice for single-caller stop scenarios but is technically a data race. The correct fix is an `eventfd` that the event loop polls, with `stop()` writing to the eventfd instead of touching the ring directly.

**No TLS.** The transport is raw TCP. Message content will be E2E encrypted at the application layer (commit 5), but the transport metadata (who's connecting, frame types, payload sizes) is visible to anyone on the network path. TLS would protect the transport channel itself. This is a future addition.

**No backpressure.** If a client sends frames faster than the server can process them, the read buffer grows without bound. A production system would need per-connection buffer limits and flow control.

**Key store is in-memory.** The inline global `unordered_map` works for testing but has no persistence, no access control, and no mechanism for key rotation. Production would use SQLite-backed key storage.

**Single-threaded event loop.** Both transports run on a single thread. This is correct for the target scale (~1K connections) and avoids all concurrency issues in the connection map and frame state machine. Scaling beyond that would require sharding connections across multiple event loops, each on its own thread.

## What's Next

**Commit 5** adds E2E encryption at the message level using libsodium's Sender Keys model, so the relay physically cannot read message content even if compromised.
