// Game-independent tests for src/arc.h. Built as NPNGTests; no CommonLib, no game.
#include "arc.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace npng::arc;

namespace
{
    int g_failures = 0;

    void Check(bool a_ok, const char* a_what, int a_line)
    {
        if (!a_ok) {
            std::printf("FAIL line %d: %s\n", a_line, a_what);
            ++g_failures;
        }
    }
#define CHECK(expr) Check((expr), #expr, __LINE__)

    bool Near(float a, float b, float eps = 0.5f) { return std::fabs(a - b) <= eps; }
}

int main()
{
    // Hurdle: level ground, 90u fence in between. Must start and end exactly, clear the top.
    {
        const auto arc = Solve({ 0, 0, 0 }, { 160, 0, 0 }, 90.0f);
        CHECK(arc.has_value());
        CHECK(Near(arc->At(0).z, 0.0f) && Near(arc->At(0).x, 0.0f));
        CHECK(Near(arc->At(arc->duration).x, 160.0f) && Near(arc->At(arc->duration).z, 0.0f));
        CHECK(arc->apexZ >= 90.0f + 14.0f - 0.01f);
        CHECK(arc->duration >= 0.40f && arc->duration <= 1.60f);
        // The highest sampled point of the flight is the apex.
        float top = -1e9f;
        for (int i = 0; i <= 200; ++i) {
            top = std::max(top, arc->At(arc->duration * static_cast<float>(i) / 200.0f).z);
        }
        CHECK(Near(top, arc->apexZ, 1.0f));
    }

    // Vault up onto a 100u ledge: end is higher than start, apex above the end.
    {
        const auto arc = Solve({ 0, 0, 0 }, { 70, 0, 100 }, 100.0f);
        CHECK(arc.has_value());
        CHECK(arc->apexZ > 100.0f);
        CHECK(Near(arc->At(arc->duration).z, 100.0f));
        CHECK(arc->tUp > 0.0f && arc->tDown > 0.0f);
    }

    // Drop down 200u: apex only a clearance above the start, longer fall.
    {
        const auto arc = Solve({ 0, 0, 0 }, { 120, 0, -200 }, 0.0f);
        CHECK(arc.has_value());
        CHECK(Near(arc->apexZ, 14.0f, 0.1f));
        CHECK(Near(arc->At(arc->duration).z, -200.0f));
    }

    // Velocity at the apex is purely horizontal; it points up before and down after.
    {
        const auto arc = Solve({ 0, 0, 0 }, { 160, 0, 0 }, 90.0f);
        CHECK(arc.has_value());
        CHECK(Near(arc->VelocityAt(arc->tUp).z, 0.0f, 0.01f));
        CHECK(arc->VelocityAt(0.0f).z > 0.0f);
        CHECK(arc->VelocityAt(arc->duration).z < 0.0f);
    }

    // Too far for the speed cap within the max duration -> rejected.
    CHECK(!Solve({ 0, 0, 0 }, { 5000, 0, 0 }, 0.0f).has_value());

    // Too high a wall (apex would exceed maxApexAbove) -> rejected.
    CHECK(!Solve({ 0, 0, 0 }, { 60, 0, 0 }, 400.0f).has_value());

    // Very short hop is stretched to the minimum duration, not teleported.
    {
        const auto arc = Solve({ 0, 0, 0 }, { 20, 0, 0 }, 0.0f);
        CHECK(arc.has_value());
        CHECK(arc->duration >= 0.40f - 0.001f);
    }

    // Horizontal speed never exceeds the cap.
    {
        const auto arc = Solve({ 0, 0, 0 }, { 330, 0, 0 }, 30.0f);
        CHECK(arc.has_value());
        const float speed = std::hypot(arc->horizontalVelocity.x, arc->horizontalVelocity.y);
        CHECK(speed <= 600.0f + 0.1f);
    }

    // Diagonal travel keeps both axes consistent.
    {
        const auto arc = Solve({ 10, 20, 5 }, { 130, 180, 5 }, 60.0f);
        CHECK(arc.has_value());
        const auto e = arc->At(arc->duration);
        CHECK(Near(e.x, 130.0f) && Near(e.y, 180.0f));
    }

    // An obstacle near the start (fraction 0.2) must be cleared at that point of the flight,
    // not only at the apex.
    {
        const auto arc = Solve({ 0, 0, 0 }, { 200, 0, 0 }, 100.0f, Params{}, 0.2f);
        CHECK(arc.has_value());
        CHECK(arc->At(arc->duration * 0.2f).z >= 100.0f + 14.0f - 0.5f);
        CHECK(Near(arc->At(arc->duration).x, 200.0f));
        // Constraint can make a flight impossible: wall too high for the apex limit.
        CHECK(!Solve({ 0, 0, 0 }, { 200, 0, 0 }, 185.0f, Params{}, 0.05f).has_value());
    }

    // Invalid gravity is rejected instead of dividing by zero.
    {
        Params p;
        p.gravity = 0.0f;
        CHECK(!Solve({ 0, 0, 0 }, { 10, 0, 0 }, 0.0f, p).has_value());
    }

    if (g_failures == 0) {
        std::printf("arc tests: all passed\n");
    }
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
