## 3.0.1 (2026-10-30)

Installer hotfix; the plugin DLL code is unchanged from 3.0.0 apart from the version number.

### Fixed

- **FOMOD:** 3.0.0 had no install page and decided whether to install the StepUpOnto patch with a file
  dependency on `StepUpOntoSKSE.dll`. Mod Organizer 2 only evaluates file dependencies on `.esp/.esm/.esl`
  (unless its "use_any_file" setting is on), so the patch was never installed, and users reported MO2 closing
  during install. The installer is now the plain 2.5.0 layout: required files plus one page with one optional
  tick-box (off by default) for the StepUpOnto patch. No file or flag conditions, no `extras/` copy.
- Packager and tests now reject any `fileDependency` on a non-plugin file and any installer without exactly one page.

## 3.0.0 (2026-10-27)

Rebuilt on the current CommonLibSSE-NG, rewritten for frame cost, with its own NPC hurdle/vault/drop
and a compatibility layer for Fluid Motion and NpcTraversalNG. **One FOMOD with nothing to choose.**
Not played in game yet: see "Verification" below.

### Added

- **Own hurdle / vault / drop** for stuck humanoids: collision-checked ballistic arc, vanilla jump
  events, no animation files, no SkyParkour needed. Pure arc maths in `src/arc.h` with unit tests.
  Humanoids only; off automatically when another mod owns NPC jumps.
- **Compatibility layer** (`src/compat.*`), resolved once after every plugin has loaded:
  - NpcTraversalNG / "Enemies Can Jump and Vault": `NPCT_ActiveFlights` is read each frame; an NPC it
    is flying is never touched.
  - Fluid Motion (`FluidMotion.dll`, `FluidMotionAnimation.dll`): ownership read from its INI
    (`[FluidMotion] bEnabled`, `[Jump] bEnabled`, `[NpcClimb] bClimb`), re-read on every load.
  - FluidMotionNPC: `FMNPC_IsTraversing` honoured, `FMNPC_Claim` sent while this mod moves an NPC.
  - While another mod handles stuck NPCs this one waits twice as long, keeps its own leap off, and
    needs two more stuck cycles before any teleport. Any NPC seen in the air is not "stuck" for 1.2 s.
  - Mode in the MCM (new **Compatibility** page) and `[Compat] sMode`: Auto / Force / Passive.
- **Frame-time budget** (`fFrameBudgetMs`, default 0.6, MCM slider) and a per-minute perf line in the log.
- New ESP globals `0x814 NPNG_EnableLeap`, `0x815 NPNG_CompatMode`, `0x816 NPNG_FrameBudgetMs`.
  Existing globals and FormIDs are unchanged.
- INI: `[Performance]`, `[Compat]`, `[Leap]` sections.
- The StepUpOnto SKSE compatibility build is rebuilt against the new CommonLib and ships inside the FOMOD.

### Changed

- **CommonLibSSE-NG v14.1.0** (alandtse fork; SE + AE + VR layouts in one library) replaces the
  2024-era CharmedBaryon pin. One DLL for SE 1.5.97, AE 1.6.x and 1.7.x. Plugin declaration carries the
  Address Library V5 flag and struct independence; the PE stamp is deterministic (`/Brepro`).
- **Frame hook** is resolved per runtime (`REL::Relocate(0xAD, 0xAD, 0xAF)`; 2.x used 0xAD everywhere),
  refuses a slot that does not point at executable code, chains behind other plugins that swapped the
  same slot (NpcTraversalNG and Fluid Motion do), and a thrown exception turns the plugin off for the
  session instead of reaching the game.
- **Performance:** heavy unstick attempts are queued and run at most two a frame inside the time budget;
  NPCs beyond 4500 units are skipped; MCM globals are copied 4x/s instead of every frame; follower replay
  is throttled to one ledge sweep per 0.6 s; EVG markers are collected once per cell instead of walking
  every reference of every nearby cell per stuck NPC; graph variable/event names are interned
  `BSFixedString`s.
- **Installer:** the three-option FOMOD page is gone. One automatic installer; SkyParkour, EVG, Fluid
  Motion and NpcTraversalNG are detected by the DLL at game start, StepUpOnto by the installer. The
  old profiles map to settings: *Navmesh failsafe only* = `bEnableParkour=0`, `bEnableEVGTraversal=0`;
  *SkyParkour only* = `bEnableEVGTraversal=0`.
- Default INI is no longer rewritten by the plugin when missing (the installer always ships it).

### Fixed

- ESP header `numRecords` counted records only (20, now 25 = 23 records + 2 groups). Inherited from 2.x; the game tolerated it, the header audit does not.
- Hard-coded vtable index would have been wrong on VR; now resolved per runtime.
- `PlayerCharacter::Update` hook did not check what it was overwriting.

### Verification

- Built with MSVC 19.51 (VS 2026 Build Tools), `/W4`, zero warnings; static runtime, no redistributable.
- Unit tests (arc solver), Python release tests (ESP/SEQ/MCM/INI/version/FOMOD), packager self-check
  (archive member allowlist, payload hashes, both FOMOD branches simulated).
- Not done: any in-game run, per-runtime Address Library ID audit, a frame-time measurement.

## 2.5.0 (2026-08-16)

EVG Animated Traversal markers are now **routes** for stuck NPCs, on by default.

### Added

- **Marker-guided parkour.** A stuck NPC near an EVG marker first tries a
  SkyParkour move along the marker's heading when the geometry matches. The
  2.4.10 player-distance cap still applies, so distant NPCs never blast 2D
  climb SFX at the listener.
- **Bounds-based landing hop.** When the geometry does not match a parkour
  move, the NPC hops across the marker to a landing derived from the marker's
  kind (Up: ladders/ledges, Down: drops/rolls/slides, Across: squeezes/vaults)
  and furniture bounds. Landings are ground-snapped, headroom- and
  capsule-cleared, never into water, never onto an actor, and the hop is
  refused in combat near the player.
- **Follower replay** routes followers across the same markers after the
  player uses them.
- **Cell-aware scan latch.** Fruitless marker scans now only pause scanning
  for the current player cell and re-arm on cell change or save load, instead
  of disabling marker use for the whole session.

### Changed

- **`bEnableEVGTraversal` now defaults to ON** (code, INI, ESP global and MCM
  help text). With EVG absent the route layer is inert.
- **Removed the furniture-activation path entirely** (`ActivateRef` for NPCs
  was engine-rejected — confirmed repeatedly from SKSE and Papyrus) and the
  failure latch that existed only because of it.
- **FOMOD presets rebuilt:** SkyParkour + EVG (recommended), SkyParkour only
  (no EVG markers), Navmesh failsafe only. The old "experimental EVG" option
  is gone.
- **Factored the teleport's destination checks** into a shared
  `LandingIsClear` helper now used by both the bypass teleport and the EVG
  hop (same validation, one implementation).

### Notes

- No new settings, globals or records: **no save cleaning**. ESP globals and
  FormIDs are unchanged since 2.4.6.
- The EVG toggle default only seeds fresh installs / new games; existing
  saves keep their saved MCM values.

## 2.4.10 (2026-08-16)

### Fixed

- **Phantom SkyParkour climb sounds with nobody in sight.** SkyParkour's
  climb/vault/step HKX files fire `SoundPlay.SPPF_*` descriptors whose output
  model is vanilla `SOMStereo` — 2D, no attenuation. This mod was sending the
  `SkyParkour` graph event to any stuck high-process humanoid, so a hunter two
  cells away or an NPC behind a wall played a full-volume climb at the
  listener. That is not animals climbing (they were already filtered); it is
  player-authored 2D SFX on distant NPCs. Parkour now refuses unless the actor
  is within `fParkourMaxPlayerDistance` of the player (default **1600**, about
  a 3D footstep's hearing range). Set it to **0** in MCM/INI for the old
  unlimited behaviour. Followers still replay your moves when they are near
  you. Teleport fallback is unchanged.

## 2.4.9 (2026-08-13)

Performance release. Everything here is a cost reduction - no behaviour was
intentionally changed, and no setting needs adjusting after updating.

### Fixed - city frame rate

- **Crowd jams ran the full ledge sweep.** The most common "stuck" cause in a busy
  city is one NPC wedged behind another. `Unstick` went straight into the parkour
  ledge detection, ~15 raycast iterations, for every one of them - and it could never
  succeed, because actors are already rejected as landing surfaces. Two rays now
  classify the blocker first and bail immediately when it is another actor.
- **EVG marker scan searched the entire cell grid.** The search radius is 250 units,
  but the scan called `ForEachReferenceInRange` on *all* attached cells (25 at
  `uGridsToLoad=5`), and each of those iterates every reference in the cell -
  thousands of them in a dense city. Cells whose bounds fall outside the radius are
  now rejected up front, leaving at most the actor's own cell and its neighbours.
- **The EVG self-disable latch could never trip in a city.** It only counted rejected
  activations, and an activation is only attempted once a marker has been found. In an
  area with no EVG markers - which is every city - the scan found nothing, so the
  counter never moved and every stuck NPC kept re-scanning for the whole session. A
  fruitless scan now counts toward the same latch: the path switches itself off after
  3 and re-arms on the next game load.
- **Follower-faction lookup ran per actor, per check.** `IsTeammate` called
  `LookupByID` into the global form table for every scanned NPC four times a second.
  Resolved once and cached.

The two EVG items only affected setups with `bEnableEVGTraversal=1`. That option stays
**off by default**: EVG NPC traversal still cannot work, because furniture entry for an
NPC is driven by the AI package system and activation is rejected by the engine.

## 2.4.8 (2026-08-06)

First genuine native rebuild since 2.4.4. Versions 2.4.5, 2.4.6 and 2.4.7 all
shipped the identical 2.4.4 DLL (SHA256 `9e5616e0...`); 2.4.8 ships a new binary
(SHA256 `f0fe793b...`).

### Fixed

- **NPCs vaulted onto barrels and crates and got stranded on top.** The landing
  surface filter only rejected NPCs, doors and activators. Barrels and urns are
  `Container` records and crates/physics props are `MovableStatic`, so both were
  treated as valid ledges — an NPC would climb one and then be stuck on an
  unnavmeshed prop. `Container`, `MovableStatic`, `Flora` and `Tree` are now
  rejected as landing surfaces.
- **INI edits and the FOMOD preset were silently ignored whenever the ESP was
  active.** Settings were copied out of the plugin's globals into memory every
  single frame, which overwrote everything read from the INI — including the
  preset chosen in the installer. The INI now *seeds* the MCM at `kDataLoaded`,
  which runs before a save is deserialized, so:
  - a new game or fresh install starts with your INI / FOMOD preset;
  - loading an existing save still restores that save's own MCM values;
  - MCM changes still apply instantly and persist.

### Changed

- `iTeleportEscalation` default raised **3 -> 5**. Teleports were firing more
  often than the animated traversal they exist to back up. Set
  `bEnableTeleportFallback=0` to disable teleporting entirely.
- INI header and README now document the actual settings precedence, and the
  `fMaxClimbHeight` default is documented as **130** everywhere (the Nexus
  description previously claimed 250). 130 = steps, vaults and low/chest ledges.
  Raise toward 250 for full cliff and mountain climbing.

### Privacy

- The author field was the local Windows account name and shipped as a plain
  readable string inside **both** `NPCPathingNG.dll` and the `NPCPathingNG.esp`
  header (visible in any mod manager and in xEdit). Both now read **GennyWoo**.
- Scanned every shipped file: no account name, e-mail address or machine name
  remains in the release.

### Note on the plugin hash

- `NPCPathingNG.esp` had been byte-identical from 2.4.4 through 2.4.7
  (`02256817...`). The author-name fix changes it to `9dc50ac1...` (1504 bytes).
  Only the TES4 header `CNAM` differs - no record, FormID or master changed, so
  **no save cleaning is needed** and existing playthroughs are unaffected.

## 2.4.7 (2026-07-25)

- Metadata-only packaging bump.
- FOMOD Author set to **GennyWoo**.
- DLL/ESP unchanged vs 2.4.6 (still the 2.4.4 binary line).
# Changelog

## 2.4.6 - 2026-07-23

### Fixed

- **Double MCM menu after 2.4.5.** Version 2.4.5 swapped the MCM quest script from stock `MCM_ConfigBase` to a custom `NPNG_MCMBridge` on the same FormID. Papyrus script instances persist inside saves, so the old empty menu stayed registered next to the new one — and uninstalling older files could not clear it because it was baked into the save. **2.4.6 restores the 2.4.4 ESP** (`MCM_ConfigBase` only) and does **not** ship `NPNG_MCMBridge`.
- **Native binary unchanged.** The 2.4.4 `NPCPathingNG.dll` is shipped as-is (byte-identical). This is a package/ESP fix only.

### Removed

- `NPNG_MCMBridge.psc` / `.pex` and the 2.4.5 MCM-persistence bridge approach. MCM Helper already persists changed values under `Data/MCM/Settings/` when you use the MCM; the bridge was unnecessary and caused the dual-menu registration.

### Notes for players who already loaded 2.4.5

1. Install **2.4.6** (or reinstall 2.4.4) so the ESP again uses `MCM_ConfigBase`.
2. Remove any leftover `Data/Scripts/NPNG_MCMBridge.pex` if a manager left it behind.
3. The empty duplicate menu may still appear on **existing saves** until that orphaned script instance is gone. Use the working (lower or non-empty) menu, or clean the orphan with a save editor / start a new game. New games only see one menu.

## 2.4.5 - 2026-07-22 (YANKED)

### Yanked — do not use

- Attempted MCM-outside-save persistence by attaching `NPNG_MCMBridge` to the existing MCM quest. Causes **two** MCM entries on any save that previously registered `MCM_ConfigBase` (empty page + working page). Replaced by 2.4.6.

## 2.4.4 - 2026-07-21

### Changed

- **EVG Animated Traversal is now OFF by default, and is confirmed NOT required.** The mod has never had a hard dependency on it (the plugin's only master is Skyrim.esm, and the EVG forms are looked up at runtime and simply skipped when absent). Because NPC marker use cannot work at all - the engine only permits furniture entry through AI packages - leaving it enabled bought nothing and only added marker scans and activation calls. Player-side EVG use is entirely unaffected. Existing saves that already enabled it can turn it off in the MCM.

### Added

- **FOMOD installer with dependency auto-detection.** The installer probes `SkyParkour.esp`, `EVGAnimatedTraversal.esl` and picks a matching configuration:
  - SkyParkour detected: "SkyParkour traversal" is pre-selected.
  - SkyParkour absent: "Navmesh failsafe only" is pre-selected instead.
  - EVG absent or disabled: the experimental EVG option is greyed out entirely, so it cannot be chosen by mistake.
  Every branch installs a matching INI, so the shipped settings always agree with what is actually installed.

## 2.4.3 - 2026-07-21

### Fixed

- **NPCs no longer get pushed sideways out of doorways.** A doorway is a chokepoint, not a wall — sidestepping an NPC out of one removes them from the only route through and was a likely contributor to NPCs milling around doors. The bypass now recognises a door ahead and never repositions there. If the door is simply shut (and unlocked, and not a load door) it gets opened instead, then the NPC's own pathing takes over.
- **EVG marker traversal for NPCs now fails loudly instead of silently.** Activating an EVG furniture marker succeeds for the player but is rejected for NPCs: furniture entry for an NPC is driven by the AI package system, which activation cannot force. This is an engine limitation, confirmed from both SKSE and Papyrus. After repeated rejections the mod disables NPC marker traversal for the session, logs one clear explanation, and falls straight through to SkyParkour traversal — no more burning stuck-cycles or stalling follower replay on a call that cannot succeed.

## 2.4.2 - 2026-07-20

### Fixed

- NPCs no longer get stuck walking diagonally after traversing fences and ledges. Root cause: the engine rights the *player's* pitch/roll from camera input every frame but never rights NPCs, so a vault that ended tilted (root-motion with simulation off, or a slope-rotated EVG furniture marker) left the actor tilted permanently. Every parkour end now zeroes controller pitch/roll and reference pitch/roll, and a 15-second post-traversal posture guard self-heals any residual tilt on the next detection sample (never in combat, so ranged aiming is untouched).

### Changed

- Follower replay is more reliable: trigger radius raised from 80 to 110 units (the anim-start distance cap still prevents wrong-ledge drags) and the player-above threshold lowered from 60 to 40 units, so followers reproduce more of the player's moves instead of falling back to stuck detection.

## 2.4.1 - 2026-07-18

### Fixed

- Restores character-controller simulation and graph state when the mod is disabled during an active NPC parkour animation.
- Separates EVG follower replay from the SkyParkour toggle, prevents replay through a disabled integration, and discards stale events when a toggle changes.
- Clears `SkyParkourLowerBody` before and after every move to prevent state leaking between animations.
- Sends `SkyParkour_Interrupt` only for actual forced stops and timeouts, not normal graph completion.
- Resets teleport escalation after a successful bypass and validates a body-width travel corridor plus ground, headroom, and body-radius clearance at the destination.
- Uses type-safe form casts for MCM globals and the follower faction lookup.
- Stores the ESP HEDR next-object value as the local object ID (`0x813`).
- Corrects the documented default climb height from 250 to 130.
- Synchronizes DLL source, CMake, vcpkg, documentation, and package version metadata.
- Replaces the Visual Studio 2026-only preset and makes CI resolve both standard vcpkg environment variables.

### Distribution

- Adds automated ESP/package validation and a reproducible Nexus ZIP builder.
- Adds a Windows GitHub Actions build that compiles against pinned CommonLibSSE-NG and uploads the release ZIP.
- Excludes compiler output, dependency checkouts, nested repositories, and archive debris from source distributions.
