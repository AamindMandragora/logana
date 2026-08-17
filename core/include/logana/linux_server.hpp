#pragma once

#include <liburing.h>
#include <vector>
#include "crypto.hpp"
#include "transport.hpp"

namespace logana {
    // each connection has a socket, id, read buffer, number of bytes read, pending frame header, and a flag indicating if the header is complete
    struct ConnectionState {
        int socket_fd;
        Connection id;
        std::vector<uint8_t> read_buffer;
        size_t bytes_read;
        FrameHeader pending_header;
        bool header_complete;
    };

    // we can accept, read from, and write to connections
    enum class OpType { Accept, Read, Write };

    // each operation has a type, connection id, socket, and a write buffer for sending data
    struct OpData {
        OpType type;
        Connection connection;
        int socket_fd;
        uint8_t* write_buffer = nullptr;
    };

    // transport class handles operations on connections
    class Transport {
        struct io_uring ring_;
        int listen_socket_fd_;
        // map from connection id to connection state
        std::unordered_map<Connection, ConnectionState> connections_;
        // callbacks for connection, disconnection, and frame reception
        OnConnect on_connect_;
        OnDisconnect on_disconnect_;
        OnFrame on_frame_;
        // connection counter
        Connection next_id_ = 1;
        // prepares async accept and adds opdata to submission queue
        void post_accept() {
            auto* op = new OpData{};
            op->type = OpType::Accept;
            op->connection = next_id_++;
            auto* sqe = io_uring_get_sqe(&ring_);
            io_uring_prep_accept(sqe, listen_socket_fd_, NULL, NULL, 0);
            io_uring_sqe_set_data(sqe, op);
            io_uring_submit(&ring_);
        }
        // prepares async recv and adds opdata to submission queue
        void post_read(Connection id) {
            auto it = connections_.find(id);
            if (it == connections_.end()) return;
            auto* op = new OpData{};
            op->type = OpType::Read;
            op->connection = id;
            it->second.read_buffer.resize(it->second.bytes_read + 4096);
            auto* sqe = io_uring_get_sqe(&ring_);
            char* buffer = reinterpret_cast<char*>(it->second.read_buffer.data() + it->second.bytes_read);
            op->socket_fd = it->second.socket_fd;
            io_uring_prep_recv(sqe, op->socket_fd, buffer, 4096, 0);
            io_uring_sqe_set_data(sqe, op);
            io_uring_submit(&ring_);
        }
        // initializes connection state, runs handler, requests read and new accept
        void handle_accept(OpData* op, int fd) {
            ConnectionState state{};
            state.socket_fd = fd;
            state.id = op->connection;
            state.bytes_read = 0;
            state.header_complete = false;
            connections_[op->connection] = std::move(state);
            if (on_connect_) on_connect_(op->connection);
            post_read(op->connection);
            delete op;
            post_accept();
        }
        // if we read less than 0 bytes disconnect, otherwise process the read and start another
        void handle_read(OpData* op, int bytes) {
            if (bytes <= 0) {
                if (on_disconnect_) on_disconnect_(op->connection);
                close(connections_[op->connection].socket_fd);
                connections_.erase(op->connection);
                delete op;
                return;
            }
            connections_[op->connection].bytes_read += bytes;
            process_read_buffer(op->connection);
            if (connections_.contains(op->connection)) post_read(op->connection);
            delete op;
        }
        // send completed, free buffer and op
        void handle_write(OpData* op, int bytes) {
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
                        close(conn.socket_fd);
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
        // sets up listen socket, io_uring, posts first accept, enters loop of looking at completed events and running the corresponding handler, breaks and cleans up on sentinel
        void start(uint16_t port) {
            listen_socket_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port);
            bind(listen_socket_fd_, (sockaddr*)&addr, sizeof(addr));
            listen(listen_socket_fd_, SOMAXCONN);
            io_uring_queue_init(256, &ring_, 0);
            post_accept();
            struct io_uring_cqe* cqe;
            while (io_uring_wait_cqe(&ring_, &cqe) == 0) {
                if (cqe->user_data == 0) break;
                auto* op = reinterpret_cast<OpData*>(cqe->user_data);
                switch (op->type) {
                    case OpType::Accept:
                        handle_accept(op, cqe->res);
                        break;
                    case OpType::Read:
                        handle_read(op, cqe->res);
                        break;
                    case OpType::Write:
                        handle_write(op, cqe->res);
                        break;
                }
                io_uring_cqe_seen(&ring_, cqe);
            }
            for (auto& [id, conn] : connections_) close(conn.socket_fd);
            connections_.clear();
            close(listen_socket_fd_);
            io_uring_queue_exit(&ring_);
        }
        // submits NULL sentinel to io_uring
        void stop() {
            auto* sqe = io_uring_get_sqe(&ring_);
            io_uring_prep_nop(sqe);
            io_uring_sqe_set_data(sqe, 0);
            io_uring_submit(&ring_);
        }
        // serializes header, sets up buffer, preps for send, submits to queue
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
            auto* sqe = io_uring_get_sqe(&ring_);
            char* buffer = reinterpret_cast<char*>(op->write_buffer);
            op->socket_fd = it->second.socket_fd;
            io_uring_prep_send(sqe, op->socket_fd, buffer, sizeof(FrameHeader) + payload_size, 0);
            io_uring_sqe_set_data(sqe, op);
            io_uring_submit(&ring_);
        }
    };
}