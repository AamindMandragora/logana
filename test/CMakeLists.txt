add_executable(test_chunked_deque test_chunked_deque.cpp)

target_link_libraries(test_chunked_deque PRIVATE logana_core)

if(SANITIZER STREQUAL "asan")
    target_compile_options(test_chunked_deque PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_chunked_deque PRIVATE -fsanitize=address)
elseif(SANITIZER STREQUAL "tsan")
    target_compile_options(test_chunked_deque PRIVATE -fsanitize=thread)
    target_link_options(test_chunked_deque PRIVATE -fsanitize=thread)
elseif(SANITIZER STREQUAL "ubsan")
    target_compile_options(test_chunked_deque PRIVATE -fsanitize=undefined)
    target_link_options(test_chunked_deque PRIVATE -fsanitize=undefined)
endif()

add_test(NAME ChunkedDequeTests COMMAND test_chunked_deque)

add_executable(test_device_registry test_device_registry.cpp)

target_link_libraries(test_device_registry PRIVATE logana_core)

if(SANITIZER STREQUAL "asan")
    target_compile_options(test_device_registry PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_device_registry PRIVATE -fsanitize=address)
elseif(SANITIZER STREQUAL "tsan")
    target_compile_options(test_device_registry PRIVATE -fsanitize=thread)
    target_link_options(test_device_registry PRIVATE -fsanitize=thread)
elseif(SANITIZER STREQUAL "ubsan")
    target_compile_options(test_device_registry PRIVATE -fsanitize=undefined)
    target_link_options(test_device_registry PRIVATE -fsanitize=undefined)
endif()

add_test(NAME DeviceRegistryTests COMMAND test_device_registry)

add_executable(test_tag_registry test_tag_registry.cpp)

target_link_libraries(test_tag_registry PRIVATE logana_core)

if(SANITIZER STREQUAL "asan")
    target_compile_options(test_tag_registry PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_tag_registry PRIVATE -fsanitize=address)
elseif(SANITIZER STREQUAL "tsan")
    target_compile_options(test_tag_registry PRIVATE -fsanitize=thread)
    target_link_options(test_tag_registry PRIVATE -fsanitize=thread)
elseif(SANITIZER STREQUAL "ubsan")
    target_compile_options(test_tag_registry PRIVATE -fsanitize=undefined)
    target_link_options(test_tag_registry PRIVATE -fsanitize=undefined)
endif()

add_test(NAME TagRegistryTests COMMAND test_tag_registry)

add_executable(test_message_store test_message_store.cpp)

target_link_libraries(test_message_store PRIVATE logana_core)

if(SANITIZER STREQUAL "asan")
    target_compile_options(test_message_store PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_message_store PRIVATE -fsanitize=address)
elseif(SANITIZER STREQUAL "tsan")
    target_compile_options(test_message_store PRIVATE -fsanitize=thread)
    target_link_options(test_message_store PRIVATE -fsanitize=thread)
elseif(SANITIZER STREQUAL "ubsan")
    target_compile_options(test_message_store PRIVATE -fsanitize=undefined)
    target_link_options(test_message_store PRIVATE -fsanitize=undefined)
endif()

add_test(NAME MessageStoreTests COMMAND test_message_store)

add_executable(test_relay_router test_relay_router.cpp)

target_link_libraries(test_relay_router PRIVATE logana_core)

if(SANITIZER STREQUAL "asan")
    target_compile_options(test_relay_router PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_relay_router PRIVATE -fsanitize=address)
elseif(SANITIZER STREQUAL "tsan")
    target_compile_options(test_relay_router PRIVATE -fsanitize=thread)
    target_link_options(test_relay_router PRIVATE -fsanitize=thread)
elseif(SANITIZER STREQUAL "ubsan")
    target_compile_options(test_relay_router PRIVATE -fsanitize=undefined)
    target_link_options(test_relay_router PRIVATE -fsanitize=undefined)
endif()

add_test(NAME RelayRouterTests COMMAND test_relay_router)