#pragma once

#include <winsock2.h>
#include <mswsock.h>
#include <vector>
#include "crypto.hpp"
#include "transport.hpp"

namespace logana {
    // each connection has a socket, id, read buffer, number of bytes read, pending frame header, and a flag indicating if the header is complete
    struct ConnectionState {
        SOCKET socket;
        Connection id;
        std::vector<uint8_t> read_buffer;
        size_t bytes_read;
        FrameHeader pending_header;
        bool header_complete;
    };

    // we can accept, read from, and write to connections
    enum class OpType { Accept, Read, Write };

    // each operation has an overlapped struct, type, connection id, socket, address buffer that can hold both a local and remote address, and a write buffer for sending data
    struct OpData {
        OVERLAPPED overlapped;
        OpType type;
        Connection connection;
        SOCKET socket;
        char addr_buffer[2 * (sizeof(sockaddr_in) + 16)];
        uint8_t* write_buffer;
    };

    // transport class handles operations on connections
    class Transport {
        HANDLE completion_port_;
        SOCKET listen_socket_;
        // map from connection id to connection state
        std::unordered_map<Connection, ConnectionState> connections_;
        // callbacks for connection, disconnection, and frame reception
        OnConnect on_connect_;
        OnDisconnect on_disconnect_;
        OnFrame on_frame_;
        // connection counter
        Connection next_id_ = 1;
        // pre-creates socket for incoming connection and passes to accept
        void post_accept() {
            SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            auto* op = new OpData{};
            op->type = OpType::Accept;
            op->connection = next_id_++;
            op->socket = client;
            DWORD bytes_recieved;
            AcceptEx(listen_socket_, client, op->addr_buffer, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &bytes_recieved, &op->overlapped);
        }
        // resizes connection buffer and requests read from socket
        void post_read(Connection id) {
            auto it = connections_.find(id);
            if (it == connections_.end()) return;
            auto* op = new OpData{};
            op->type = OpType::Read;
            op->connection = id;
            it->second.read_buffer.resize(it->second.bytes_read + 4096);
            WSABUF wsabuf;
            wsabuf.buf = reinterpret_cast<char*>(it->second.read_buffer.data() + it->second.bytes_read);
            wsabuf.len = 4096;
            DWORD flags = 0;
            WSARecv(it->second.socket, &wsabuf, 1, NULL, &flags, &op->overlapped, NULL);
        }
        // associate socket with IOCP, creates connection state and runs connection handler, sets up read and accept
        void handle_accept(OpData* op, DWORD bytes) {
            CreateIoCompletionPort((HANDLE)op->socket, completion_port_, 0, 0);
            ConnectionState state{};
            state.socket = op->socket;
            state.id = op->connection;
            state.bytes_read = 0;
            state.header_complete = false;
            connections_[op->connection] = std::move(state);
            if (on_connect_) on_connect_(op->connection);
            post_read(op->connection);
            delete op;
            post_accept();
        }
        // if we read 0 bytes, disconnect, otherwise update the connection and prepare another read
        void handle_read(OpData* op, DWORD bytes) {
            if (bytes == 0) {
                if (on_disconnect_) on_disconnect_(op->connection);
                closesocket(connections_[op->connection].socket);
                connections_.erase(op->connection);
                delete op;
                return;
            }
            connections_[op->connection].bytes_read += bytes;
            process_read_buffer(op->connection);
            if (connections_.contains(op->connection)) post_read(op->connection);
            delete op;
        }
        // send completed, free buffer and opdata
        void handle_write(OpData* op, DWORD bytes) {
            (void)bytes;
            delete[] op->write_buffer;
            delete op;
        }
        // if header is incomplete, attempt to deserialize, disconnect on wrong magic, reads payload and moves extra bytes to front of buffer
        void process_read_buffer(Connection id) {
            auto& conn = connections_[id];
            size_t offset = 0;
            while (offset < conn.bytes_read) {
                if (!conn.header_complete) {
                    if (conn.bytes_read - offset < sizeof(FrameHeader)) break;
                    auto result = deserialize(conn.read_buffer.data() + offset, sizeof(FrameHeader));
                    if (!result) {
                        if (on_disconnect_) on_disconnect_(id);
                        closesocket(conn.socket);
                        connections_.erase(id);
                        return;
                    }
                    auto [payload_len, frame_type] = *result;
                    conn.pending_header = *reinterpret_cast<FrameHeader*>(conn.read_buffer.data() + offset);
                    conn.pending_header.payload_length = payload_len;
                    conn.header_complete = true;
                    offset += sizeof(FrameHeader);
                }
                uint32_t payload_len = conn.pending_header.payload_length;
                if (conn.bytes_read - offset < payload_len) break;
                if (on_frame_) on_frame_(id, static_cast<FrameType>(conn.pending_header.type), conn.read_buffer.data() + offset, payload_len);
                offset += payload_len;
                conn.header_complete = false;
            }
            if (offset > 0) {
                size_t remaining = conn.bytes_read - offset;
                std::memmove(conn.read_buffer.data(), conn.read_buffer.data() + offset, remaining);
                conn.bytes_read = remaining;
            }
        }
    public:
        // store callbacks
        void set_on_connect(OnConnect cb) {
            on_connect_ = cb;
        }
        void set_on_disconnect(OnDisconnect cb) {
            on_disconnect_ = cb;
        }
        void set_on_frame(OnFrame cb) {
            on_frame_ = cb;
        }
        // sets up windows sockets, IOCP and listener socket, loops on events and dispatches to handlers, cleans up after receiving sentinel
        void start(uint16_t port) {
            WSADATA wsaData;
            int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (err) {
                WSAGetLastError();
                exit(1);
            }
            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
                WSAGetLastError();
                WSACleanup();
                exit(1);
            }
            completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
            listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port);
            bind(listen_socket_, (sockaddr*)&addr, sizeof(addr));
            listen(listen_socket_, SOMAXCONN);
            CreateIoCompletionPort((HANDLE)listen_socket_, completion_port_, 0, 0);
            post_accept();
            DWORD num_bytes_transferred;
            ULONG_PTR completion_key;
            LPOVERLAPPED overlapped;
            while (GetQueuedCompletionStatus(completion_port_, &num_bytes_transferred, &completion_key, &overlapped, INFINITE)) {
                if (overlapped == nullptr) break;
                auto* overlapped_ex = reinterpret_cast<OpData*>(overlapped);
                switch (overlapped_ex->type) {
                    case OpType::Accept:
                        handle_accept(overlapped_ex, num_bytes_transferred);
                        break;
                    case OpType::Read:
                        handle_read(overlapped_ex, num_bytes_transferred);
                        break;
                    case OpType::Write:
                        handle_write(overlapped_ex, num_bytes_transferred);
                        break;
                }
            }
            for (auto& [id, conn] : connections_) closesocket(conn.socket);
            connections_.clear();
            closesocket(listen_socket_);
            CloseHandle(completion_port_);
            WSACleanup();
        }
        // posts sentinel that breaks event loop
        void stop() {
            PostQueuedCompletionStatus(completion_port_, 0, 0, nullptr);
        }
        // serializes header, initializes write buffer, submits async send
        void send_frame(Connection id, FrameType type, uint8_t flags, const uint8_t* payload, uint32_t payload_size) {
            auto it = connections_.find(id);
            if (it == connections_.end()) return;
            auto frame_header = serialize(flags, payload_size, type);
            auto* op = new OpData{};
            op->write_buffer = new uint8_t[sizeof(FrameHeader) + payload_size];
            std::memcpy(op->write_buffer, &frame_header, sizeof(FrameHeader));
            std::memcpy(op->write_buffer + sizeof(FrameHeader), payload, payload_size);
            op->type = OpType::Write;
            op->connection = id;
            WSABUF wsabuf;
            wsabuf.buf = reinterpret_cast<char*>(op->write_buffer);
            wsabuf.len = sizeof(FrameHeader) + payload_size;
            DWORD sent;
            WSASend(it->second.socket, &wsabuf, 1, &sent, 0, &op->overlapped, NULL);
        }
    };
}