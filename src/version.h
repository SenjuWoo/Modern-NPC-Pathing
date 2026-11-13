#pragma once

#include <cstdint>

// The version lives in one place: project(... VERSION x.y.z) in CMakeLists.txt, passed in as
// NPNG_VERSION_*. 3.0.1 shipped with a hand-edited string here and Patch still 0, so SKSE saw 3.0.0.
#define NPNG_STRINGIFY_(x) #x
#define NPNG_STRINGIFY(x) NPNG_STRINGIFY_(x)

namespace PluginVersion
{
    inline constexpr std::uint16_t Major = NPNG_VERSION_MAJOR;
    inline constexpr std::uint16_t Minor = NPNG_VERSION_MINOR;
    inline constexpr std::uint16_t Patch = NPNG_VERSION_PATCH;
    inline constexpr const char* String =
        NPNG_STRINGIFY(NPNG_VERSION_MAJOR) "." NPNG_STRINGIFY(NPNG_VERSION_MINOR) "." NPNG_STRINGIFY(NPNG_VERSION_PATCH);
}
