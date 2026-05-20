#include <cassert>
#include <iostream>

#include "logana/tag_registry.hpp"

using namespace logana;

// tests the creation of a tag
void test_create_tag() {
    TagRegistry registry;
    // account 100 creates tag 1
    registry.create_tag(1, 100);

    // gets members of tag 100, asserts there's one member with write permissions
    auto members = registry.get_members(1);
    assert(members.size() == 1);
    assert(members.contains(100));
    assert(registry.can_write(1, 100));

    std::cout << "test_create_tag passed" << std::endl;
}

// tests adding a connected account to the tag
void test_add_member() {
    TagRegistry registry;
    // account 100 connects to 200, creates tag 1, and adds 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    assert(registry.add_member(1, 100, 200));

    // gets the members of tag 1, asserts both 100 and 200 are members
    auto members = registry.get_members(1);
    assert(members.size() == 2);
    assert(members.contains(100));
    assert(members.contains(200));

    std::cout << "test_add_member passed" << std::endl;
}

// tests double-add
void test_add_member_already_exists() {
    TagRegistry registry;
    // account 100 connects to 200, creates tag 1
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    // account 100 tries to add account 200 to tag 1 twice, assert success first time and fail second time
    assert(registry.add_member(1, 100, 200));
    assert(!registry.add_member(1, 100, 200));

    std::cout << "test_add_member_already_exists passed" << std::endl;
}

// tests member with no permissions attempting to add someone they're connected to
void test_add_member_denied_no_permission() {
    TagRegistry registry;
    // account 100 connects to 200, 200 connects to 300
    registry.add_connection(100, 200);
    registry.add_connection(200, 300);
    // account 100 creates tag 1 and adds account 200
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    // 100 and 200 disconnect
    registry.remove_connection(100, 200);

    // account 200 fails to add account 300 as it doesn't have permissions anymore
    assert(!registry.add_member(1, 200, 300));

    // gets the members of tag 1 and asserts there are 2
    auto members = registry.get_members(1);
    assert(members.size() == 2);

    std::cout << "test_add_member_denied_no_permission passed" << std::endl;
}

// tests adding a member who isn't connected to the inviter
void test_add_member_denied_no_connection() {
    TagRegistry registry;
    // account 100 creates tag 1
    registry.create_tag(1, 100);

    // account 100 fails to add account 200 as no connection
    assert(!registry.add_member(1, 100, 200));

    auto members = registry.get_members(1);
    assert(members.size() == 1);

    std::cout << "test_add_member_denied_no_connection passed" << std::endl;
}

// test setting the inviter permission for a member and having that member invite another member
void test_add_member_with_inviter_flag() {
    TagRegistry registry;
    // account 100 connects to 200, 200 connects to 300
    registry.add_connection(100, 200);
    registry.add_connection(200, 300);
    // account 100 creates tag 1, adds 200, and gives 200 inviter permission
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.set_permissions(1, 100, 200, FLAG_INVITER);

    // assert 200 can add 300 to the tag since they have the permissions and are connected
    assert(registry.add_member(1, 200, 300));

    // asserts there are three members in the tag
    auto members = registry.get_members(1);
    assert(members.size() == 3);

    std::cout << "test_add_member_with_inviter_flag passed" << std::endl;
}

// tests removing a member from the tag
void test_remove_member() {
    TagRegistry registry;
    // account 100 connects to 200, creates tag 1, adds 200, and removes 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    assert(registry.remove_member(1, 100, 200));

    // gets members of tag 1, asserts that there's only one and it's not the one we removed
    auto members = registry.get_members(1);
    assert(members.size() == 1);
    assert(members.contains(100));
    assert(!members.contains(200));

    std::cout << "test_remove_member passed" << std::endl;
}

// tests account without permissions tries to remove a member
void test_remove_member_denied_no_permission() {
    TagRegistry registry;
    // account 100 connects to 200 and 300, creates tag 1, adds 200 and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);

    // account 200 fails to remove account 300
    assert(!registry.remove_member(1, 200, 300));

    // asserts we still have all the members
    auto members = registry.get_members(1);
    assert(members.size() == 3);

    std::cout << "test_remove_member_denied_no_permission passed" << std::endl;
}

// tests account with kicker flag removing another member
void test_remove_member_with_kicker_flag() {
    TagRegistry registry;
    // account 100 connects to 200 and 300, creates tag 1, adds 200 and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);
    // account 100 gives 200 the kicker permission
    registry.set_permissions(1, 100, 200, FLAG_KICKER);

    // account 200 can now remove account 300
    assert(registry.remove_member(1, 200, 300));

    // asserts we have two members now
    auto members = registry.get_members(1);
    assert(members.size() == 2);

    std::cout << "test_remove_member_with_kicker_flag passed" << std::endl;
}

// tests account attempting to remove the creator of the tag
void test_remove_creator_denied() {
    TagRegistry registry;
    // connects account 100 and 200, 100 creates tag 1 and adds 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // account 200 can't kick account 100
    assert(!registry.remove_member(1, 200, 100));

    // assert the creator's inside of the tag
    auto members = registry.get_members(1);
    assert(members.contains(100));

    std::cout << "test_remove_creator_denied passed" << std::endl;
}

// tests an account removing itself
void test_self_leave() {
    TagRegistry registry;
    // account 100 connects to 200, creates tag 1 and adds account 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // account 200 removes themselves
    assert(registry.remove_member(1, 200, 200));

    // assert there's only one person left in the cache and it's not the person who left
    auto members = registry.get_members(1);
    assert(members.size() == 1);
    assert(!members.contains(200));

    std::cout << "test_self_leave passed" << std::endl;
}

// tests the creator removing itself
void test_creator_cannot_self_leave() {
    TagRegistry registry;
    // account 100 creating tag 1
    registry.create_tag(1, 100);

    // 100 can't remove itself as it's the creator
    assert(!registry.remove_member(1, 100, 100));

    // asserts all the members are still there
    auto members = registry.get_members(1);
    assert(members.contains(100));

    std::cout << "test_creator_cannot_self_leave passed" << std::endl;
}

// tests getting a member of a fake tag
void test_get_members_nonexistent_tag() {
    TagRegistry registry;
    // gets members from a fake tag asserts there are none
    auto members = registry.get_members(999);
    assert(members.empty());

    std::cout << "test_get_members_nonexistent_tag passed" << std::endl;
}

// tests the permissions of two connected members
void test_fully_connected_two_connected() {
    TagRegistry registry;
    // connects 100 and 200
    registry.add_connection(100, 200);
    // 100 creates tag 1, adds 200
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // both are fully connected and can write
    assert(registry.can_write(1, 100));
    assert(registry.can_write(1, 200));

    std::cout << "test_fully_connected_two_connected passed" << std::endl;
}

// tests that disconnecting removes write permission
void test_not_connected_no_write() {
    TagRegistry registry;
    // connects 100 and 200, 100 creates tag 1 and adds 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // disconnects 100 and 200
    registry.remove_connection(100, 200);

    // 100 can write as it's the creator, 200 cannot
    assert(registry.can_write(1, 100));
    assert(!registry.can_write(1, 200));

    std::cout << "test_not_connected_no_write passed" << std::endl;
}

// tests permissions of three accounts that are partially connected
void test_three_members_partial_connectivity() {
    TagRegistry registry;
    // connects 100 to 200 and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);

    // 100 creates tag 1, adds 200 and 300
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);

    // 100 the creator can write but 200 and 300 can't because they aren't connected to each other
    assert(registry.can_write(1, 100));
    assert(!registry.can_write(1, 200));
    assert(!registry.can_write(1, 300));

    std::cout << "test_three_members_partial_connectivity passed" << std::endl;
}

// tests permissions of three accounts fully connected
void test_three_members_full_connectivity() {
    TagRegistry registry;
    // adds connections between 100, 200, and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    registry.add_connection(200, 300);

    // 100 creates tag 1, adds 200 and 300
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 200, 300);

    // every tag can write because they're fully connected
    assert(registry.can_write(1, 100));
    assert(registry.can_write(1, 200));
    assert(registry.can_write(1, 300));

    std::cout << "test_three_members_full_connectivity passed" << std::endl;
}

// tests that disconnecting then reconnecting restores write permission
void test_connection_added_after_tag() {
    TagRegistry registry;
    // connects 100 and 200, 100 creates tag 1 and adds 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // both can write while connected
    assert(registry.can_write(1, 100));
    assert(registry.can_write(1, 200));

    // disconnects 100 and 200, 200 loses write
    registry.remove_connection(100, 200);
    assert(!registry.can_write(1, 200));

    // reconnects 100 and 200, write restored
    registry.add_connection(100, 200);
    assert(registry.can_write(1, 100));
    assert(registry.can_write(1, 200));

    std::cout << "test_connection_added_after_tag passed" << std::endl;
}

// tests connectivity after removing a connection
void test_connection_removed() {
    TagRegistry registry;
    // connects 100 and 200, 100 creates tag 1 and invites 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // both are fully connected and can write
    assert(registry.can_write(1, 100));
    assert(registry.can_write(1, 200));

    // disconnects 100 and 200
    registry.remove_connection(100, 200);

    // 100 is creator and can write, 200 no longer can
    assert(registry.can_write(1, 100));
    assert(!registry.can_write(1, 200));

    std::cout << "test_connection_removed passed" << std::endl;
}

// tests removing an unconnected member, restoring connectivity to other members
void test_remove_unconnected_member_restores_connectivity() {
    TagRegistry registry;
    // 100 connects to 200 and 300, creates tag 1 and adds 200 and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);

    // 200 and 300 aren't connected to each other, so not fully connected
    assert(registry.can_write(1, 100));
    assert(!registry.can_write(1, 200));

    // 100 removes 300, now only 100 and 200 remain and they are connected
    registry.remove_member(1, 100, 300);

    // both 100 and 200 can write now
    assert(registry.can_write(1, 100));
    assert(registry.can_write(1, 200));

    std::cout << "test_remove_unconnected_member_restores_connectivity passed" << std::endl;
}

// tests that a fully connected account can set the permissions for other accounts
void test_set_permissions_by_fully_connected() {
    TagRegistry registry;
    // connects 200 to 100 and 300
    registry.add_connection(100, 200);
    registry.add_connection(200, 300);
    // 100 creates tag 1, adds 200 and 300
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 200, 300);

    // 200 is fully connected and can set permissions
    registry.set_permissions(1, 200, 300, FLAG_WRITER);

    // 300 has writer permission and can write now
    assert(registry.can_write(1, 300));

    std::cout << "test_set_permissions_by_fully_connected passed" << std::endl;
}

// a non-fully connected member without the permission can't set the permissions of other members
void test_set_permissions_denied_not_connected() {
    TagRegistry registry;
    // 100 connects to 200 and 300, creates tag 1, adds 200 and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);

    // 200 and 300 aren't connected, so 200 is not fully connected and has no FLAG_MODIFIER
    registry.set_permissions(1, 200, 300, FLAG_WRITER);

    // 300 still can't write because 200 isn't allowed to do that
    assert(!registry.can_write(1, 300));

    std::cout << "test_set_permissions_denied_not_connected passed" << std::endl;
}

// tests having explicit permission to modify permissions
void test_set_permissions_by_modifier() {
    TagRegistry registry;
    // connects 100 to 200 and 300, 100 creates tag 1, adds 200 and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);

    // 100 gives 200 modifier flag and disconnects
    registry.set_permissions(1, 100, 200, FLAG_MODIFIER);
    registry.remove_connection(100, 200);

    // 200 gives 300 writer flag, which it can do now as it is a modifier
    registry.set_permissions(1, 200, 300, FLAG_WRITER);
    assert(registry.can_write(1, 300));

    std::cout << "test_set_permissions_by_modifier passed" << std::endl;
}

// nonexistent accounts can't write to tags, and nonexistent tags can't be written to
void test_can_write_nonexistent() {
    TagRegistry registry;

    // no such tag 999 to write to
    assert(!registry.can_write(999, 100));

    // 100 creates tag 1, no such 999 to write to tag
    registry.create_tag(1, 100);
    assert(!registry.can_write(1, 999));

    std::cout << "test_can_write_nonexistent passed" << std::endl;
}

// tests setting permissions multiple times in a row
void test_last_write_wins() {
    TagRegistry registry;
    // 100, 200, 300 connected to each other, 200 fully connected
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    registry.add_connection(200, 300);
    registry.add_connection(200, 400);
    registry.add_connection(100, 400);
    // 100 creates tag 1, adds 200, 300, 400
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);
    registry.add_member(1, 100, 400);

    // 100 gives 400 write permissions and it can write
    registry.set_permissions(1, 100, 400, FLAG_WRITER);
    assert(registry.can_write(1, 400));

    // 200 sets 400 to default and it can no longer write
    registry.set_permissions(1, 200, 400, FLAG_DEFAULT);
    assert(!registry.can_write(1, 400));

    std::cout << "test_last_write_wins passed" << std::endl;
}

// tests accounts sharing multiple tags with a third member breaking full connectivity
void test_multiple_shared_tags() {
    TagRegistry registry;
    // 100 connects to 200 and 300
    registry.add_connection(100, 200);
    registry.add_connection(100, 300);
    // 100 creates tags 1 and 2, invites 200 and 300 to both
    registry.create_tag(1, 100);
    registry.create_tag(2, 100);
    registry.add_member(1, 100, 200);
    registry.add_member(1, 100, 300);
    registry.add_member(2, 100, 200);
    registry.add_member(2, 100, 300);

    // 200 and 300 aren't connected to each other, so not fully connected in either tag
    assert(!registry.can_write(1, 200));
    assert(!registry.can_write(2, 200));

    // connecting 200 and 300 makes both tags fully connected
    registry.add_connection(200, 300);

    // 200 can write in both tags
    assert(registry.can_write(1, 200));
    assert(registry.can_write(2, 200));

    std::cout << "test_multiple_shared_tags passed" << std::endl;
}

// tests transferring ownership of a tag
void test_transfer_ownership() {
    TagRegistry registry;
    // connects 100 to 200, 100 creates tag and adds 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // 100 transfers ownership to 200, 200 kicks 100
    assert(registry.transfer_ownership(1, 100, 200));
    assert(registry.remove_member(1, 200, 100));

    std::cout << "test_transfer_ownership passed" << std::endl;
}

// tests invalid transfers of ownership
void test_transfer_ownership_denied() {
    TagRegistry registry;
    // connects 100 to 200, 100 creates tag and adds 200
    registry.add_connection(100, 200);
    registry.create_tag(1, 100);
    registry.add_member(1, 100, 200);

    // 200 doesn't have ownership to give to 100, so 100 can't be kicked
    assert(!registry.transfer_ownership(1, 200, 100));
    assert(!registry.remove_member(1, 200, 100));

    std::cout << "test_transfer_ownership_denied passed" << std::endl;
}

// runs all tests
int main() {
    test_create_tag();
    test_add_member();
    test_add_member_already_exists();
    test_add_member_denied_no_permission();
    test_add_member_denied_no_connection();
    test_add_member_with_inviter_flag();
    test_remove_member();
    test_remove_member_denied_no_permission();
    test_remove_member_with_kicker_flag();
    test_remove_creator_denied();
    test_self_leave();
    test_creator_cannot_self_leave();
    test_get_members_nonexistent_tag();
    test_fully_connected_two_connected();
    test_not_connected_no_write();
    test_three_members_partial_connectivity();
    test_three_members_full_connectivity();
    test_connection_added_after_tag();
    test_connection_removed();
    test_remove_unconnected_member_restores_connectivity();
    test_set_permissions_by_fully_connected();
    test_set_permissions_denied_not_connected();
    test_set_permissions_by_modifier();
    test_can_write_nonexistent();
    test_last_write_wins();
    test_multiple_shared_tags();
    test_transfer_ownership();
    test_transfer_ownership_denied();

    std::cout << std::endl << "All tag registry tests passed!" << std::endl;
    return 0;
}