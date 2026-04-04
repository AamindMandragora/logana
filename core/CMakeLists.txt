add_library(logana_core INTERFACE)

target_include_directories(logana_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(logana_core INTERFACE
    -Wall -Wextra -Wpedantic -Werror
)