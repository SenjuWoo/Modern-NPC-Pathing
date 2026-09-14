<p align="center">
  <img src="docs/images/logo.svg" width="72" height="72" alt="NPC Pathing NG mark">
</p>

<h1 align="center">NPC Pathing NG</h1>

<p align="center"><strong>Unstick humanoid NPCs. Parkour is optional.</strong></p>

<p align="center">
  SKSE plugin for Skyrim SE/AE. Motion-gated navmesh failsafe, optional SkyParkour<br>
  vault/climb, EVG marker routes, and a validated last-resort teleport.
</p>

<p align="center">
  <a href="https://github.com/ShugokiFable/Modern-NPC-Pathing/actions/workflows/build.yml"><img src="https://github.com/ShugokiFable/Modern-NPC-Pathing/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-7ee0ff?labelColor=0d0f11" alt="GPL-3.0"></a>
  <a href="https://github.com/ShugokiFable/Modern-NPC-Pathing/releases/tag/v2.5.0"><img src="https://img.shields.io/badge/release-v2.5.0-7ee0ff?labelColor=0d0f11" alt="v2.5.0"></a>
  <img src="https://img.shields.io/badge/SKSE-SE%20%2F%20AE-8f9aa6?labelColor=0d0f11" alt="SKSE SE/AE">
</p>

<p align="center">
  <a href="#install">Install</a>
  ·
  <a href="#build">Build</a>
  ·
  <a href="#honest-status">Honest status</a>
  ·
  <a href="CHANGELOG.md">Changelog</a>
  ·
  <a href="https://www.nexusmods.com/skyrimspecialedition/mods/185413">Nexus</a>
</p>

## Why it exists

Skyrim NPCs freeze against geometry, doorframes, and short ledges that the navmesh does not cover. This plugin watches for **walk intent with no progress**, then tries animated escapes before a short, checked teleport.

It is not a pathfinding rewrite and not a combat-AI overhaul.

## What you get

- Stuck detection that ignores idle, sandboxing, sitting, swimming, and mounted NPCs
- Optional **SkyParkour** vault/climb when that mod is installed and enabled
- **Follower replay** of your recent SkyParkour moves (teammate flag: vanilla followers, NFF, similar frameworks)
- Doorway handling that does not shove NPCs sideways off the only route
- **EVG Animated Traversal markers as routes** (optional; on by default since 2.5.0) — never furniture activation
- Validated last-resort teleport after repeated stuck cycles against static geometry — not the player body, other actors, or dialogue holds
- MCM via MCM Helper, with `Data/SKSE/Plugins/NPCPathingNG.ini` as fallback
- FOMOD that auto-detects SkyParkour / EVG and picks a matching INI profile

## What this mod does not claim

- **Not a full AI overhaul.** It unstucks and optionally parkours.
- **Default climb height is 130 units** (steps, vaults, low/chest ledges). Raise toward 250 in MCM/INI for higher climbs.
- **Indoor parkour is off by default.** Teleport fallback can still clear stuck NPCs indoors.
- **SkyParkour climb SFX are 2D** (`SOMStereo`). Parkour only fires within **1600** units of the player (`fParkourMaxPlayerDistance`; `0` = unlimited).
- **EVG is optional.** Plugin masters are only `Skyrim.esm`. EVG and SkyParkour are runtime lookups. NPCs never enter EVG furniture (`ActivateRef` / Papyrus `Activate` are engine-rejected for NPCs). With EVG absent, the route layer is inert.
- **2.4.8 was the first genuine native rebuild since 2.4.4.** 2.4.5, 2.4.6, and 2.4.7 shipped the identical 2.4.4 DLL (SHA256 `9e5616e0...`). Do not use 2.4.5.

## How stuck NPCs are handled

```text
walk intent + no progress
    → SkyParkour vault/climb (if installed, in range, geometry matches)
    → EVG marker route (if EVG present): parkour along heading, else bounds hop
    → doorway / sidestep
    → validated teleport (last resort, after iTeleportEscalation stuck cycles)
```

EVG landings are ground-snapped, headroom- and capsule-cleared, never into water, never through actors, and refused in combat near the player. The hop waits until animated routes have had a chance (second stuck trigger onward).

## Requirements

**Hard**

- Skyrim SE (1.5.97) or AE (1.6.x)
- SKSE64
- Address Library for SKSE Plugins

**Optional**

- [SkyParkour V3](https://www.nexusmods.com/skyrimspecialedition/mods/132292) plus its Nemesis/Pandora (or equivalent) behavior patch — vault/climb and follower replay
- SkyUI + MCM Helper — in-game menu (INI works without them)
- EVG Animated Traversal — marker routes when present; nothing changes when absent

`NPCPathingNG.esp` is ESL-flagged. No full load-order slot.

## Install

Install the FOMOD from [Releases](https://github.com/ShugokiFable/Modern-NPC-Pathing/releases) or [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/185413) with Mod Organizer 2 or Vortex.

1. Let the installer detect SkyParkour / EVG.
2. Keep the matching profile unless you want navmesh-failsafe only: **SkyParkour + EVG**, **SkyParkour only**, or **Navmesh failsafe**.
3. Launch through SKSE.

If you already loaded **2.4.5**: install 2.4.8 or later and remove leftover `Data/Scripts/NPNG_MCMBridge.pex` if present. An empty orphan menu may remain on that save until cleaned or a new game is started.

No save cleaning is required for 2.4.8 → 2.5.0. ESP globals, record layout, and FormIDs are unchanged since 2.4.6. Existing saves keep their saved MCM values; the EVG-on default only seeds fresh installs / new games.

### Configuration

| | |
| --- | --- |
| MCM | **NPC Pathing NG** (pages: General, Parkour, Followers & Combat) |
| INI | `Data/SKSE/Plugins/NPCPathingNG.ini` (re-read when you close the journal) |
| Log | `Documents/My Games/Skyrim Special Edition/SKSE/NPCPathingNG.log` |

Defaults: followers included, combat included, indoor parkour off, climb height **130**, parkour max distance **1600**, EVG marker routes **on**, teleport escalation **5**.

The INI seeds the MCM at `kDataLoaded` (before a save loads). Loading an existing save restores that save's MCM values. MCM changes still apply instantly.

## Build

Windows x64. Visual Studio 2022 or newer with Desktop C++ tools, CMake 3.21+, Python 3.10+, vcpkg (`VCPKG_ROOT` or `VCPKG_INSTALLATION_ROOT`), and CommonLibSSE-NG commit `b93280e832f263dbef44e44cbe2936622a02f91a`. Full recipe: [`BUILDING.md`](BUILDING.md).

```powershell
./build_release.ps1
```

The script pins CommonLibSSE-NG, builds it and the plugin with the static MSVC runtime, runs validation, and writes the Nexus archive to `dist/`. The compiled DLL lands at `package/Data/SKSE/Plugins/NPCPathingNG.dll`. This tree does not ship a DLL.

After CommonLibSSE-NG is installed:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
python generate_esp.py
python -m unittest discover -s tests -v
```

Do not distribute `build/`, `extern/`, vcpkg packages, object files, PDBs, nested `.git` directories, or old ZIP files.

## Project map

```text
src/                 SKSE plugin (pathing, parkour, EVG routes, settings)
tests/               packaging / ESP / version unittest (not in-game)
package/             staged Data tree (ESP, INI, SEQ)
fomod_src/           FOMOD installer sources
patches/             StepUpOnto SKSE compatibility patch sources
tools/               release / FOMOD packagers
generate_esp.py      ESL-flagged ESP generator
build_release.ps1    one-command CommonLibSSE-NG + plugin + archive
```

## Honest status

Verified in this tree:

- Version **2.5.0** (`VERSION.txt`, `src/version.h`, CMake `project(...)`)
- Python release tests (`tests/test_release.py`)
- GitHub Actions build that compiles the DLL against pinned CommonLibSSE-NG
- EVG furniture-activation path removed; markers used as routes only

Not claimed:

- A rewrite of Bethesda pathfinding or combat AI
- Mountain climbing at the default 130-unit cap
- In-game screenshots or a complete load-order compatibility matrix
- That 2.4.5–2.4.7 contained native behaviour changes (they did not)

## Related: StepUpOnto SKSE

If you use [StepUpOnto SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/175689) with SkyParkour / this mod, install the companion build so StepUp does not fire mid-parkour:

- Dedicated repo: [StepUpOntoSKSE-patched](https://github.com/ShugokiFable/StepUpOntoSKSE-patched)
- Patch sources in this tree: [`patches/StepUpOntoSKSE-V2`](patches/StepUpOntoSKSE-V2)
- Binary tag: [StepUpOntoSKSE-NPCPathing-V2](https://github.com/ShugokiFable/Modern-NPC-Pathing/releases/tag/StepUpOntoSKSE-NPCPathing-V2)

## Credits

SkyParkour V3 by Waffuru (graph events / `SkyParkourOngoing`). EVG Animated Traversal markers are used as routes when that mod is present. MCM Helper for the in-game menu. CommonLibSSE-NG (CharmedBaryon / REL lineage). Address Library (meh321). SKSE team.

## License

[GPL-3.0](LICENSE)
