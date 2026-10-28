#pragma once
#include "RE/B/BSFixedString.h"

#include <cstdint>

/// Parkour types — must match SkyParkour V3's behavior graph state machine values
/// (ParkourType.h in the SkyParkour source).
enum class NpcParkourType : std::int32_t {
    NoLedge  = -1, Failed   = 0, Grab     = 1, Vault    = 2,
    StepLow  = 3,  StepHigh = 4, Low      = 5, Medium   = 6,
    High     = 7,  Highest  = 8
};

inline constexpr bool IsClimbType(NpcParkourType a_type)
{
    return a_type == NpcParkourType::Low || a_type == NpcParkourType::Medium ||
           a_type == NpcParkourType::High || a_type == NpcParkourType::Highest;
}

/// Height thresholds from SkyParkour's HardCodedVariables (Skyrim units).
/// Multiplied by actor scale at runtime.
namespace ParkourHeights {
    constexpr float climbMax    = 250.0f;
    constexpr float climbMin    = 30.0f;
    constexpr float vaultMax    = 120.0f;
    constexpr float vaultMin    = 42.0f;
    constexpr float highestLim  = 220.0f;
    constexpr float highLim     = 170.0f;
    constexpr float medLim      = 130.0f;
    constexpr float lowLim      = 80.0f;
    constexpr float highStepLim = 60.0f;

    // Ending elevation of each animation's root motion (SkyParkour HardCodedVariables).
    // The anim start position is placed ledge.z - elevation so root motion ends on the ledge.
    constexpr float highestElev  = 250.0f;
    constexpr float highElev     = 200.0f;
    constexpr float medElev      = 153.0f;
    constexpr float lowElev      = 110.0f;
    constexpr float stepHighElev = 70.0f;
    constexpr float stepLowElev  = 50.0f;
    constexpr float vaultElev    = 60.0f;
}

/// SkyParkour behavior graph variable / anim event names (BehaviorGraph.h in the SkyParkour source).
/// Interned once: the graph calls take BSFixedString, and building one from a literal on every
/// call goes through the global string pool each time.
namespace SkyParkourGraph {
    // Anim events
    inline const RE::BSFixedString& NotifyStart() { static const RE::BSFixedString s{ "SkyParkour" }; return s; }            // begin a parkour action
    inline const RE::BSFixedString& EvInterrupt() { static const RE::BSFixedString s{ "SkyParkour_Interrupt" }; return s; }  // safe force-stop (never send SkyParkour_Stop from outside — recursion crash)

    // Graph variables
    inline const RE::BSFixedString& VarLedge() { static const RE::BSFixedString s{ "SkyParkourLedge" }; return s; }               // int, NpcParkourType
    inline const RE::BSFixedString& VarOngoing() { static const RE::BSFixedString s{ "SkyParkourOngoing" }; return s; }           // bool, owned by the graph — never write it
    inline const RE::BSFixedString& VarLowerBody() { static const RE::BSFixedString s{ "SkyParkourLowerBody" }; return s; }       // bool, lower-body-only steps when weapon out
    inline const RE::BSFixedString& VarTPPInstalled() { static const RE::BSFixedString s{ "SkyParkourTPPInstalled" }; return s; } // bool, third-person behavior patch present
}
