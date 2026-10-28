#include "npc_parkour.h"
#include "raycast.h"
#include "settings.h"

#include "RE/A/ActorState.h"
#include "RE/B/bhkCharacterController.h"
#include "RE/H/hkpCharacterState.h"
#include "RE/T/TESObjectCELL.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kActorHeight = 120.0f;
}

namespace NpcParkour
{
    // Ledge-point layers that are invalid for climbing (SkyParkour ClimbLayerExclusionList).
    bool LayerExcludedForClimb(RE::COL_LAYER a_layer)
    {
        switch (a_layer) {
        case RE::COL_LAYER::kNonCollidable:
        case RE::COL_LAYER::kCharController:
        case RE::COL_LAYER::kWeapon:
        case RE::COL_LAYER::kProjectile:
        case RE::COL_LAYER::kTransparent:
        case RE::COL_LAYER::kClutter:
        case RE::COL_LAYER::kBiped:
        case RE::COL_LAYER::kActorZone:
        case RE::COL_LAYER::kDebrisLarge:
            return true;
        default:
            return false;
        }
    }

    // Ledge-point layers that make a vault point invalid (SkyParkour VaultDownRayList).
    bool LayerExcludedForVault(RE::COL_LAYER a_layer)
    {
        switch (a_layer) {
        case RE::COL_LAYER::kWeapon:
        case RE::COL_LAYER::kProjectile:
        case RE::COL_LAYER::kCharController:
        case RE::COL_LAYER::kClutter:
        case RE::COL_LAYER::kBiped:
        case RE::COL_LAYER::kDeadBip:
            return true;
        default:
            return false;
        }
    }

    // Don't parkour onto actors, doors or activators (SkyParkour form exclusions).
    bool FormExcluded(RE::TESObjectREFR* a_ref)
    {
        if (!a_ref) {
            return false;
        }
        auto* base = a_ref->GetBaseObject();
        if (!base) {
            return false;
        }
        switch (base->GetFormType()) {
        case RE::FormType::NPC:
        case RE::FormType::Door:
        case RE::FormType::Activator:
            return true;
        // Clutter is never a valid landing surface. Barrels and urns are
        // Containers, crates and physics props are MovableStatics: an NPC that
        // vaults onto one is left standing on an unnavmeshed prop it can't path
        // off, or gets shoved around by havok. Reported as "NPCs vault random
        // barrels and just stand on top of them".
        case RE::FormType::Container:
        case RE::FormType::MovableStatic:
        case RE::FormType::Flora:
        case RE::FormType::Tree:
            return true;
        default:
            return false;
        }
    }

    // Never parkour into water (SkyParkour GetLedgePoint tail check).
    bool LedgeUnderWater(RE::Actor* a_actor, const RE::NiPoint3& a_ledge)
    {
        auto* cell = a_actor->GetParentCell();
        if (!cell) {
            return false;
        }
        float waterLevel = -200000.0f;
        cell->GetWaterHeight(a_actor->GetPosition(), waterLevel);
        constexpr float validWaterDepth = 10.0f;
        return a_ledge.z < waterLevel - validWaterDepth;
    }

    NpcParkourType VaultCheck(RE::Actor* a_actor, const RE::NiPoint3& a_fwd, float a_scale, RE::NiPoint3& a_outLedge)
    {
        const auto pos = a_actor->GetPosition();
        const float height = kActorHeight * a_scale;
        const RE::NiPoint3 up(0.0f, 0.0f, 1.0f);
        const RE::NiPoint3 down(0.0f, 0.0f, -1.0f);
        const RE::NiPoint3 side = a_fwd.Cross(up);
        const RE::NiPoint3 headPos = pos + RE::NiPoint3(0.0f, 0.0f, height);

        constexpr float vaultLength = 100.0f;
        const float maxElevIncrease = 70.0f * a_scale;
        const float minVault = ParkourHeights::vaultMin * a_scale;
        const float maxVault = ParkourHeights::vaultMax * a_scale;

        // Head-level forward obstruction (center + offset left/right) means no room to vault through.
        for (float off : { 0.0f, -15.0f, 15.0f }) {
            const RE::NiPoint3 start = headPos + side * off;
            if (Raycast::Cast(a_actor, start, a_fwd, vaultLength).didHit) {
                return NpcParkourType::NoLedge;
            }
        }

        // March forward casting down: find the vaultable surface, then a landing beyond it.
        bool foundVaulter = false;
        bool foundLanding = false;
        float vaultHeight = -10000.0f;
        float landingHeight = 10000.0f;
        const float vaultableGap = height + 100.0f * a_scale;
        RE::NiPoint3 lastStart;

        constexpr int downIterations = 20;
        for (int i = 0; i < downIterations; i++) {
            lastStart = pos + a_fwd * (5.0f * static_cast<float>(i));
            lastStart.z = headPos.z;

            const auto ray = Raycast::Cast(a_actor, lastStart, down, vaultableGap);
            if (!ray.didHit || LayerExcludedForVault(ray.layer) || FormExcluded(ray.hitRef)) {
                continue;
            }

            const float hitHeight = (headPos.z - ray.distance) - pos.z;
            if (hitHeight > maxVault) {
                return NpcParkourType::NoLedge;  // too high to vault
            }
            if (hitHeight > minVault) {
                if (hitHeight >= vaultHeight) {
                    vaultHeight = hitHeight;
                    foundLanding = false;
                }
                a_outLedge = ray.hitPos;
                foundVaulter = true;
            } else if (foundVaulter) {
                landingHeight = std::min(hitHeight, landingHeight);
                foundLanding = true;
                break;
            }
        }

        if (!foundVaulter || !foundLanding || landingHeight >= maxElevIncrease) {
            return NpcParkourType::NoLedge;
        }

        a_outLedge.z = pos.z + vaultHeight;

        // Obstruction above the vault surface?
        if (Raycast::Cast(a_actor, a_outLedge + RE::NiPoint3(0.0f, 0.0f, 5.0f), up, height * 0.5f).didHit) {
            return NpcParkourType::NoLedge;
        }

        // Obstruction ahead of the landing spot?
        const RE::NiPoint3 landingPoint = lastStart + down * (headPos.z - (pos.z + landingHeight));
        const RE::NiPoint3 landObsStart = landingPoint + RE::NiPoint3(0.0f, 0.0f, ParkourHeights::climbMin);
        if (Raycast::Cast(a_actor, landObsStart, a_fwd, vaultLength).didHit) {
            return NpcParkourType::NoLedge;
        }

        // Uneven / cluttered vault top?
        const RE::NiPoint3 sideStart = a_outLedge + RE::NiPoint3(0.0f, 0.0f, 10.0f);
        const float sideMax = 15.0f * a_scale;
        if (Raycast::Cast(a_actor, sideStart, side, sideMax).didHit ||
            Raycast::Cast(a_actor, sideStart, side * -1.0f, sideMax).didHit) {
            return NpcParkourType::NoLedge;
        }

        return NpcParkourType::Vault;
    }

    NpcParkourType ClimbCheck(RE::Actor* a_actor, const RE::NiPoint3& a_fwd, float a_scale,
                              float a_maxClimbHeight, RE::NiPoint3& a_outLedge)
    {
        const auto pos = a_actor->GetPosition();
        const float height = kActorHeight * a_scale;
        const RE::NiPoint3 up(0.0f, 0.0f, 1.0f);
        const RE::NiPoint3 down(0.0f, 0.0f, -1.0f);

        const float minLedge = ParkourHeights::climbMin * a_scale;
        const float maxLedge = std::min(a_maxClimbHeight, ParkourHeights::climbMax) * a_scale;
        if (maxLedge <= minLedge) {
            return NpcParkourType::NoLedge;
        }

        // Headroom directly above the actor.
        if (Raycast::Cast(a_actor, pos + RE::NiPoint3(0.0f, 0.0f, height), up, height).didHit) {
            return NpcParkourType::NoLedge;
        }

        RE::NiPoint3 fwdStart = pos;
        fwdStart.z += maxLedge;

        constexpr float fwdCheckStep = 5.0f;
        constexpr int fwdCheckIterations = 15;  // NPCs walk/run; SkyParkour uses speedPct*15+15
        constexpr float minLedgeFlatness = 0.5f;

        bool foundLedge = false;
        RE::NiPoint3 ledgePoint;
        float lastZ = minLedge;  // prevents grabbing a ledge through an obstruction

        for (int i = 0; i < fwdCheckIterations; i++) {
            const float probeLen = fwdCheckStep * static_cast<float>(i);

            // Probe forward at max ledge height; skip lengths that are blocked.
            const auto fwdRay = Raycast::Cast(a_actor, fwdStart, a_fwd, probeLen);
            if (fwdRay.didHit && fwdRay.distance < probeLen) {
                continue;
            }

            // Down ray to find the ledge surface.
            const RE::NiPoint3 downStart = fwdStart + a_fwd * probeLen;
            const float downMax = maxLedge - minLedge;
            const auto downRay = Raycast::Cast(a_actor, downStart, down, downMax);
            if (!downRay.didHit || LayerExcludedForClimb(downRay.layer) || FormExcluded(downRay.hitRef)) {
                continue;
            }

            const RE::NiPoint3 lp = downRay.hitPos;
            if (lp.z - pos.z < lastZ) {
                continue;
            }
            if (downRay.distance < 10.0f) {
                continue;
            }
            if (downRay.normalZ < minLedgeFlatness) {
                continue;
            }
            if (lp.z < pos.z + minLedge || lp.z > pos.z + maxLedge) {
                continue;
            }
            lastZ = lp.z - pos.z;

            // Space behind the ledge point to stand on (SkyParkour obstruction check).
            constexpr float obsBackOffset = 15.0f;
            constexpr float minSpaceRequired = obsBackOffset + 3.0f;
            const RE::NiPoint3 obsStart = RE::NiPoint3(lp.x, lp.y, lp.z + 5.0f) - a_fwd * obsBackOffset;
            const auto obsRay = Raycast::Cast(a_actor, obsStart, a_fwd, obsBackOffset + 15.0f);
            if (obsRay.didHit && obsRay.distance < minSpaceRequired) {
                continue;
            }

            ledgePoint = lp;
            foundLedge = true;
            break;
        }

        if (!foundLedge) {
            return NpcParkourType::NoLedge;
        }

        // Headroom for the climb path: two up-rays at the actor, offset left/right, at ledge height.
        const RE::NiPoint3 side = a_fwd.Cross(up);
        const RE::NiPoint3 hrBase(pos.x, pos.y, ledgePoint.z - 5.0f);
        if (Raycast::Cast(a_actor, hrBase - side * 15.0f, up, height).didHit ||
            Raycast::Cast(a_actor, hrBase + side * 15.0f, up, height).didHit) {
            return NpcParkourType::NoLedge;
        }

        a_outLedge = ledgePoint;

        const float diff = ledgePoint.z - pos.z;
        if (diff >= ParkourHeights::highestLim * a_scale) {
            return NpcParkourType::Highest;
        }
        if (diff >= ParkourHeights::highLim * a_scale) {
            return NpcParkourType::High;
        }
        if (diff >= ParkourHeights::medLim * a_scale) {
            return NpcParkourType::Medium;
        }
        if (diff >= ParkourHeights::lowLim * a_scale) {
            return NpcParkourType::Low;
        }
        if (diff >= ParkourHeights::highStepLim * a_scale) {
            return NpcParkourType::StepHigh;
        }
        return NpcParkourType::StepLow;
    }

    // Port of SkyParkour's CalculateStartingPosition: back off from the ledge along facing,
    // drop by the animation's ending elevation so root motion lands exactly on the ledge.
    bool ComputeStartPos(const RE::NiPoint3& a_ledge, const RE::NiPoint3& a_fwd, float a_scale,
                         NpcParkourType a_type, RE::NiPoint3& a_out)
    {
        float elev = 0.0f;
        float backOffset = 55.0f;

        switch (a_type) {
        case NpcParkourType::Highest:
            elev = ParkourHeights::highestElev - 5.0f;
            backOffset = 62.0f;
            break;
        case NpcParkourType::High:
            elev = ParkourHeights::highElev - 5.0f;
            break;
        case NpcParkourType::Medium:
            elev = ParkourHeights::medElev - 5.0f;
            break;
        case NpcParkourType::Low:
            elev = ParkourHeights::lowElev - 5.0f;
            break;
        case NpcParkourType::StepHigh:
            elev = ParkourHeights::stepHighElev - 5.0f;
            backOffset = 15.0f;
            break;
        case NpcParkourType::StepLow:
            elev = ParkourHeights::stepLowElev - 5.0f;
            backOffset = 15.0f;
            break;
        case NpcParkourType::Vault:
            elev = ParkourHeights::vaultElev - 5.0f;
            break;
        default:
            return false;
        }

        const RE::NiPoint3 back = a_fwd * (backOffset * a_scale);
        a_out = RE::NiPoint3(a_ledge.x - back.x, a_ledge.y - back.y, a_ledge.z - elev * a_scale);
        return true;
    }
}

namespace NpcParkour
{
    bool HasBehaviorPatch(RE::Actor* a_actor)
    {
        if (!a_actor) {
            return false;
        }
        bool installed = false;
        a_actor->GetGraphVariableBool(SkyParkourGraph::VarTPPInstalled(), installed);
        return installed;
    }

    Detection Detect(RE::Actor* a_actor, const RE::NiPoint3& a_fwd, float a_maxClimbHeight)
    {
        Detection det;
        if (!a_actor) {
            return det;
        }

        float scale = a_actor->GetScale();
        if (scale <= 0.0f) {
            scale = 1.0f;
        }

        RE::NiPoint3 ledge;
        NpcParkourType type = VaultCheck(a_actor, a_fwd, scale, ledge);
        if (type == NpcParkourType::NoLedge) {
            type = ClimbCheck(a_actor, a_fwd, scale, a_maxClimbHeight, ledge);
        }
        if (type == NpcParkourType::NoLedge) {
            return det;
        }

        if (LedgeUnderWater(a_actor, ledge)) {
            return det;
        }

        RE::NiPoint3 startPos;
        if (!ComputeStartPos(ledge, a_fwd, scale, type, startPos)) {
            return det;
        }

        det.type = type;
        det.ledgePoint = ledge;
        det.startPos = startPos;
        return det;
    }

    bool Trigger(RE::Actor* a_actor, const Detection& a_det)
    {
        if (!a_actor || a_det.type == NpcParkourType::NoLedge) {
            return false;
        }

        a_actor->SetGraphVariableInt(SkyParkourGraph::VarLedge(), static_cast<std::int32_t>(a_det.type));

        const bool isStep = a_det.type == NpcParkourType::StepLow ||
                            a_det.type == NpcParkourType::StepHigh;
        const bool lowerBody = isStep && a_actor->AsActorState()->IsWeaponDrawn();
        // Always write this variable. Otherwise a weapon-drawn step can leak its
        // lower-body-only mode into the actor's next full-body climb.
        a_actor->SetGraphVariableBool(SkyParkourGraph::VarLowerBody(), lowerBody);

        const bool accepted = a_actor->NotifyAnimationGraph(SkyParkourGraph::NotifyStart());
        if (!accepted) {
            // Graph rejected the transition — leave no stale state behind.
            a_actor->SetGraphVariableInt(SkyParkourGraph::VarLedge(),
                                         static_cast<std::int32_t>(NpcParkourType::NoLedge));
            a_actor->SetGraphVariableBool(SkyParkourGraph::VarLowerBody(), false);
        }
        return accepted;
    }

    void OnParkourStart(RE::Actor* a_actor)
    {
        if (!a_actor) {
            return;
        }
        if (auto* ctrl = a_actor->GetCharController()) {
            // Disable simulation so vertical root motion drives the actor
            // (SkyParkour OnStartStop, start path).
            ctrl->flags.set(RE::CHARACTER_FLAGS::kNoSim);
        }
    }

    void OnParkourEnd(RE::Actor* a_actor, bool a_sendInterrupt)
    {
        if (!a_actor) {
            return;
        }

        if (auto* ctrl = a_actor->GetCharController()) {
            ctrl->flags.reset(RE::CHARACTER_FLAGS::kNoSim);

            // Prevent flinging if the controller thinks it's airborne (SkyParkour stop path).
            if (ctrl->context.currentState != RE::hkpCharacterStateType::kOnGround) {
                RE::hkVector4 zero(0.0f, 0.0f, 0.0f, 0.0f);
                ctrl->SetLinearVelocityImpl(zero);
            }

            // Root-motion traversal with simulation off can leave the controller
            // tilted. The engine rights the *player* from camera input every frame
            // but never rights NPCs — without this they keep walking diagonally.
            ctrl->pitchAngle = 0.0f;
            ctrl->rollAngle = 0.0f;
        }

        // Same for the reference orientation: only yaw is legitimate here.
        a_actor->data.angle.x = 0.0f;
        a_actor->data.angle.y = 0.0f;

        a_actor->SetGraphVariableInt(SkyParkourGraph::VarLedge(),
                                     static_cast<std::int32_t>(NpcParkourType::NoLedge));
        a_actor->SetGraphVariableBool(SkyParkourGraph::VarLowerBody(), false);

        if (a_sendInterrupt) {
            // Never send SkyParkour_Stop from outside the graph — SkyParkour warns it recurses.
            a_actor->NotifyAnimationGraph(SkyParkourGraph::EvInterrupt());
        }
    }
}
