#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Hooks
{
    /// Swap one vtable slot, but only if the slot currently points into the game's
    /// executable image. A slot already owned by another module is left untouched
    /// and reported (runtime, slot, owner module, bytes) so the failure is diagnosable.
    /// Returns the original function address, or 0 when the swap was refused.
    [[nodiscard]] std::uintptr_t SwapVirtual(std::uintptr_t a_vtable, std::size_t a_index, void* a_thunk,
                                             std::string_view a_name);

    /// PlayerCharacter::Update drives the per-frame tick. Call once, at kDataLoaded.
    [[nodiscard]] bool InstallFrameHook();
}
