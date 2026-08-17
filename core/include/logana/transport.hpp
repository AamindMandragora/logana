#pragma once

#include <functional>
#include "frame.hpp"

namespace logana {
    // forward declare callback function types
    using OnConnect = std::function<void(Connection)>;
    using OnDisconnect = std::function<void(Connection)>;
    using OnFrame = std::function<void(Connection, FrameType, const uint8_t*, uint32_t)>;
}

// import platform-specific server implementation
#ifdef __linux__
    #include "linux_server.hpp"
#elif defined(_WIN32)
    #include "windows_server.hpp"
#endif