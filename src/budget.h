#pragma once

#include <chrono>
#include <cstdint>

// Per-frame time budget for this plugin's work, plus the counters behind the
// once-a-minute log line. Everything runs on the game's main thread.
namespace npng
{
    class FrameBudget
    {
    public:
        static FrameBudget& Get() noexcept
        {
            static FrameBudget instance;
            return instance;
        }

        void SetLimitMs(float a_ms) noexcept { limitMs = a_ms; }
        [[nodiscard]] float LimitMs() const noexcept { return limitMs; }

        void Begin() noexcept
        {
            frameStart = Clock::now();
            raysThisFrame = 0;
        }

        // True once this frame's share has been used. 0 disables the cap.
        [[nodiscard]] bool Exhausted() const noexcept
        {
            return limitMs > 0.0f && ElapsedMs() >= limitMs;
        }

        [[nodiscard]] float ElapsedMs() const noexcept
        {
            return std::chrono::duration<float, std::milli>(Clock::now() - frameStart).count();
        }

        void CountRay() noexcept
        {
            ++raysThisFrame;
            ++stats.rays;
        }
        [[nodiscard]] std::uint32_t RaysThisFrame() const noexcept { return raysThisFrame; }

        struct Stats
        {
            std::uint64_t frames{ 0 };
            double        totalMs{ 0.0 };
            float         maxMs{ 0.0f };
            std::uint64_t rays{ 0 };
            std::uint32_t heavyRuns{ 0 };
            std::uint32_t heavyDeferred{ 0 };
            std::uint32_t leapsLaunched{ 0 };
            std::uint32_t yielded{ 0 };  // stuck triggers left to Fluid Motion / NpcTraversalNG
        };

        void End() noexcept
        {
            const float ms = ElapsedMs();
            ++stats.frames;
            stats.totalMs += ms;
            if (ms > stats.maxMs) {
                stats.maxMs = ms;
            }
        }

        // Returns true (and rolls the window) when a minute of frames has been recorded.
        [[nodiscard]] bool TakeReport(Stats& a_out, double a_now) noexcept
        {
            if (a_now - lastReport < 60.0) {
                return false;
            }
            lastReport = a_now;
            a_out = stats;
            stats = {};
            return a_out.frames > 0;
        }

        Stats stats;

    private:
        using Clock = std::chrono::steady_clock;  // QueryPerformanceCounter on MSVC
        Clock::time_point frameStart{ Clock::now() };
        float             limitMs{ 0.6f };
        std::uint32_t     raysThisFrame{ 0 };
        double            lastReport{ 0.0 };
    };
}
