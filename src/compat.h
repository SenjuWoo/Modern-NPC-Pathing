#pragma once

#include "RE/A/Actor.h"

#include <string>

/// Compatibility layer for the other mods that move NPCs over obstacles.
///
///  * Fluid Motion (megakuutti, Nexus 194680): FluidMotion.dll drives NPC jumps, routes
///    and climbs; FluidMotionAnimation.dll is its pose layer. FluidMotion.dll has no
///    exported API we can type-check, so its ownership is read from its INI.
///  * FluidMotionNPC (tonycubed2): exports the FMNPC_* C API (IsTraversing / Claim).
///  * NpcTraversalNG ("Enemies Can Jump and Vault"): exports NPCT_ActiveFlights.
///
/// All lookups are by module name after every plugin has loaded (kPostLoad), so none of
/// these mods is a hard dependency and load order does not matter.
namespace Compat
{
    struct Providers
    {
        bool fluidMotion = false;       // FluidMotion.dll
        bool fluidMotionAnim = false;   // FluidMotionAnimation.dll
        bool fluidMotionNpc = false;    // FluidMotionNPC.dll (FMNPC_* API)
        bool npcTraversal = false;      // NpcTraversal.dll
        bool skyParkour = false;        // SkyParkourNG.dll
        bool stepUpOnto = false;        // StepUpOntoSKSE.dll

        // From FluidMotion.ini (re-read on every game load, like Fluid Motion itself)
        bool fmEnabled = false;
        bool fmJump = false;            // [FluidMotion] bPathfinding and [Jump] bEnabled
        bool fmClimb = false;           // [NpcClimb] bEnabled and bClimb
        bool fmnpcEnabled = false;      // FMNPC_IsEnabled()
        bool ntFlightsBound = false;    // NPCT_ActiveFlights resolved
    };

    /// Resolve modules and exports. Call at kPostLoad.
    void Detect();

    /// Re-read FluidMotion.ini and FMNPC state. Call at kDataLoaded and on every game load.
    void Reload();

    [[nodiscard]] const Providers& Get();

    /// Another mod owns ballistic jumps/vaults/leaps for NPCs: this plugin's own leap stays off.
    [[nodiscard]] bool JumpsOwnedByOther();

    /// Another mod handles stuck NPCs first: this plugin's parkour, hop and teleport act only
    /// as a fallback after the other mod had its turns.
    [[nodiscard]] bool AssistOnly();

    /// Snapshot the external flight list once per frame (cheap). Call before ExternalTraversing.
    void BeginFrame();

    /// Fluid Motion / NpcTraversalNG is moving this actor right now.
    [[nodiscard]] bool ExternalTraversing(RE::FormID a_id);

    /// Ask FluidMotionNPC to leave the actor alone for a_seconds (no-op without it).
    void Claim(RE::FormID a_id, float a_seconds);

    /// One-line human summary for the log.
    [[nodiscard]] std::string Summary();
}
