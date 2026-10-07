# StepUpOnto SKSE - NPC Pathing Patch V2

Companion compatibility build of **StepUpOnto SKSE** for use with
[Modern NPC Pathing / NPC Pathing NG](https://github.com/SenjuWoo/Modern-NPC-Pathing).

Upstream: [StepUpOnto SKSE (Nexus 175689)](https://www.nexusmods.com/skyrimspecialedition/mods/175689) by **TheShinyHaxorus**.

## What V2 changes

While a SkyParkour animation is driving an actor (player parkour, or NPC parkour via NPC Pathing NG),
the character controller can read as "grounded and not moving" - which is exactly StepUpOnto's step trigger.
The original could fire its step-warp mid climb/vault and fight parkour alignment.

This build checks the `SkyParkourOngoing` animation graph variable in both the player and NPC step gates
and stays hands-off until the parkour move finishes.

That is the only behavior change. Internally, SimpleIni was replaced with a built-in INI reader/writer
(same file, same keys, same format).

## Installation

1. Install original StepUpOnto SKSE (or have it already).
2. Install this patch **after** it so `StepUpOntoSKSE.dll` overwrites.
3. Keep your existing `StepUpOntoSKSE.ini` - settings continue to work.

Without SkyParkour, behavior matches the original.

Requirements: Skyrim SE/AE, SKSE64, Address Library.

## Layout in this repo

```
patches/StepUpOntoSKSE-V2/
  src/                 source
  CMakeLists.txt
  StepUpOntoSKSE.ini   sample / default keys
  package/README.md
  README.md            this file
```

Binary release zip is attached to the GitHub release **StepUpOntoSKSE-NPCPathing-V2**
(not committed as a tracked binary under the main package ignore rules).

SHA256 of the shipping zip:
`98f205cdf4edd8ccfc918ba866a0eaf1fd20dc7ad6e917f9b93047282e1d5dec  StepUpOntoSKSE1.5.1 - NPCPathing Patch.zip`

## Building

- Windows x64, Visual Studio 2022 C++ tools, CMake 3.21+
- Built CommonLibSSE-NG (same pin as NPC Pathing NG when possible)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCOMMONLIB_SSE_ROOT=<path to CommonLibSSE>
cmake --build build --config Release
```

## Credits / permissions

- **TheShinyHaxorus** - original StepUpOnto SKSE
- SkyParkour V3 by Waffuru (graph variable checked)
- Compatibility patch by GennyWoo (NPC Pathing NG)

Released under the original mod page permissions (modification with credit).