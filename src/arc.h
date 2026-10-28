#pragma once

// Game-independent ballistic arc solver for NPC leaps (see leap.cpp). Plain
// floats, no CommonLib, so tests/test_arc.cpp builds and runs without the game.

#include <algorithm>
#include <cmath>
#include <optional>

namespace npng::arc
{
    struct Vec3
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    struct Params
    {
        float gravity{ 900.0f };            // u/s^2; vanilla is ~686, higher reads snappier
        float minDuration{ 0.40f };         // s; shorter flights look like a teleport
        float maxDuration{ 1.60f };         // s; longer flights are rejected
        float maxHorizontalSpeed{ 600.0f }; // u/s
        float clearance{ 14.0f };           // u above the highest of start, end and obstacle top
        float maxApexAbove{ 190.0f };       // u above the higher end point
    };

    struct Arc
    {
        Vec3  start;
        Vec3  end;
        float apexZ{ 0.0f };
        float tUp{ 0.0f };
        float tDown{ 0.0f };
        float duration{ 0.0f };
        Vec3  horizontalVelocity;  // u/s, z unused

        [[nodiscard]] Vec3 At(float a_t) const noexcept
        {
            const float t = std::clamp(a_t, 0.0f, duration);
            const float f = duration > 0.0f ? t / duration : 1.0f;
            const float vz0 = gravityUsed * tUp;
            return { start.x + horizontalVelocity.x * t,
                     start.y + horizontalVelocity.y * t,
                     f >= 1.0f ? end.z : start.z + vz0 * t - 0.5f * gravityUsed * t * t };
        }

        [[nodiscard]] Vec3 VelocityAt(float a_t) const noexcept
        {
            const float t = std::clamp(a_t, 0.0f, duration);
            return { horizontalVelocity.x, horizontalVelocity.y, gravityUsed * (tUp - t) };
        }

        float gravityUsed{ 900.0f };
    };

    namespace detail
    {
        [[nodiscard]] inline float TimeToFall(float a_height, float a_g) noexcept
        {
            return a_height > 0.0f ? std::sqrt(2.0f * a_height / a_g) : 0.0f;
        }

        [[nodiscard]] inline float TotalTime(float a_apexZ, float a_z0, float a_z1, float a_g) noexcept
        {
            return TimeToFall(a_apexZ - a_z0, a_g) + TimeToFall(a_apexZ - a_z1, a_g);
        }
    }

    // Lowest parabola from a_start to a_end whose apex clears a_obstacleTopZ. When a_obstacleFraction
    // is in [0,1] the arc must also be that high at that fraction of the flight (the obstacle is not
    // always under the apex). Returns nothing when the flight would need to be longer than
    // maxDuration, higher than maxApexAbove, or faster than maxHorizontalSpeed.
    [[nodiscard]] inline std::optional<Arc> Solve(const Vec3& a_start, const Vec3& a_end, float a_obstacleTopZ,
                                                  const Params& a_p = {}, float a_obstacleFraction = -1.0f) noexcept
    {
        if (!(a_p.gravity > 0.0f)) {
            return std::nullopt;
        }
        const float dx = a_end.x - a_start.x;
        const float dy = a_end.y - a_start.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float highEnd = std::max(a_start.z, a_end.z);
        const float apexLimit = highEnd + a_p.maxApexAbove;

        auto build = [&](float a_apex) {
            Arc arc;
            arc.start = a_start;
            arc.end = a_end;
            arc.gravityUsed = a_p.gravity;
            arc.apexZ = a_apex;
            arc.tUp = detail::TimeToFall(a_apex - a_start.z, a_p.gravity);
            arc.tDown = detail::TimeToFall(a_apex - a_end.z, a_p.gravity);
            arc.duration = arc.tUp + arc.tDown;
            if (arc.duration > 0.0f) {
                arc.horizontalVelocity = { dx / arc.duration, dy / arc.duration, 0.0f };
            }
            return arc;
        };

        // The flight must last long enough to look like one and to keep the speed under the cap.
        const float wanted = std::max(a_p.minDuration, a_p.maxHorizontalSpeed > 0.0f ? dist / a_p.maxHorizontalSpeed : 0.0f);
        const float needHeight = a_obstacleTopZ + a_p.clearance;

        // Smallest apex that satisfies duration, clearance at the apex and (optionally) clearance
        // at the obstacle's fraction. Every condition only gets easier with a higher apex.
        auto satisfied = [&](float a_apex) {
            const Arc arc = build(a_apex);
            if (arc.duration < wanted) {
                return false;
            }
            if (a_obstacleFraction >= 0.0f && a_obstacleFraction <= 1.0f &&
                arc.At(arc.duration * a_obstacleFraction).z < needHeight) {
                return false;
            }
            return true;
        };

        float apex = std::max(highEnd, a_obstacleTopZ) + a_p.clearance;
        if (!satisfied(apex)) {
            float lo = apex;
            float hi = apexLimit;
            if (hi <= lo || !satisfied(hi)) {
                return std::nullopt;
            }
            for (int i = 0; i < 40; ++i) {
                const float mid = 0.5f * (lo + hi);
                (satisfied(mid) ? hi : lo) = mid;
            }
            apex = hi;
        }
        if (apex > apexLimit + 0.01f) {
            return std::nullopt;
        }

        Arc arc = build(apex);
        if (arc.duration > a_p.maxDuration || arc.duration <= 0.0f) {
            return std::nullopt;
        }
        if (dist / arc.duration > a_p.maxHorizontalSpeed + 0.01f) {
            return std::nullopt;
        }
        return arc;
    }
}
