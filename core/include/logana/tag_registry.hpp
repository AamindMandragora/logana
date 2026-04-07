#pragma once

#include "types.hpp"
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace logana {
    // default flag for accounts
    constexpr uint8_t FLAG_DEFAULT = 0;
    // flag for accounts with write permissions
    constexpr uint8_t FLAG_WRITER = 1 << 0;
    // flag for accounts with invite permissions
    constexpr uint8_t FLAG_INVITER = 1 << 1;
    // flag for accounts with kick permissions
    constexpr uint8_t FLAG_KICKER = 1 << 2;
    // flag for accounts with permission modification permissions
    constexpr uint8_t FLAG_MODIFIER = 1 << 3;

    // simple permissions struct holding permissions flags and a boolean for if the account is connected to every other account
    struct TagPermission {
        uint8_t permissions;
        bool fully_connected;
    };

    // struct holding tag information
    struct TagEntry {
        // unique id of the tag
        uint32_t tag_id;
        // unique account id of the tag's creator
        uint32_t creator_id;
        // mapping between accounts inside the tag and their permissions
        std::unordered_map<uint32_t, TagPermission> account_permissions;
    };

    // class holding the info about all of the tags
    class TagRegistry {
        std::mutex mutex_;
        // mapping from tag ids to tag entries
        std::unordered_map<uint32_t, TagEntry> tags_;
        // mapping between account ids and a set of tags they're in
        std::unordered_map<uint32_t, std::unordered_set<uint32_t>> tags_of_account_;
        // adjacency list for account ids and accounts they're connected to
        std::unordered_map<uint32_t, std::unordered_set<uint32_t>> adjacent_accounts_;

        // helper function that recomputes the interconnectedness of every account in the tag
        void recompute_connectivity(uint32_t tag_id) {
            // gets reference of all accounts in the tag
            auto& members = tags_[tag_id].account_permissions;
            for (auto& [id, perm] : members) {
                bool connected = true;
                // loops through all members
                for (auto& [other_id, _] : members) {
                    // ignore duplicate pairs
                    if (id == other_id) continue;
                    // if this account isn't connected to another account then this account isn't fully connected
                    if (!adjacent_accounts_[id].contains(other_id)) {
                        connected = false;
                        break;
                    }
                }
                perm.fully_connected = connected;
            }
        }
    public:
        // account param creates tag with id param
        void create_tag(uint32_t tag_id, uint32_t account_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // creates a new entry and fills out fields
            TagEntry entry;
            entry.tag_id = tag_id;
            entry.creator_id = account_id;
            entry.account_permissions[account_id].fully_connected = true;
            entry.account_permissions[account_id].permissions = FLAG_WRITER | FLAG_INVITER | FLAG_KICKER | FLAG_MODIFIER;
            // moves entry into the map
            tags_[tag_id] = std::move(entry);
            // inserts id into tags the account is in
            tags_of_account_[account_id].insert(tag_id);
        }

        // setter account adds target account to the tag
        bool add_member(uint32_t tag_id, uint32_t setter_id, uint32_t target_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // checks if tag, setter, target exist
            if (!tags_.contains(tag_id) || !tags_[tag_id].account_permissions.contains(setter_id) || tags_[tag_id].account_permissions.contains(target_id)) return false;
            // checks if setter has modification permissions or is fully connected
            if (tags_[tag_id].account_permissions[setter_id].fully_connected || tags_[tag_id].account_permissions[setter_id].permissions & FLAG_INVITER) {
                // sets the account's permissions to default
                tags_[tag_id].account_permissions[target_id].permissions = FLAG_DEFAULT;
                // inserts id into tags the account is in
                tags_of_account_[target_id].insert(tag_id);
                recompute_connectivity(tag_id);
                return true;
            }
            return false;
        }

        // setter account removes target account from the tag
        bool remove_member(uint32_t tag_id, uint32_t setter_id, uint32_t target_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // checks if tag, setter, exist and target doesn't
            if (!tags_.contains(tag_id) || !tags_[tag_id].account_permissions.contains(setter_id) || !tags_[tag_id].account_permissions.contains(target_id) || tags_[tag_id].creator_id == target_id) return false;
            // checks if setter has modification permissions or is fully connected or is the target
            if (tags_[tag_id].account_permissions[setter_id].fully_connected || tags_[tag_id].account_permissions[setter_id].permissions & FLAG_KICKER || setter_id == target_id) {
                // erases account key from the tag's accounts
                tags_[tag_id].account_permissions.erase(target_id);
                // erases tag from the account's tags
                tags_of_account_[target_id].erase(tag_id);
                recompute_connectivity(tag_id);
                return true;
            }
            return false;
        }

        // adds a connection between two accounts
        void add_connection(uint32_t account_id_1, uint32_t account_id_2) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // updates the adjacency lists
            adjacent_accounts_[account_id_1].insert(account_id_2);
            adjacent_accounts_[account_id_2].insert(account_id_1);
            // loops through all tags that the first account is in
            for (uint32_t tag_id : tags_of_account_[account_id_1]) {
                // if the second account is also in it recompute connectivity
                if (tags_of_account_[account_id_2].contains(tag_id)) {
                    recompute_connectivity(tag_id);
                }
            }
        }

        // removes a connection between two accounts
        void remove_connection(uint32_t account_id_1, uint32_t account_id_2) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // updates the adjacency lists
            adjacent_accounts_[account_id_1].erase(account_id_2);
            adjacent_accounts_[account_id_2].erase(account_id_1);
            // loops through all tags that the first account is in
            for (uint32_t tag_id : tags_of_account_[account_id_1]) {
                // if the second account is also in it recompute connectivity
                if (tags_of_account_[account_id_2].contains(tag_id)) {
                    recompute_connectivity(tag_id);
                }
            }
        }

        // setter account sets the permissions of target account in a tag
        void set_permissions(uint32_t tag_id, uint32_t setter_id, uint32_t target_id, uint8_t permissions) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // checks if tag, setter, target exist
            if (!tags_.contains(tag_id) || !tags_[tag_id].account_permissions.contains(setter_id) || !tags_[tag_id].account_permissions.contains(target_id)) return;
            // checks if setter has modification permissions or is fully connected
            if (tags_[tag_id].account_permissions[setter_id].fully_connected || tags_[tag_id].account_permissions[setter_id].permissions & FLAG_MODIFIER) {
                // sets target permissions
                tags_[tag_id].account_permissions[target_id].permissions = permissions;
            }
        }

        // checks if an account has write permissions in a tag
        bool can_write(uint32_t tag_id, uint32_t account_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // checks if tag and account exist
            if (!tags_.contains(tag_id) || !tags_[tag_id].account_permissions.contains(account_id)) return false;
            // returns if the account is fully connected or has the write flag set
            return (tags_[tag_id].account_permissions[account_id].fully_connected) || (tags_[tag_id].account_permissions[account_id].permissions & FLAG_WRITER);
        }

        // returns a set of all the accounts in the tag
        std::unordered_set<uint32_t> get_members(uint32_t tag_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            std::unordered_set<uint32_t> to_return;
            if (!tags_.contains(tag_id)) return to_return;
            for (auto &[key, _] : tags_[tag_id].account_permissions) {
                to_return.insert(key);
            }
            return to_return;
        }

        // transfers creator of tag from old to new
        bool transfer_ownership(uint32_t tag_id, uint32_t old_id, uint32_t new_id) {
            // locks mutex
            std::lock_guard<std::mutex> lock(mutex_);
            // checks if tag exists, old_id is creator, new_id is in the tag
            if (tags_.contains(tag_id) && tags_[tag_id].creator_id == old_id && tags_[tag_id].account_permissions.contains(new_id)) {
                tags_[tag_id].creator_id = new_id;
                return true;
            }
            return false;
        }
    };
}