# NPC Pathing NG 3.0.1 — Nexus release notes

## Short summary

Rebuilt on CommonLibSSE-NG v14.1.0 (one DLL for SE 1.5.97 and AE 1.6.x / 1.7.x), faster, with its own hurdle/vault/drop and a compatibility layer for Fluid Motion and NpcTraversalNG. Installer fixed in 3.0.1.

## What changed

- **Compatibility:** detects Fluid Motion, FluidMotionNPC and NpcTraversalNG ("Enemies Can Jump and Vault") when the game starts. An NPC they are moving is never touched; the own leap switches off while they own jumps; this mod waits twice as long and needs two more stuck cycles before teleporting. MCM > Compatibility: Auto / Always run own traversal / Passive.
- **Own hurdle / vault / drop** for stuck humanoids (vanilla jump animations, collision-checked arc) when neither mod is installed. No SkyParkour needed.
- **Performance:** heavy work is queued, two attempts per frame at most, inside a 0.6 ms budget (MCM slider). Far NPCs are skipped, MCM globals are not copied every frame, follower replay is throttled, EVG markers are cached per cell.
- **3.0.1 installer fix:** 3.0.0's no-page installer decided about the StepUpOnto patch by looking for a DLL, which Mod Organizer 2 can never see, so the patch was never offered and some MO2 setups misbehaved on install. Back to a plain one-page installer: press Install. The three old profiles are still gone (the plugin detects SkyParkour / EVG / the jump mods itself); the only choice is an optional tick-box, off by default, for the SkyParkour-aware StepUpOnto SKSE build. No `extras/` folder any more.
- **Static runtime**: no Visual C++ Redistributable needed.
- **New in the log:** one `compat:` line at start listing what was found, and a per-minute frame-cost line when work happens.

## Upgrading

Install over 2.x. FormIDs of existing globals are unchanged; `0x814-0x816` were added. Existing saves keep their saved MCM values.

## Honest limits

- Not played in game yet. Compiled, unit-tested and package-verified only. Roll back to 2.5.0 if it crashes on load and send `NPCPathingNG.log`.
- Own leap is humanoid-only.
- Fluid Motion 1.2 has no documented API, so its ownership is read from its INI.
