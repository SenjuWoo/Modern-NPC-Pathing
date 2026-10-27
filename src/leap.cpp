#include "leap.h"

#include "arc.h"
#include "compat.h"
#include "npc_parkour.h"
#include "raycast.h"
#include "settings.h"

namespace Leap
{
    namespace
    {
        using npng::arc::Arc;
        using npng::arc::Vec3;

        constexpr float kMinLandingNormalZ = 0.7f;   // ~45 degrees
        constexpr float kLandEventLead = 0.18f;      // JumpLand goes out this long before touchdown
        constexpr float kMaxDisplacement = 150.0f;   // something else moved the actor: give it back
        constexpr double kRelaunchCooldown = 1.0;    // seconds after a flight before the next probe

        // Vanilla humanoid jump events (the same ones Fluid Motion and NpcTraversalNG send).
        const RE::BSFixedString& EvStanding() { static const RE::BSFixedString s{ "JumpStandingStart" }; return s; }
        const RE::BSFixedString& EvDirectional() { static const RE::BSFixedString s{ "JumpDirectionalStart" }; return s; }
        const RE::BSFixedString& EvFall() { static const RE::BSFixedString s{ "JumpFall" }; return s; }
        const RE::BSFixedString& EvLand() { static const RE::BSFixedString s{ "JumpLand" }; return s; }

        struct Flight
        {
            RE::ActorHandle handle;
            RE::FormID      id{ 0 };
            Arc             arc;
            float           t{ 0.0f };
            bool            landingSent{ false };
            const void*     world{ nullptr };
            RE::NiPoint3    lastPlaced;
        };

        std::vector<Flight>                  g_flights;
        std::unordered_map<RE::FormID, double> g_lastEnd;

        RE::NiPoint3 ToNi(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }
        Vec3 ToVec(const RE::NiPoint3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        Flight* Find(RE::FormID a_id)
        {
            for (auto& f : g_flights) {
                if (f.id == a_id) {
                    return &f;
                }
            }
            return nullptr;
        }

        bool IsValidSurface(const Raycast::Result& a_hit)
        {
            return a_hit.didHit && a_hit.normalZ >= kMinLandingNormalZ && !Raycast::IsActorHit(a_hit) &&
                   !NpcParkour::LayerExcludedForClimb(a_hit.layer) && !NpcParkour::FormExcluded(a_hit.hitRef);
        }

        bool UnderWater(RE::Actor* a_actor, const RE::NiPoint3& a_point)
        {
            auto* cell = a_actor->GetParentCell();
            if (!cell) {
                return false;
            }
            float level = -200000.0f;
            cell->GetWaterHeight(a_point, level);
            return a_point.z < level - 10.0f;
        }

        struct Plan
        {
            Arc          arc;
            RE::NiPoint3 landing;
        };

        // The landing must lie toward the goal (when there is one), never away from it.
        bool TowardGoal(const RE::NiPoint3& a_from, const RE::NiPoint3& a_landing, const RE::NiPoint3* a_goal)
        {
            if (!a_goal) {
                return true;
            }
            const RE::NiPoint3 before(a_goal->x - a_from.x, a_goal->y - a_from.y, 0.0f);
            const RE::NiPoint3 after(a_goal->x - a_landing.x, a_goal->y - a_landing.y, 0.0f);
            return after.Length() < before.Length() - 10.0f;
        }

        // Arc rays at feet, waist and head height between sampled points; any static hit aborts.
        bool ArcClear(RE::Actor* a_actor, const Arc& a_arc, float a_scale)
        {
            constexpr int kSegments = 10;
            Vec3 prev = a_arc.At(0.0f);
            for (int i = 1; i <= kSegments; ++i) {
                const Vec3 next = a_arc.At(a_arc.duration * static_cast<float>(i) / static_cast<float>(kSegments));
                for (float h : { 25.0f, 65.0f, 105.0f }) {
                    const RE::NiPoint3 from = ToNi(prev) + RE::NiPoint3(0.0f, 0.0f, h * a_scale);
                    RE::NiPoint3 delta = ToNi(next) + RE::NiPoint3(0.0f, 0.0f, h * a_scale) - from;
                    const float len = delta.Length();
                    if (len < 1.0f) {
                        continue;
                    }
                    delta = delta / len;
                    const auto hit = Raycast::Cast(a_actor, from, delta, len);
                    if (hit.didHit && !Raycast::IsActorHit(hit)) {
                        return false;
                    }
                }
                prev = next;
            }
            return true;
        }

        // Blocked ahead by a fence, wall or ledge: find its top and a landing beyond it.
        std::optional<Plan> PlanObstacle(RE::Actor* a_actor, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_fwd,
                                         float a_scale, const RE::NiPoint3* a_goal)
        {
            const auto* s = Settings::GetSingleton();
            const float minH = s->leapMinHeight * a_scale;
            const float maxH = s->leapMaxHeight * a_scale;
            const RE::NiPoint3 down(0.0f, 0.0f, -1.0f);

            float blockDist = -1.0f;
            for (float h : { 25.0f, 85.0f }) {
                const auto hit = Raycast::Cast(a_actor, a_pos + RE::NiPoint3(0.0f, 0.0f, h * a_scale), a_fwd, 90.0f);
                if (hit.didHit && !Raycast::IsActorHit(hit)) {
                    blockDist = blockDist < 0.0f ? hit.distance : std::min(blockDist, hit.distance);
                }
            }
            if (blockDist < 0.0f) {
                return std::nullopt;
            }

            // March across the obstacle with down-rays from above the tallest allowed top.
            float topZ = -1.0e9f;
            float topD = 0.0f;
            for (float d = std::max(0.0f, blockDist - 8.0f); d <= blockDist + 90.0f; d += 12.0f) {
                RE::NiPoint3 from = a_pos + a_fwd * d;
                from.z = a_pos.z + maxH + 40.0f;
                const auto hit = Raycast::Cast(a_actor, from, down, maxH + 70.0f);
                if (!hit.didHit || Raycast::IsActorHit(hit) || NpcParkour::FormExcluded(hit.hitRef)) {
                    continue;
                }
                const float h = hit.hitPos.z - a_pos.z;
                if (h > maxH) {
                    return std::nullopt;  // taller than allowed
                }
                if (h >= minH) {
                    if (hit.hitPos.z > topZ) {
                        topZ = hit.hitPos.z;
                        topD = d;
                    } else if (hit.hitPos.z < topZ - 25.0f) {
                        break;  // past the top
                    }
                }
            }
            if (topZ < -1.0e8f) {
                return std::nullopt;  // nothing tall enough: the navmesh can walk this
            }

            for (float offset : { 30.0f, 50.0f, 75.0f, 105.0f }) {
                const float d = topD + offset;
                if (d > s->leapMaxDistance) {
                    break;
                }
                RE::NiPoint3 from = a_pos + a_fwd * d;
                from.z = topZ + 10.0f;
                const auto hit = Raycast::Cast(a_actor, from, down, topZ + 10.0f - (a_pos.z - s->leapMaxDrop));
                if (!IsValidSurface(hit)) {
                    continue;
                }
                const float rise = hit.hitPos.z - a_pos.z;
                if (rise > maxH + 10.0f || rise < -s->leapMaxDrop) {
                    continue;
                }
                if (UnderWater(a_actor, hit.hitPos) || !TowardGoal(a_pos, hit.hitPos, a_goal) ||
                    !Raycast::LandingIsClear(a_actor, hit.hitPos.z, hit.hitPos.x, hit.hitPos.y)) {
                    continue;
                }
                const RE::NiPoint3 landing(hit.hitPos.x, hit.hitPos.y, hit.hitPos.z + 2.0f);
                const auto arc = npng::arc::Solve(ToVec(a_pos), ToVec(landing), topZ, {}, std::clamp(topD / d, 0.0f, 1.0f));
                if (arc && ArcClear(a_actor, *arc, a_scale)) {
                    return Plan{ *arc, landing };
                }
            }
            return std::nullopt;
        }

        // Floor ends ahead and the goal is below: step off the edge to a checked landing.
        std::optional<Plan> PlanDrop(RE::Actor* a_actor, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_fwd,
                                     float a_scale, const RE::NiPoint3* a_goal)
        {
            const auto* s = Settings::GetSingleton();
            if (!a_goal || s->leapMaxDrop < 60.0f || a_goal->z > a_pos.z - 60.0f) {
                return std::nullopt;
            }
            const RE::NiPoint3 down(0.0f, 0.0f, -1.0f);

            // Floor still present just ahead: this is not an edge.
            const auto floor = Raycast::Cast(a_actor, a_pos + a_fwd * 40.0f + RE::NiPoint3(0.0f, 0.0f, 20.0f), down, 60.0f);
            if (floor.didHit) {
                return std::nullopt;
            }
            for (float d = 50.0f; d <= s->leapMaxDistance; d += 20.0f) {
                const RE::NiPoint3 from = a_pos + a_fwd * d + RE::NiPoint3(0.0f, 0.0f, 20.0f);
                const auto hit = Raycast::Cast(a_actor, from, down, s->leapMaxDrop + 40.0f);
                if (!IsValidSurface(hit)) {
                    continue;
                }
                const float drop = a_pos.z - hit.hitPos.z;
                if (drop < 40.0f || drop > s->leapMaxDrop) {
                    continue;
                }
                if (UnderWater(a_actor, hit.hitPos) || !TowardGoal(a_pos, hit.hitPos, a_goal) ||
                    !Raycast::LandingIsClear(a_actor, hit.hitPos.z, hit.hitPos.x, hit.hitPos.y)) {
                    continue;
                }
                const RE::NiPoint3 landing(hit.hitPos.x, hit.hitPos.y, hit.hitPos.z + 2.0f);
                const auto arc = npng::arc::Solve(ToVec(a_pos), ToVec(landing), a_pos.z, {}, -1.0f);
                if (arc && ArcClear(a_actor, *arc, a_scale)) {
                    return Plan{ *arc, landing };
                }
            }
            return std::nullopt;
        }

        void Place(RE::Actor* a_actor, Flight& a_flight, const Vec3& a_pos, const Vec3& a_vel,
                   RE::hkpCharacterStateType a_state)
        {
            const RE::NiPoint3 pos = ToNi(a_pos);
            a_actor->SetPosition(pos, true);
            if (std::abs(a_vel.x) + std::abs(a_vel.y) > 1.0f) {
                a_actor->SetHeading(std::atan2(a_vel.x, a_vel.y));  // Skyrim heading: 0 = +Y, clockwise
            }
            a_flight.lastPlaced = pos;
            if (auto* ctrl = a_actor->GetCharController()) {
                const float scale = RE::bhkWorld::GetWorldScale();
                ctrl->context.currentState = a_state;
                ctrl->SetLinearVelocityImpl(RE::hkVector4(a_vel.x * scale, a_vel.y * scale, a_vel.z * scale, 0.0f));
                ctrl->fallStartHeight = a_pos.z;  // a planned flight never builds fall damage
                ctrl->fallTime = 0.0f;
            }
        }

        // Why a flight must end early. 0 = keep flying.
        enum class Stop { kNone, kGone, kInterrupted, kTaken, kMoved };

        Stop CheckFlight(RE::Actor* a_actor, const Flight& a_flight)
        {
            if (!a_actor || !a_actor->Is3DLoaded() || a_actor->IsDead()) {
                return Stop::kGone;
            }
            auto* cell = a_actor->GetParentCell();
            if (!cell || cell->GetbhkWorld() != a_flight.world || !a_actor->GetCharController()) {
                return Stop::kGone;
            }
            if (a_actor->IsInRagdollState() || a_actor->IsOnMount() || a_actor->IsInKillMove()) {
                return Stop::kInterrupted;
            }
            if (const auto* state = a_actor->AsActorState();
                state && (state->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal || state->IsStaggered() ||
                          state->IsSwimming() || state->IsBleedingOut())) {
                return Stop::kInterrupted;
            }
            if (Compat::ExternalTraversing(a_flight.id)) {
                return Stop::kTaken;
            }
            if ((a_actor->GetPosition() - a_flight.lastPlaced).Length() > kMaxDisplacement) {
                return Stop::kMoved;
            }
            return Stop::kNone;
        }

        // Hand the body back: the engine's own gravity finishes whatever is left of the fall.
        void Release(RE::Actor* a_actor)
        {
            if (!a_actor) {
                return;
            }
            if (auto* ctrl = a_actor->GetCharController()) {
                if (ctrl->context.currentState != RE::hkpCharacterStateType::kOnGround) {
                    ctrl->context.currentState = RE::hkpCharacterStateType::kInAir;
                }
            }
        }
    }

    bool TryStart(RE::Actor* a_actor, const RE::NiPoint3& a_fwd, const RE::NiPoint3* a_goal, double a_now)
    {
        const auto* s = Settings::GetSingleton();
        if (!a_actor || !s->enableLeap || Compat::JumpsOwnedByOther()) {
            return false;
        }
        const RE::FormID id = a_actor->GetFormID();
        if (g_flights.size() >= static_cast<std::size_t>(s->leapMaxActive) || Find(id) ||
            SinceLastFlight(id, a_now) < kRelaunchCooldown || Compat::ExternalTraversing(id)) {
            return false;
        }
        auto* ctrl = a_actor->GetCharController();
        if (!ctrl || ctrl->context.currentState != RE::hkpCharacterStateType::kOnGround) {
            return false;
        }
        auto* cell = a_actor->GetParentCell();
        if (!cell || !cell->GetbhkWorld()) {
            return false;
        }
        // Indoors the headroom and clutter make a ballistic arc a poor bet; parkour/teleport cover it.
        if (cell->IsInteriorCell() && s->parkourIndoorMode == 0) {
            return false;
        }

        float scale = a_actor->GetScale();
        if (scale <= 0.0f) {
            scale = 1.0f;
        }
        const RE::NiPoint3 pos = a_actor->GetPosition();

        auto plan = PlanObstacle(a_actor, pos, a_fwd, scale, a_goal);
        if (!plan) {
            plan = PlanDrop(a_actor, pos, a_fwd, scale, a_goal);
        }
        if (!plan) {
            return false;
        }

        Flight flight;
        flight.handle = a_actor->GetHandle();
        flight.id = id;
        flight.arc = plan->arc;
        flight.world = cell->GetbhkWorld();
        flight.lastPlaced = pos;

        const float speed = std::hypot(plan->arc.horizontalVelocity.x, plan->arc.horizontalVelocity.y);
        const bool accepted = a_actor->NotifyAnimationGraph(speed > 140.0f ? EvDirectional() : EvStanding());
        if (!accepted) {
            a_actor->NotifyAnimationGraph(EvFall());  // the graph refused the take-off: fall pose instead
        }
        Compat::Claim(id, plan->arc.duration + 0.5f);
        g_flights.push_back(std::move(flight));

        if (s->debugLogging) {
            const char* name = a_actor->GetName();
            logger::info("NPCPathingNG: leap {} ({:.0f},{:.0f},{:.0f}) -> ({:.0f},{:.0f},{:.0f}) in {:.2f}s",
                         name ? name : "?", pos.x, pos.y, pos.z, plan->landing.x, plan->landing.y, plan->landing.z,
                         plan->arc.duration);
        }
        return true;
    }

    void Update(float a_delta, double a_now)
    {
        for (auto it = g_flights.begin(); it != g_flights.end();) {
            auto actorPtr = it->handle.get();
            RE::Actor* actor = actorPtr ? actorPtr.get() : nullptr;

            const Stop stop = CheckFlight(actor, *it);
            if (stop != Stop::kNone) {
                if (stop != Stop::kGone) {
                    Release(actor);
                }
                g_lastEnd[it->id] = a_now;
                it = g_flights.erase(it);
                continue;
            }

            it->t += a_delta;
            if (it->t >= it->arc.duration) {
                // Touchdown: exactly on the checked landing, controller back on the ground.
                Place(actor, *it, it->arc.end, { 0.0f, 0.0f, 0.0f }, RE::hkpCharacterStateType::kOnGround);
                if (!it->landingSent) {
                    actor->NotifyAnimationGraph(EvLand());
                }
                g_lastEnd[it->id] = a_now;
                it = g_flights.erase(it);
                continue;
            }

            Place(actor, *it, it->arc.At(it->t), it->arc.VelocityAt(it->t), RE::hkpCharacterStateType::kInAir);
            if (!it->landingSent && it->t >= it->arc.duration - kLandEventLead) {
                it->landingSent = true;
                actor->NotifyAnimationGraph(EvLand());
            }
            ++it;
        }

        if (g_lastEnd.size() > 256) {
            std::erase_if(g_lastEnd, [&](const auto& a_pair) { return a_now - a_pair.second > 30.0; });
        }
    }

    bool IsFlying(RE::FormID a_id)
    {
        return Find(a_id) != nullptr;
    }

    std::size_t ActiveCount()
    {
        return g_flights.size();
    }

    void CancelAll()
    {
        for (auto& f : g_flights) {
            if (auto actorPtr = f.handle.get()) {
                Release(actorPtr.get());
            }
        }
        g_flights.clear();
        g_lastEnd.clear();
    }

    double SinceLastFlight(RE::FormID a_id, double a_now)
    {
        const auto it = g_lastEnd.find(a_id);
        return it == g_lastEnd.end() ? 1.0e9 : a_now - it->second;
    }
}
