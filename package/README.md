# NPC Pathing NG v3.0.1

SKSE plugin for Skyrim SE/AE (one DLL, built on CommonLibSSE-NG v14.1.0). Unsticks NPCs: hurdle, vault, drop, parkour and EVG marker routes, with a compatibility layer for Fluid Motion and NpcTraversalNG.

## Does

- Detects NPCs that are trying to walk but not moving and gets them going again
- **Own hurdle / vault / drop** for humanoids (vanilla jump animations, collision-checked arc) when no other mod owns NPC jumps
- Optional SkyParkour vault/climb and follower replay of your moves
- EVG Animated Traversal markers used as routes (never furniture activation)
- Doorway-aware handling; validated last-resort teleport
- **Detects Fluid Motion, FluidMotionNPC and NpcTraversalNG (Enemies Can Jump and Vault) and lets them go first**; it never moves an NPC they are flying
- Frame-time budget: heavy work is queued and capped per frame
- MCM (MCM Helper) or INI

## Install

One short FOMOD: press Install. It installs the plugin, SEQ, MCM config, INI and DLL. One optional tick-box (off by default) adds the SkyParkour-aware StepUpOnto SKSE build; tick it only if you use StepUpOnto SKSE with SkyParkour.

Requires SKSE64 and Address Library. Optional: SkyParkour V3, SkyUI + MCM Helper, EVG Animated Traversal. No Visual C++ Redistributable needed.

## Config

- MCM: **NPC Pathing NG** (General, Parkour, Compatibility, Followers & Combat)
- INI: `Data/SKSE/Plugins/NPCPathingNG.ini`
- Compatibility mode (MCM or `[Compat] sMode`): Auto (default) / Force / Passive
- Log: `Documents/My Games/Skyrim Special Edition/SKSE/NPCPathingNG.log` (first lines show what was detected)

## Does not

- Replace pathfinding or combat AI
- Climb mountains at the default 130-unit cap
- Move creatures: the own leap is humanoid-only (NpcTraversalNG covers creatures)

## Not tested in game

This build was compiled, unit-tested and package-verified, not played. If it crashes on load, roll back to 2.5.0 and send the log.
