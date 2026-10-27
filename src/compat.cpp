#include "compat.h"

#include "ini.h"
#include "settings.h"

namespace Compat
{
    namespace
    {
        using VersionFn = int (*)();
        using EnabledFn = bool (*)();
        using TraversingFn = bool (*)(std::uint32_t);
        using ClaimFn = void (*)(std::uint32_t, float);
        using ActiveFlightsFn = std::uint32_t (*)(std::uint32_t*, std::uint32_t);

        constexpr int kFmnpcApiVersion = 1;
        constexpr const char* kFluidMotionIni = "Data/SKSE/Plugins/FluidMotion.ini";
        constexpr std::uint32_t kFlightCapacity = 32;  // NpcTraversalNG caps MaxActiveTraversals at 32

        Providers g_providers;
        EnabledFn g_fmnpcEnabled = nullptr;
        TraversingFn g_fmnpcTraversing = nullptr;
        ClaimFn g_fmnpcClaim = nullptr;
        ActiveFlightsFn g_ntFlights = nullptr;
        std::array<std::uint32_t, kFlightCapacity> g_flights{};
        std::uint32_t g_flightCount = 0;

        REX::W32::HMODULE Module(const wchar_t* a_name)
        {
            return REX::W32::GetModuleHandleW(a_name);
        }

        template <class T>
        T Export(REX::W32::HMODULE a_module, const char* a_name)
        {
            return reinterpret_cast<T>(REX::W32::GetProcAddress(a_module, a_name));
        }

        // The other mods' exports are called across a DLL boundary: a throw must never reach the game.
        template <class F, class R>
        R Guarded(F&& a_call, R a_fallback) noexcept
        {
            try {
                return a_call();
            } catch (...) {
                return a_fallback;
            }
        }
    }

    void Detect()
    {
        g_providers = {};
        g_fmnpcEnabled = nullptr;
        g_fmnpcTraversing = nullptr;
        g_fmnpcClaim = nullptr;
        g_ntFlights = nullptr;

        g_providers.fluidMotion = Module(L"FluidMotion.dll") != nullptr;
        g_providers.fluidMotionAnim = Module(L"FluidMotionAnimation.dll") != nullptr;
        g_providers.skyParkour = Module(L"SkyParkourNG.dll") != nullptr;
        g_providers.stepUpOnto = Module(L"StepUpOntoSKSE.dll") != nullptr;

        if (auto* fmnpc = Module(L"FluidMotionNPC.dll")) {
            g_providers.fluidMotionNpc = true;
            const auto version = Export<VersionFn>(fmnpc, "FMNPC_GetAPIVersion");
            if (version && Guarded([&] { return version(); }, 0) == kFmnpcApiVersion) {
                g_fmnpcEnabled = Export<EnabledFn>(fmnpc, "FMNPC_IsEnabled");
                g_fmnpcTraversing = Export<TraversingFn>(fmnpc, "FMNPC_IsTraversing");
                g_fmnpcClaim = Export<ClaimFn>(fmnpc, "FMNPC_Claim");
                if (!g_fmnpcEnabled || !g_fmnpcTraversing || !g_fmnpcClaim) {
                    g_fmnpcEnabled = nullptr;
                    g_fmnpcTraversing = nullptr;
                    g_fmnpcClaim = nullptr;
                    logger::warn("FluidMotionNPC API 1 is missing an export: running unlinked");
                }
            } else {
                logger::warn("FluidMotionNPC API version is not {}: running unlinked", kFmnpcApiVersion);
            }
        }

        if (auto* nt = Module(L"NpcTraversal.dll")) {
            g_providers.npcTraversal = true;
            g_ntFlights = Export<ActiveFlightsFn>(nt, "NPCT_ActiveFlights");
            g_providers.ntFlightsBound = g_ntFlights != nullptr;
            if (!g_ntFlights) {
                logger::warn("NpcTraversal.dll has no NPCT_ActiveFlights export: its flights are not visible, "
                             "relying on character-controller state instead");
            }
        }

        Reload();
    }

    void Reload()
    {
        g_providers.fmEnabled = false;
        g_providers.fmJump = false;
        g_providers.fmClimb = false;
        if (g_providers.fluidMotion) {
            // FluidMotion.dll has no typed API: its INI is the contract (it re-reads it on every load).
            const auto ini = Ini::Parse(kFluidMotionIni);
            g_providers.fmEnabled = Ini::Bool(ini, "fluidmotion/benabled", true);
            const bool pathing = g_providers.fmEnabled && Ini::Bool(ini, "fluidmotion/bpathfinding", true);
            g_providers.fmJump = pathing && Ini::Bool(ini, "jump/benabled", true);
            g_providers.fmClimb = g_providers.fmEnabled && Ini::Bool(ini, "npcclimb/benabled", true) &&
                                  Ini::Bool(ini, "npcclimb/bclimb", true);
        }
        g_providers.fmnpcEnabled = g_fmnpcEnabled && Guarded([] { return g_fmnpcEnabled(); }, false);
    }

    const Providers& Get()
    {
        return g_providers;
    }

    bool JumpsOwnedByOther()
    {
        switch (Settings::GetSingleton()->compatMode) {
        case Settings::CompatMode::Force:
            return false;
        case Settings::CompatMode::Passive:
            return true;
        case Settings::CompatMode::Auto:
        default:
            break;
        }
        return g_providers.npcTraversal || g_providers.fmnpcEnabled || g_providers.fmJump;
    }

    bool AssistOnly()
    {
        switch (Settings::GetSingleton()->compatMode) {
        case Settings::CompatMode::Force:
            return false;
        case Settings::CompatMode::Passive:
            return true;
        case Settings::CompatMode::Auto:
        default:
            break;
        }
        return g_providers.npcTraversal || g_providers.fmnpcEnabled || g_providers.fmJump || g_providers.fmClimb;
    }

    void BeginFrame()
    {
        g_flightCount = 0;
        if (g_ntFlights) {
            g_flightCount = std::min(Guarded([] { return g_ntFlights(g_flights.data(), kFlightCapacity); }, 0u),
                                     kFlightCapacity);
        }
    }

    bool ExternalTraversing(RE::FormID a_id)
    {
        for (std::uint32_t i = 0; i < g_flightCount; ++i) {
            if (g_flights[i] == a_id) {
                return true;
            }
        }
        if (g_fmnpcTraversing && g_providers.fmnpcEnabled) {
            return Guarded([&] { return g_fmnpcTraversing(a_id); }, false);
        }
        return false;
    }

    void Claim(RE::FormID a_id, float a_seconds)
    {
        if (g_fmnpcClaim && a_id && std::isfinite(a_seconds)) {
            Guarded([&] { g_fmnpcClaim(a_id, std::max(a_seconds, 0.0f)); return true; }, false);
        }
    }

    std::string Summary()
    {
        const auto& p = g_providers;
        const char* mode = "auto";
        switch (Settings::GetSingleton()->compatMode) {
        case Settings::CompatMode::Force: mode = "force"; break;
        case Settings::CompatMode::Passive: mode = "passive"; break;
        default: break;
        }
        return std::format(
            "mode={} | FluidMotion={} (jump={} climb={}) FluidMotionAnimation={} FluidMotionNPC={} (api={}) "
            "NpcTraversal={} (flights={}) SkyParkourNG={} StepUpOnto={} => ownJumps={} assistOnly={}",
            mode, p.fluidMotion, p.fmJump, p.fmClimb, p.fluidMotionAnim, p.fluidMotionNpc, p.fmnpcEnabled,
            p.npcTraversal, p.ntFlightsBound, p.skyParkour, p.stepUpOnto, !JumpsOwnedByOther(), AssistOnly());
    }
}
