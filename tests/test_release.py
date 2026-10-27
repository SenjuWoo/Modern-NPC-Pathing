from __future__ import annotations

import ast
import importlib.util
import json
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "3.0.0"
COMMONLIB_TAG = "v14.1.0"
EXPECTED_LOCAL_IDS = {
    0x800, 0x801, 0x802, 0x803, 0x804, 0x805, 0x806, 0x807,
    0x808, 0x809, 0x80A, 0x80B, 0x80C, 0x80D, 0x80E, 0x80F,
    0x810, 0x811, 0x812, 0x813, 0x814, 0x815, 0x816,
}


def read_text(path: Path) -> str:
    """Read project text files as UTF-8 so CI is locale-independent on Windows."""
    return path.read_text(encoding="utf-8")


def read_subrecord(data: bytes, offset: int) -> tuple[bytes, bytes, int]:
    tag = data[offset:offset + 4]
    size = struct.unpack_from("<H", data, offset + 4)[0]
    start = offset + 6
    return tag, data[start:start + size], start + size


class ReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.esp = Path(cls.tmp.name) / "NPCPathingNG.esp"
        subprocess.run([sys.executable, str(ROOT / "generate_esp.py"), str(cls.esp)], check=True, cwd=ROOT)
        cls.data = cls.esp.read_bytes()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def test_version_metadata_is_synchronized(self) -> None:
        self.assertIn(f"VERSION {VERSION}", read_text(ROOT / "CMakeLists.txt"))
        self.assertEqual(json.loads(read_text(ROOT / "vcpkg.json"))["version-semver"], VERSION)
        version_header = read_text(ROOT / "src/version.h")
        self.assertIn(f'String = "{VERSION}"', version_header)
        self.assertIn("(static_cast<std::uint32_t>(Major) << 24)", version_header)
        self.assertIn(f"# NPC Pathing NG v{VERSION}", read_text(ROOT / "README.md"))
        self.assertTrue(read_text(ROOT / "package/README.md").startswith(f"# NPC Pathing NG v{VERSION}"))
        self.assertEqual(read_text(ROOT / "VERSION.txt").strip(), f"NPC Pathing NG {VERSION}")
        self.assertIn(f"## {VERSION}", read_text(ROOT / "CHANGELOG.md"))
        module = ET.fromstring((ROOT / "fomod_src/fomod/ModuleConfig.xml").read_bytes())
        self.assertEqual(module.findtext("moduleName"), f"NPC Pathing NG {VERSION}")
        info = ET.fromstring((ROOT / "fomod_src/fomod/info.xml").read_bytes())
        self.assertEqual(info.find("Version").get("MachineVersion"), VERSION)

    def test_esp_header_and_form_ids(self) -> None:
        self.assertEqual(self.data[:4], b"TES4")
        record_size, flags = struct.unpack_from("<II", self.data, 4)
        self.assertEqual(flags & 0x200, 0x200, "ESP must remain ESL-flagged")
        tag, payload, _ = read_subrecord(self.data, 24)
        self.assertEqual(tag, b"HEDR")
        version, record_count, next_object_id = struct.unpack("<fII", payload)
        self.assertAlmostEqual(version, 1.71, places=5)
        self.assertEqual(record_count, len(EXPECTED_LOCAL_IDS) + 2, "HEDR counts records and groups")
        self.assertEqual(next_object_id, 0x817, "HEDR next object ID must be local, not load-order-prefixed")
        self.assertEqual(record_size + 24, self.data.find(b"GRUP"))

        form_ids: set[int] = set()
        offset = self.data.find(b"GRUP")
        while offset < len(self.data):
            self.assertEqual(self.data[offset:offset + 4], b"GRUP")
            group_size = struct.unpack_from("<I", self.data, offset + 4)[0]
            pos = offset + 24
            end = offset + group_size
            while pos < end:
                size = struct.unpack_from("<I", self.data, pos + 4)[0]
                form_ids.add(struct.unpack_from("<I", self.data, pos + 12)[0] & 0x00FFFFFF)
                pos += 24 + size
            self.assertEqual(pos, end)
            offset = end
        self.assertEqual(form_ids, EXPECTED_LOCAL_IDS)
        self.assertTrue(all(0x800 <= i <= 0xFFF for i in form_ids), "light-plugin local IDs for HEDR 1.71 are 0x800-0xFFF")

    def test_saved_form_ids_are_unchanged(self) -> None:
        """Ids shipped in 2.4.6 - 2.5.0 must keep their meaning so existing saves load unchanged."""
        generator = ast.parse(read_text(ROOT / "generate_esp.py"))
        globs = None
        for node in generator.body:
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "GLOBS" for t in node.targets):
                globs = {row[0]: row[1] for row in ast.literal_eval(node.value)}
        self.assertIsNotNone(globs)
        legacy = {
            0x800: "NPNG_Enabled", 0x801: "NPNG_CheckInterval", 0x802: "NPNG_StuckThreshold",
            0x803: "NPNG_StuckDistance", 0x804: "NPNG_Cooldown", 0x805: "NPNG_ActorsPerFrame",
            0x806: "NPNG_EnableParkour", 0x807: "NPNG_IndoorMode", 0x808: "NPNG_MaxClimbHeight",
            0x809: "NPNG_TeleportFallback", 0x80A: "NPNG_SnapDistance", 0x80B: "NPNG_ExcludeInCombat",
            0x80C: "NPNG_ExcludeFollowers", 0x80D: "NPNG_ExcludeMounted", 0x80E: "NPNG_FollowerReplay",
            0x80F: "NPNG_DebugLogging", 0x811: "NPNG_EVGTraversal", 0x812: "NPNG_TeleportEscalation",
            0x813: "NPNG_ParkourMaxPlayerDistance",
        }
        for fid, name in legacy.items():
            self.assertEqual(globs.get(fid), name)
        self.assertEqual({0x814: "NPNG_EnableLeap", 0x815: "NPNG_CompatMode", 0x816: "NPNG_FrameBudgetMs"},
                         {k: v for k, v in globs.items() if k >= 0x814})

    def test_seq_points_to_start_game_quest(self) -> None:
        seq = self.esp.parent / "Seq/NPCPathingNG.seq"
        self.assertEqual(struct.unpack("<I", seq.read_bytes())[0], 0x01000810)

    def test_mcm_global_references_exist(self) -> None:
        config = json.loads(read_text(ROOT / "package/Data/MCM/Config/NPCPathingNG/config.json"))
        refs: set[int] = set()
        for page in config["pages"]:
            for item in page.get("content", []):
                source = item.get("valueOptions", {}).get("sourceForm")
                if source:
                    refs.add(int(source.split("|")[1], 16))
        self.assertTrue(refs)
        self.assertTrue(refs.issubset(EXPECTED_LOCAL_IDS))
        self.assertNotIn(0x810, refs, "The quest is not a GlobalValue")
        # The 3.0 controls are reachable from the MCM.
        self.assertTrue({0x814, 0x815, 0x816} <= refs)
        names = [p["pageDisplayName"] for p in config["pages"]]
        self.assertIn("Compatibility", names)

    def test_regressions_are_guarded_in_source(self) -> None:
        pathing = read_text(ROOT / "src/pathing.cpp")
        parkour = read_text(ROOT / "src/npc_parkour.cpp")
        evg = read_text(ROOT / "src/evg_traversal.cpp")
        self.assertIn("CancelParkourJobs(true)", pathing)
        self.assertIn("(settings->enableParkour || settings->enableEvgTraversal)", pathing)
        self.assertIn("(isEvg && !settings->enableEvgTraversal)", pathing)
        self.assertIn("it = playerEvents.erase(it)", pathing)
        self.assertIn("OnParkourEnd(actor, timedOut)", pathing)
        self.assertIn("OnParkourEnd(actor, true);", pathing)
        self.assertIn("TryTeleportBypass(a_actor))", pathing)
        self.assertIn("pathBlocked", pathing)
        self.assertIn("Raycast::LandingIsClear", pathing)
        self.assertIn("IsActorHit(ground)", pathing)
        self.assertGreaterEqual(parkour.count("SetGraphVariableBool(SkyParkourGraph::VarLowerBody(), false)"), 2)
        self.assertIn("WithinParkourRange", pathing)
        self.assertIn("parkourMaxPlayerDistance", pathing)
        self.assertNotIn("ActivateRef", evg)
        self.assertIn("RouteKind", evg)
        self.assertIn("KindFor", evg)
        self.assertIn("IsRouteEnabled", evg)
        self.assertIn("NoteRouteSuccess", evg)
        self.assertIn("TryEvgHop", pathing)

    def test_performance_design_is_present(self) -> None:
        pathing = read_text(ROOT / "src/pathing.cpp")
        evg = read_text(ROOT / "src/evg_traversal.cpp")
        self.assertIn("DrainPending", pathing)            # heavy work is queued, not run inline
        self.assertIn("kMaxHeavyPerFrame", pathing)
        self.assertIn("budget.Exhausted()", pathing)
        self.assertIn("settingsAccum", pathing)           # MCM globals are not copied every frame
        self.assertIn("processRadius", pathing)           # far actors are skipped
        self.assertIn("nextReplayTry", pathing)           # follower replay is throttled
        self.assertIn("g_cellMarkers", evg)               # EVG markers cached per cell
        self.assertNotRegex(evg, r"[>.]ForEachReferenceInRange\(")  # the per-NPC full-cell scan is gone
        parkour_types = read_text(ROOT / "src/parkour_types.h")
        self.assertNotIn("constexpr const char*", parkour_types, "graph names must be interned BSFixedStrings")

    def test_compat_layer_covers_both_mods(self) -> None:
        compat = read_text(ROOT / "src/compat.cpp")
        for token in ("FluidMotion.dll", "FluidMotionAnimation.dll", "FluidMotionNPC.dll", "NpcTraversal.dll",
                      "NPCT_ActiveFlights", "FMNPC_IsTraversing", "FMNPC_Claim", "SkyParkourNG.dll",
                      "StepUpOntoSKSE.dll", "jump/benabled", "npcclimb/bclimb"):
            self.assertIn(token, compat)
        pathing = read_text(ROOT / "src/pathing.cpp")
        self.assertIn("Compat::ExternalTraversing", pathing)
        self.assertIn("Compat::AssistOnly()", pathing)
        self.assertIn("Compat::JumpsOwnedByOther()", pathing)
        self.assertIn("Compat::Claim", pathing)
        leap = read_text(ROOT / "src/leap.cpp")
        self.assertIn("Compat::JumpsOwnedByOther()", leap)
        self.assertIn("Compat::ExternalTraversing", leap)
        # Compat is resolved after every plugin has loaded, whatever the load order.
        main = read_text(ROOT / "src/main.cpp")
        self.assertRegex(main, r"kPostLoad:\s*//[^\n]*\n\s*(//[^\n]*\n\s*)*Compat::Detect\(\)")

    def test_hooks_use_runtime_relocation_and_ownership_check(self) -> None:
        hooks = read_text(ROOT / "src/hooks.cpp")
        self.assertIn("REL::Relocate<std::size_t>(0xAD, 0xAD, 0xAF)", hooks)
        self.assertIn("candidate_not_executable", hooks)
        self.assertIn("chaining behind", hooks)
        self.assertIn("textx", hooks)
        main = read_text(ROOT / "src/main.cpp")
        self.assertIn("SKSE::StructCompatibility::Independent", main)
        self.assertIn("SKSE::VersionIndependence::AddressLibrary", main)

    def test_evg_default_is_enabled_everywhere(self) -> None:
        settings_h = read_text(ROOT / "src/settings.h")
        self.assertIn("enableEvgTraversal = true", settings_h)
        ini = read_text(ROOT / "package/Data/SKSE/Plugins/NPCPathingNG.ini")
        self.assertIn("bEnableEVGTraversal=1", ini)
        generator = read_text(ROOT / "generate_esp.py")
        self.assertIn("(0x811, 'NPNG_EVGTraversal',     1.0)", generator)

    def test_every_ini_key_the_plugin_reads_ships_in_the_ini(self) -> None:
        settings = read_text(ROOT / "src/settings.cpp")
        keys = set(re.findall(r'"([a-z]+/[a-z]+)"', settings))
        keys.discard("section/key")
        ini_lines = read_text(ROOT / "package/Data/SKSE/Plugins/NPCPathingNG.ini").splitlines()
        shipped: set[str] = set()
        section = ""
        for line in ini_lines:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1].lower()
            elif "=" in line and not line.startswith(";"):
                shipped.add(f"{section}/{line.split('=', 1)[0].strip().lower()}")
        self.assertTrue(keys)
        self.assertEqual(sorted(keys - shipped), [], "settings.cpp reads keys the shipped INI does not document")

    def test_shipping_dll_has_required_skse_exports(self) -> None:
        dll_path = ROOT / "package/Data/SKSE/Plugins/NPCPathingNG.dll"
        if not dll_path.exists():
            self.skipTest("DLL is produced by the Windows release build and is not committed")
        spec = importlib.util.spec_from_file_location("package_release", ROOT / "tools/package_release.py")
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        dll = dll_path.read_bytes()
        self.assertTrue({"SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version"}.issubset(module.read_pe_exports(dll)))
        self.assertIn(VERSION.encode("ascii"), dll)

    def test_settings_bindings_match_generated_globals(self) -> None:
        generator = ast.parse(read_text(ROOT / "generate_esp.py"))
        globs = None
        for node in generator.body:
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "GLOBS" for t in node.targets):
                globs = ast.literal_eval(node.value)
                break
        self.assertIsNotNone(globs)
        generated_ids = {row[0] for row in globs}
        settings = read_text(ROOT / "src/settings.cpp")
        bound_ids = {int(value, 16) for value in re.findall(r"= lookup\((0x[0-9A-Fa-f]+)\);", settings)}
        self.assertEqual(bound_ids, generated_ids)

    def test_fomod_is_one_automatic_installer(self) -> None:
        root = ET.fromstring((ROOT / "fomod_src/fomod/ModuleConfig.xml").read_bytes())
        self.assertIsNone(root.find("installSteps"), "the installer must not ask questions")
        self.assertIsNotNone(root.find("requiredInstallFiles"))
        deps = [fd.get("file") for fd in root.iter("fileDependency")]
        self.assertEqual(deps, [r"SKSE\Plugins\StepUpOntoSKSE.dll"])
        raw = (ROOT / "fomod_src/fomod/ModuleConfig.xml").read_bytes()
        self.assertTrue(raw.startswith(b'<?xml version="1.0" encoding="utf-8"?>'))

    def test_build_presets_and_ci_are_portable(self) -> None:
        presets = json.loads(read_text(ROOT / "CMakePresets.json"))
        names = {p["name"]: p for p in presets["configurePresets"]}
        self.assertEqual(names["windows-msvc"]["generator"], "Visual Studio 18 2026")
        self.assertEqual(names["windows-msvc-2022"]["generator"], "Visual Studio 17 2022")
        self.assertEqual(names["windows-msvc"]["cacheVariables"]["VCPKG_TARGET_TRIPLET"], "x64-windows-static")
        workflow = read_text(ROOT / ".github/workflows/build.yml")
        self.assertRegex(workflow, r"actions/setup-python@v\d+")
        self.assertIn("VCPKG_INSTALLATION_ROOT", workflow)
        self.assertIn(f"COMMONLIB_TAG: {COMMONLIB_TAG}", workflow)
        self.assertIn(f"git -C extern/CommonLibSSE checkout {COMMONLIB_TAG}", read_text(ROOT / "BUILDING.md"))
        self.assertIn(f'$CommonLibTag = "{COMMONLIB_TAG}"', read_text(ROOT / "build_release.ps1"))
        self.assertIn("alandtse/CommonLibSSE-NG", workflow)

    def test_documented_default_matches_runtime(self) -> None:
        readme = read_text(ROOT / "README.md")
        self.assertIn("Default climb height is 130 units", readme)
        self.assertIn("fMaxClimbHeight=130.0", read_text(ROOT / "package/Data/SKSE/Plugins/NPCPathingNG.ini"))
        self.assertIn("float maxClimbHeight = 130.0f", read_text(ROOT / "src/settings.h"))
        for token in ("Fluid Motion", "NpcTraversalNG", "Enemies Can Jump and Vault", "CommonLibSSE-NG"):
            self.assertIn(token, readme)


if __name__ == "__main__":
    unittest.main()
