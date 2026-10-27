#include "raycast.h"

#include "budget.h"

#include "RE/B/bhkPickData.h"
#include "RE/B/bhkWorld.h"
#include "RE/H/hkpCollidable.h"
#include "RE/T/TESHavokUtilities.h"
#include "RE/T/TESObjectCELL.h"

namespace Raycast
{
    Result Cast(RE::Actor* a_actor, const RE::NiPoint3& a_start, const RE::NiPoint3& a_dir, float a_maxDist)
    {
        Result result;
        result.distance = a_maxDist;
        result.hitPos = a_start + a_dir * a_maxDist;

        if (!a_actor) {
            return result;
        }

        auto* cell = a_actor->GetParentCell();
        if (!cell) {
            return result;
        }

        auto* world = cell->GetbhkWorld();
        if (!world) {
            return result;
        }

        const float scale = RE::bhkWorld::GetWorldScale();
        const RE::NiPoint3 end = a_start + a_dir * a_maxDist;
        npng::FrameBudget::Get().CountRay();

        RE::bhkPickData pickData;
        pickData.rayInput.from = RE::hkVector4(a_start.x * scale, a_start.y * scale, a_start.z * scale, 0.0f);
        pickData.rayInput.to = RE::hkVector4(end.x * scale, end.y * scale, end.z * scale, 0.0f);

        // Actor's filter group with the layer forced to kTransparent — same trick
        // SkyParkour uses for all its parkour rays (skips self, hits world geometry).
        RE::CFilter filter{};
        a_actor->GetCollisionFilterInfo(filter);
        filter.SetCollisionLayer(RE::COL_LAYER::kTransparent);
        pickData.rayInput.filterInfo = filter;

        if (world->PickObject(pickData) && pickData.rayOutput.HasHit()) {
            result.didHit = true;
            result.distance = a_maxDist * pickData.rayOutput.hitFraction;
            result.hitPos = a_start + a_dir * result.distance;
            result.normalZ = pickData.rayOutput.normal.quad.m128_f32[2];
            if (pickData.rayOutput.rootCollidable) {
                result.layer = pickData.rayOutput.rootCollidable->GetCollisionLayer();
                result.hitRef = RE::TESHavokUtilities::FindCollidableRef(*pickData.rayOutput.rootCollidable);
            }
        }

        return result;
    }

    bool IsActorHit(const Result& a_hit)
    {
        switch (a_hit.layer) {
        case RE::COL_LAYER::kCharController:
        case RE::COL_LAYER::kBiped:
        case RE::COL_LAYER::kBipedNoCC:
        case RE::COL_LAYER::kDeadBip:
            return true;
        default:
            break;
        }
        return a_hit.hitRef && a_hit.hitRef->GetFormType() == RE::FormType::ActorCharacter;
    }

    bool LandingIsClear(RE::Actor* a_actor, float a_groundZ, float a_x, float a_y)
    {
        constexpr float kBodyRadius = 28.0f;
        const RE::NiPoint3 up(0.0f, 0.0f, 1.0f);

        // Headroom above the landing spot.
        if (Cast(a_actor, RE::NiPoint3(a_x, a_y, a_groundZ + 10.0f), up, 110.0f).didHit) {
            return false;
        }

        // Capsule-width clearance around the landing spot. A clear travel ray alone is not
        // enough if the endpoint is inside a narrow corner, prop, or another collision hull.
        constexpr float kDiagonal = 0.70710678f;
        constexpr RE::NiPoint3 clearanceDirs[] = {
            { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
            { kDiagonal, kDiagonal, 0.0f }, { -kDiagonal, kDiagonal, 0.0f },
            { kDiagonal, -kDiagonal, 0.0f }, { -kDiagonal, -kDiagonal, 0.0f }
        };
        for (float height : { 25.0f, 70.0f }) {
            const RE::NiPoint3 center(a_x, a_y, a_groundZ + height);
            for (const auto& dir : clearanceDirs) {
                if (Cast(a_actor, center, dir, kBodyRadius).didHit) {
                    return false;
                }
            }
        }
        return true;
    }
}
