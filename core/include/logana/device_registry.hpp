#pragma once

#include "types.hpp"
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <mutex>

namespace logana {
    // struct holding the state of a device
    struct DeviceState {
        // the unique id of the device
        uint32_t device_id;
        // the unique id of the account that owns the device
        uint32_t account_id;
        // the file descriptor the device is connected to (-1 if disconnected)
        int64_t connection;
        // map of sender id to last-read sequence number
        std::unordered_map<uint32_t, uint64_t> read_sequences;

        // a function that returns if the device is online
        bool is_online() const {
            return connection != -1;
        }
    };

    // struct holding account info
    struct AccountEntry {
        // the unique id of the account
        uint32_t account_id;
        // vector of device ids owned by the account
        std::vector<uint32_t> device_ids;
    };

    // class holding all the info about accounts and devices
    class DeviceRegistry {
        std::mutex mutex_;
        // map of unique ids to existing devices
        std::unordered_map<uint32_t, DeviceState> devices_;
        // map of unique ids to existing accounts
        std::unordered_map<uint32_t, AccountEntry> accounts_;
        // the id that the next created device will be assigned
        uint32_t next_device_id_ = 1;
    public:
        // registers a device under the given account
        uint32_t register_device(uint32_t account_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // gets account from id parameter
            AccountEntry& account = accounts_[account_id];
            // places the device id in the account's device list
            account.device_ids.emplace_back(next_device_id_);
            // creates a new device, fills fields
            DeviceState device;
            device.device_id = next_device_id_;
            device.account_id = account_id;
            device.connection = -1;
            // moves device into the devices array
            devices_[next_device_id_] = std::move(device);
            // returns device id and increments it for next time
            return next_device_id_++;
        }

        // takes in device id and file descriptor, changes connectivity status to file descriptor
        bool connect_device(uint32_t device_id, int64_t connection) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // returns if the device was found
            if (!devices_.contains(device_id)) return false;
            devices_[device_id].connection = connection;
            return true;
        }

        // takes in device, changes connectivity status to disconnected
        bool disconnect_device(uint32_t device_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // returns if the device was found
            if (!devices_.contains(device_id)) return false;
            devices_[device_id].connection = -1;
            return true;
        }

        // gets the device associated with the device id
        DeviceState *get_device(uint32_t device_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // if device doesn't exist return nullptr
            if (!devices_.contains(device_id)) return nullptr;
            return &devices_[device_id];
        }

        // returns a vector of devices owned by the account associated with id parameter
        std::vector<uint32_t> get_account_devices(uint32_t account_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // if account doesn't exist return empty vector
            if (!accounts_.contains(account_id)) return std::vector<uint32_t>();
            return accounts_[account_id].device_ids;
        }

        // updates the last read sequence of a sender in a device
        void update_sequence(uint32_t device_id, uint32_t sender_id, uint64_t sequence) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            if (devices_.contains(device_id)) {
                devices_[device_id].read_sequences[sender_id] = sequence;
            }
        }

        // gets the oldest sequence from a sender across all devices
        uint64_t get_oldest_sequence(uint32_t sender_id) {
            uint64_t oldest = UINT64_MAX;
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // loops through all devices
            for (auto& [_, device] : devices_) {
                // checks if device has message from sender
                if (device.read_sequences.contains(sender_id)) {
                    // updates oldest
                    oldest = std::min(oldest, device.read_sequences[sender_id]);
                }
            }
            return oldest;
        }

        // templating for faster function
        template <typename F>
        // calls callback on each device owned by the account except for maybe a skipped one
        void fan_out(uint32_t account_id, MessageRef ref, F&& callback, std::optional<uint32_t> skip_device_id = std::nullopt) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accounts_.contains(account_id)) return;
            AccountEntry& account = accounts_[account_id];
            // loop through all devices in the account
            for (uint32_t device_id : account.device_ids) {
                // if this device should be skipped continue
                if (skip_device_id.has_value() && device_id == skip_device_id.value()) continue;
                // if the device is online callback on the device's file descriptor and the message reference
                if (devices_[device_id].is_online()) std::forward<F>(callback)(devices_[device_id].connection, ref);
            }
        }        
    };
}