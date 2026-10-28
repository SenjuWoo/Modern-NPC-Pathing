#include "hooks.h"

#include "pathing.h"

namespace Hooks
{
    namespace
    {
        // Owner module of an address, or empty when it is not inside any loaded image.
        std::string OwnerModule(std::uintptr_t a_address)
        {
            REX::W32::MEMORY_BASIC_INFORMATION info{};
            if (REX::W32::VirtualQuery(reinterpret_cast<const void*>(a_address), &info, sizeof(info)) != sizeof(info) ||
                !info.allocationBase) {
                return {};
            }
            wchar_t path[260]{};
            const auto n = REX::W32::GetModuleFileNameW(static_cast<REX::W32::HMODULE>(info.allocationBase), path, 260);
            if (n == 0) {
                return {};
            }
            return std::filesystem::path(std::wstring(path, n)).filename().string();
        }

        // Committed memory with any execute permission (PAGE_EXECUTE | _READ | _READWRITE | _WRITECOPY).
        bool IsExecutable(std::uintptr_t a_address)
        {
            if (!a_address) {
                return false;
            }
            REX::W32::MEMORY_BASIC_INFORMATION info{};
            if (REX::W32::VirtualQuery(reinterpret_cast<const void*>(a_address), &info, sizeof(info)) != sizeof(info)) {
                return false;
            }
            return info.state == 0x1000 && (info.protect & 0xF0) != 0;
        }

        bool InGameText(std::uintptr_t a_address)
        {
            const auto text = REL::Module::get().segment(REL::Segment::textx);
            return a_address >= text.address() && a_address < text.address() + text.size();
        }

        // PlayerCharacter::Update — vfunc 0xAD on SE and AE, shifted to 0xAF in VR.
        struct PlayerUpdate
        {
            using Fn = void (*)(RE::PlayerCharacter*, float);
            static inline Fn original{ nullptr };

            // A failure in our tick turns the plugin off for the session instead of taking the game down.
            static inline bool faulted = false;

            // Stop for the session, but first hand back every NPC mid-parkour or mid-leap
            // (controller simulation, in-air state), since nothing will tick them again.
            static void Fault(const char* a_what)
            {
                faulted = true;
                logger::critical("NPCPathingNG stopped for this session: {}", a_what);
                try {
                    PathingManager::GetSingleton()->Reset();
                } catch (...) {
                    logger::critical("NPCPathingNG: releasing driven NPCs after the fault also failed");
                }
            }

            static void Thunk(RE::PlayerCharacter* a_this, float a_delta)
            {
                original(a_this, a_delta);
                if (faulted) {
                    return;
                }
                try {
                    PathingManager::GetSingleton()->OnFrame(a_delta);
                } catch (const std::exception& e) {
                    Fault(e.what());
                } catch (...) {
                    Fault("unknown exception");
                }
            }
        };
    }

    std::uintptr_t SwapVirtual(std::uintptr_t a_vtable, std::size_t a_index, void* a_thunk, std::string_view a_name)
    {
        const auto slot = a_vtable + sizeof(void*) * a_index;
        const auto current = *reinterpret_cast<const std::uintptr_t*>(slot);
        const auto text = REL::Module::get().segment(REL::Segment::textx);
        const std::string owner = InGameText(current) ? std::string{ "SkyrimSE.exe" } : OwnerModule(current);

        if (!InGameText(current)) {
            // Another SKSE plugin swapped this slot before us (NpcTraversalNG and Fluid Motion hook
            // PlayerCharacter::Update too). Chaining behind it is the normal, safe case — every
            // vtable swap here calls the saved original first — as long as the target is real code.
            if (!IsExecutable(current)) {
                logger::error(
                    "hook {} ABORTED: runtime={} slot={:#x} candidate={:#x} owner_module={} expected_module=SkyrimSE.exe "
                    "text_range={:#x}-{:#x} reason=candidate_not_executable action=ABORTED",
                    a_name, REL::Module::get().version().string(), slot, current, owner.empty() ? "<none>" : owner,
                    text.address(), text.address() + text.size());
                return 0;
            }
            logger::info("hook {}: slot already owned by {} — chaining behind it", a_name,
                         owner.empty() ? "<unknown module>" : owner);
        }
        REL::Relocation<std::uintptr_t> vtbl{ a_vtable };
        return vtbl.write_vfunc(a_index, reinterpret_cast<std::uintptr_t>(a_thunk));
    }

    bool InstallFrameHook()
    {
        const auto original = SwapVirtual(RE::VTABLE_PlayerCharacter[0].address(),
                                          REL::Relocate<std::size_t>(0xAD, 0xAD, 0xAF),
                                          reinterpret_cast<void*>(&PlayerUpdate::Thunk), "PlayerCharacter::Update"sv);
        if (!original) {
            return false;
        }
        PlayerUpdate::original = reinterpret_cast<PlayerUpdate::Fn>(original);
        logger::info("hook PlayerCharacter::Update installed (runtime {})", REL::Module::get().version().string());
        return true;
    }
}
