#pragma once

#include "RE/A/Actor.h"
#include "RE/N/NiPoint3.h"

/// Standalone NPC hurdle / vault / drop: a stuck humanoid is flown along a checked ballistic arc
/// (arc.h) with the vanilla jump events, then handed back to the navmesh. No animation files.
///
/// Used only when no other mod owns NPC jumps (see Compat::JumpsOwnedByOther). Fluid Motion and
/// NpcTraversalNG already do this, with more polish; this is the same capability for users who
/// run neither. Humanoid NPCs only (the plugin's static filter), because only their graphs carry
/// the jump events.
namespace Leap
{
    /// Plan and launch a leap for a_actor, facing a_fwd. a_goal, when known (combat target, or the
    /// player for a follower), gates drops and keeps the leap pointed toward it.
    /// Costs up to ~60 rays; the caller meters it. Returns true if the actor is now in flight.
    bool TryStart(RE::Actor* a_actor, const RE::NiPoint3& a_fwd, const RE::NiPoint3* a_goal, double a_now);

    /// Advance every flight. Main thread, once per frame. a_now is the plugin clock (seconds).
    void Update(float a_delta, double a_now);

    [[nodiscard]] bool IsFlying(RE::FormID a_id);
    [[nodiscard]] std::size_t ActiveCount();

    /// Release every actor mid-flight (disable, load, new game).
    void CancelAll();

    /// Seconds since this actor last finished or was released from a flight (large if never).
    [[nodiscard]] double SinceLastFlight(RE::FormID a_id, double a_now);
}
