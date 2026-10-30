#pragma once

#include <cstdint>

namespace PluginVersion
{
    inline constexpr std::uint16_t Major = 3;
    inline constexpr std::uint16_t Minor = 0;
    inline constexpr std::uint16_t Patch = 0;
    // Match REL::Version::pack(), used by SKSEPlugin_Version / Query on SE 1.5.97.
    inline constexpr std::uint32_t Legacy =
        (static_cast<std::uint32_t>(Major) << 24) |
        (static_cast<std::uint32_t>(Minor) << 16) |
        (static_cast<std::uint32_t>(Patch) << 4);
    inline constexpr const char* String = "3.0.1";
}
