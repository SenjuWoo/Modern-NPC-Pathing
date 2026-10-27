#include "evg_traversal.h"
#include "settings.h"

#include "RE/G/GridCellArray.h"
#include "RE/T/TES.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectCELL.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr const char* EVG_PLUGIN = "EVGAnimatedTraversal.esl";

    // Usable traversal furniture, local FormIDs verified against the 2.1 ESL.
    // EVGATFailedLedgeMarker (0xF43) is deliberately absent — it's a fail anim.
    // RouteKind is how the marker is used as a ROUTE (2.5.0+): the furniture
    // itself can never be entered by an NPC (see header note).
    struct FurnitureEntry
    {
        RE::FormID              localID;
        EvgTraversal::RouteKind kind;
    };
    constexpr std::array<FurnitureEntry, 15> kFurniture = { {
        { 0x80A, EvgTraversal::RouteKind::Up },      // EVGATLadderShort
        { 0x812, EvgTraversal::RouteKind::Across },  // EVGATSqueezeMarker
        { 0x827, EvgTraversal::RouteKind::Across },  // EVGATDuckMarker
        { 0x83E, EvgTraversal::RouteKind::Down },    // EVGATRaiderDropMarker
        { 0x891, EvgTraversal::RouteKind::Up },      // EVGATLedgeMarker
        { 0x8F4, EvgTraversal::RouteKind::Across },  // EVGATVaultMarker
        { 0x920, EvgTraversal::RouteKind::Up },      // EVGATLadderShortMO
        { 0x956, EvgTraversal::RouteKind::Down },    // EVGATTLSlideMO
        { 0x982, EvgTraversal::RouteKind::Down },    // EVGATRaiderRollMarker
        { 0x98A, EvgTraversal::RouteKind::Down },    // EVGATWallDropFMarker
        { 0x992, EvgTraversal::RouteKind::Down },    // EVGATWallDropSMarker
        { 0x99A, EvgTraversal::RouteKind::Down },    // EVGATDeepWalkMarker
        { 0x9BF, EvgTraversal::RouteKind::Up },      // EVGATMediumLedgeMarker
        { 0x9C4, EvgTraversal::RouteKind::Up },      // EVGATLedgeCatchMarker
        { 0xF1D, EvgTraversal::RouteKind::Up },      // EVGATTallLadderUpMarker
    } };

    std::unordered_set<RE::FormID> g_resolvedIDs;  // runtime FormIDs of the bases
    std::unordered_map<RE::FormID, EvgTraversal::RouteKind> g_kinds;
    bool g_available = false;

    // Marker scanning is latched off after repeated scans find nothing, so a
    // markerless area (most cities) does not re-scan the cell grid forever.
    // Unlike the pre-2.5.0 activation-failure latch, this one re-arms when the
    // player's cell changes — an EVG dungeon reached later in the same session
    // gets its markers used.
    constexpr int kNpcFailureLimit = 3;
    int  g_fruitlessScans = 0;
    bool g_scanEnabled = true;
    const RE::TESObjectCELL* g_currentCell = nullptr;

    // Marker references per cell, collected once. The old scan walked every reference of every
    // nearby cell (thousands in a city) for each stuck NPC; now a markerless cell is an empty
    // vector and a lookup is a handful of comparisons. Dropped on cell change and game load.
    std::unordered_map<RE::FormID, std::vector<RE::ObjectRefHandle>> g_cellMarkers;

    const std::vector<RE::ObjectRefHandle>& MarkersIn(const RE::TESObjectCELL* a_cell)
    {
        auto [it, inserted] = g_cellMarkers.try_emplace(a_cell->GetFormID());
        if (inserted) {
            a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
                if (a_ref && !a_ref->IsDeleted() && EvgTraversal::IsTraversalFurniture(a_ref->GetBaseObject())) {
                    it->second.push_back(a_ref->GetHandle());
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        return it->second;
    }
}

namespace EvgTraversal
{
    void CacheForms()
    {
        g_resolvedIDs.clear();
        g_kinds.clear();
        g_cellMarkers.clear();
        g_available = false;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return;
        }

        for (const auto& entry : kFurniture) {
            if (auto* form = dataHandler->LookupForm(entry.localID, EVG_PLUGIN)) {
                g_resolvedIDs.insert(form->GetFormID());
                g_kinds.emplace(form->GetFormID(), entry.kind);
            }
        }

        g_available = !g_resolvedIDs.empty();
        if (g_available) {
            logger::info("NPCPathingNG: EVG Animated Traversal found — {} marker types usable as routes",
                         g_resolvedIDs.size());
        } else {
            logger::info("NPCPathingNG: EVG Animated Traversal not present — marker routes disabled");
        }
    }

    bool IsAvailable()
    {
        return g_available;
    }

    bool IsRouteEnabled()
    {
        return g_available && g_scanEnabled;
    }

    void UpdatePlayerCell(const RE::TESObjectCELL* a_cell)
    {
        // Re-arm the scan latch when the player travels to a new cell. A
        // markerless area latched scanning off; the next area must get a fresh
        // chance without a reload.
        if (a_cell && g_currentCell && g_currentCell != a_cell) {
            g_cellMarkers.clear();
            if (!g_scanEnabled) {
                g_scanEnabled = true;
                g_fruitlessScans = 0;
                logger::info("NPCPathingNG: player cell changed — EVG marker scanning re-armed");
            }
        }
        g_currentCell = a_cell;
    }

    void ResetRouteState()
    {
        g_fruitlessScans = 0;
        g_scanEnabled = true;
        g_cellMarkers.clear();
    }

    void NoteFruitlessScan()
    {
        if (g_scanEnabled && ++g_fruitlessScans >= kNpcFailureLimit) {
            g_scanEnabled = false;
            logger::info(
                "NPCPathingNG: EVG marker scanning paused for this cell — {} scans found no "
                "marker nearby. Re-armed on cell change or the next game load.",
                g_fruitlessScans);
        }
    }

    void NoteRouteSuccess()
    {
        g_fruitlessScans = 0;
        g_scanEnabled = true;
    }

    bool IsTraversalFurniture(const RE::TESBoundObject* a_base)
    {
        return a_base && g_resolvedIDs.contains(a_base->GetFormID());
    }

    RouteKind KindFor(const RE::TESBoundObject* a_base)
    {
        if (!a_base) {
            return RouteKind::Across;
        }
        const auto it = g_kinds.find(a_base->GetFormID());
        return it != g_kinds.end() ? it->second : RouteKind::Across;
    }

    RE::TESObjectREFR* FindMarkerNear(RE::Actor* a_actor, const RE::NiPoint3& a_fwd, float a_radius)
    {
        if (!g_available || !g_scanEnabled || !a_actor) {
            return nullptr;
        }
        auto* tes = RE::TES::GetSingleton();
        if (!tes) {
            return nullptr;
        }

        const RE::NiPoint3 pos = a_actor->GetPosition();
        RE::TESObjectREFR* best = nullptr;
        float bestScore = -1.0f;

        auto visit =
            [&](RE::TESObjectREFR* a_ref) {
                if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted() || !a_ref->Is3DLoaded() ||
                    !IsTraversalFurniture(a_ref->GetBaseObject())) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                const RE::NiPoint3 mPos = a_ref->GetPosition();
                const float flatX = mPos.x - pos.x;
                const float flatY = mPos.y - pos.y;
                if (flatX * flatX + flatY * flatY > a_radius * a_radius) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (std::abs(mPos.z - pos.z) > 200.0f) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                // Marker heading = traversal direction.
                const float mYaw = a_ref->GetAngleZ();
                const RE::NiPoint3 mFwd(std::sin(mYaw), std::cos(mYaw), 0.0f);

                // Entry-side gate: the actor must not already be past the marker,
                // or the anim would carry them backwards through the obstacle.
                RE::NiPoint3 toActor = pos - mPos;
                toActor.z = 0.0f;
                if (toActor.Dot(mFwd) > 30.0f) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                // The actor's travel direction must roughly match the traversal.
                const float align = a_fwd.Dot(mFwd);
                if (align < 0.0f) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                // Prefer aligned, close markers.
                const float dist = std::max(1.0f, toActor.Length());
                const float score = align + (a_radius - dist) / a_radius;
                if (score > bestScore) {
                    bestScore = score;
                    best = a_ref;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            };

        // Deliberately NOT TES::ForEachReferenceInRange: that helper ends every scan with
        // worldSpace->GetSkyCell(), and TES::worldSpace can dangle during cell transitions —
        // confirmed crash (AE id 20543, this+0xF8). We walk the cells ourselves, from the cache.
        auto scanCell = [&](const RE::TESObjectCELL* a_cell) {
            for (const auto& handle : MarkersIn(a_cell)) {
                if (auto ref = handle.get()) {
                    visit(ref.get());
                }
            }
        };

        if (tes->interiorCell) {
            scanCell(tes->interiorCell);
        } else if (auto* grid = tes->gridCells; grid && grid->length > 0) {
            // A 250-unit radius cannot reach past the actor's own cell and its immediate
            // neighbours; reject cells whose bounds are outside the radius first.
            constexpr float kCellSize = 4096.0f;
            const float r2 = a_radius * a_radius;
            const auto length = grid->length;
            for (std::uint32_t x = 0; x < length; x++) {
                for (std::uint32_t y = 0; y < length; y++) {
                    auto* cell = grid->GetCell(x, y);
                    if (!cell || !cell->IsAttached()) {
                        continue;
                    }
                    if (auto* coords = cell->GetCoordinates()) {
                        const float minX = static_cast<float>(coords->cellX) * kCellSize;
                        const float minY = static_cast<float>(coords->cellY) * kCellSize;
                        const float dx = std::max({ minX - pos.x, 0.0f, pos.x - (minX + kCellSize) });
                        const float dy = std::max({ minY - pos.y, 0.0f, pos.y - (minY + kCellSize) });
                        if (dx * dx + dy * dy > r2) {
                            continue;
                        }
                    }
                    scanCell(cell);
                }
            }
        }

        return best;
    }
}
