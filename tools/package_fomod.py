#!/usr/bin/env python3
"""Assemble and verify the FOMOD archive (one page, one optional StepUpOnto patch tick-box).

Only fomod_src/fomod/*.xml is authored. Everything the installer ships is copied from
package/Data (plugin, SEQ, MCM config, INI, DLL) and from the StepUpOnto patch build output,
so the installer cannot drift from the validated plugin.

The archive is built, then re-opened and checked: exact member allowlist, payload hashes against
the staging tree, every FOMOD source present, and every install branch simulated.
"""
from __future__ import annotations

import hashlib
import importlib.util
import shutil
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path, PureWindowsPath

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "package" / "Data"
AUTHORED = ROOT / "fomod_src" / "fomod"
STAGING = ROOT / "dist" / "fomod_staging"
DIST = ROOT / "dist"
STEPUP_DLL = ROOT / "build" / "patch_stepuponto" / "SKSE" / "Plugins" / "StepUpOntoSKSE.dll"
FIXED_TIMESTAMP = (2026, 10, 27, 0, 0, 0)

# Everything that may exist in the archive, and nothing else.
CORE_FILES = (
    "NPCPathingNG.esp",
    "Seq/NPCPathingNG.seq",
    "SKSE/Plugins/NPCPathingNG.dll",
    "SKSE/Plugins/NPCPathingNG.ini",
    "MCM/Config/NPCPathingNG/config.json",
)
PATCH_FILE = "SKSE/Plugins/StepUpOntoSKSE.dll"
DOC_FILES = ("README.md", "LICENSE")
FORBIDDEN_SUFFIXES = {".pdb", ".obj", ".lib", ".exp", ".ilk", ".log", ".zip", ".7z", ".pyc"}


def _load_release_helpers():
    spec = importlib.util.spec_from_file_location("package_release", ROOT / "tools" / "package_release.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release = _load_release_helpers()


def read_version() -> str:
    return release.read_version()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def stage() -> None:
    if STAGING.exists():
        shutil.rmtree(STAGING)
    (STAGING / "fomod").mkdir(parents=True)

    for xml in AUTHORED.glob("*.xml"):
        shutil.copy2(xml, STAGING / "fomod" / xml.name)

    for rel in CORE_FILES:
        src = PACKAGE / rel
        if not src.is_file():
            raise RuntimeError(f"missing required package file: {rel}")
        dst = STAGING / "core" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if rel.endswith(".ini"):
            # Windows users edit this in Notepad: ship CRLF, normalised from whatever the repo has.
            text = src.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\n", "\r\n")
            dst.write_bytes(text.encode("utf-8"))
        else:
            shutil.copy2(src, dst)

    if not STEPUP_DLL.is_file():
        raise RuntimeError(f"StepUpOnto patch DLL not built: {STEPUP_DLL}")
    dst = STAGING / "patch_stepuponto" / PATCH_FILE
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(STEPUP_DLL, dst)

    shutil.copy2(ROOT / "package" / "README.md", STAGING / "README.md")  # the short, user-facing readme
    shutil.copy2(ROOT / "LICENSE", STAGING / "LICENSE")


def validate_dlls(version: str) -> None:
    release.validate_pe_x64(STAGING / "core" / "SKSE/Plugins/NPCPathingNG.dll", version)
    patch = (STAGING / "patch_stepuponto" / PATCH_FILE).read_bytes()
    if patch[:2] != b"MZ":
        raise RuntimeError("StepUpOnto patch is not a PE image")
    pe = struct.unpack_from("<I", patch, 0x3C)[0]
    if struct.unpack_from("<H", patch, pe + 4)[0] != 0x8664:
        raise RuntimeError("StepUpOnto patch is not x64")
    missing = {"SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version"} - release.read_pe_exports(patch)
    if missing:
        raise RuntimeError(f"StepUpOnto patch lacks SKSE exports: {sorted(missing)}")


def _children(elem: ET.Element) -> list[str]:
    return [c.tag for c in elem]


def validate_config() -> ET.Element:
    """Parse ModuleConfig.xml and check structure; return its root."""
    cfg = STAGING / "fomod" / "ModuleConfig.xml"
    raw = cfg.read_bytes()
    if not raw.startswith(b'<?xml version="1.0" encoding="utf-8"?>'):
        raise RuntimeError("ModuleConfig.xml must start with the double-quoted utf-8 declaration")
    root = ET.fromstring(raw)
    info = ET.fromstring((STAGING / "fomod" / "info.xml").read_bytes())  # must parse
    if info.tag != "fomod":
        raise RuntimeError("info.xml root must be <fomod>")

    # ModConfig 5.0 element order: moduleName, moduleImage, moduleDependencies,
    # requiredInstallFiles, installSteps, conditionalFileInstalls.
    order = ["moduleName", "moduleImage", "moduleDependencies", "requiredInstallFiles", "installSteps",
             "conditionalFileInstalls"]
    seen = [order.index(t) for t in _children(root)]
    if seen != sorted(seen):
        raise RuntimeError(f"ModuleConfig children out of schema order: {_children(root)}")

    for elem in root.iter():
        src = elem.attrib.get("source")
        if src and not (STAGING / PureWindowsPath(src).as_posix()).exists():
            raise RuntimeError(f"ModuleConfig references missing source: {src}")
        if elem.tag == "folder" and "destination" in elem.attrib and elem.attrib["destination"].lower().startswith("data"):
            raise RuntimeError("FOMOD destinations are relative to Data; do not write a Data/ prefix")

    # MO2 only evaluates file dependencies on .esp/.esm/.esl (anything else is always "Missing"), and
    # 3.0.0 shipped with zero pages. Keep the plain layout every manager handles: one page, no conditions.
    if root.find("conditionalFileInstalls") is not None:
        raise RuntimeError("no conditionalFileInstalls: the optional patch is a plain tick-box")
    for fd in root.iter("fileDependency"):
        if Path(PureWindowsPath(fd.attrib["file"]).as_posix()).suffix.lower() not in {".esp", ".esm", ".esl"}:
            raise RuntimeError(f"fileDependency on a non-plugin file never fires in MO2: {fd.attrib['file']}")
    if len(root.findall("installSteps/installStep")) != 1:
        raise RuntimeError("the installer must have exactly one page")
    return root


def _tree_files(folder: Path) -> set[str]:
    return {p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()}


def simulate_branches(root: ET.Element) -> dict[str, set[str]]:
    """Install result for every subset of the optional plugins (tick-boxes)."""
    base: set[str] = set()
    for folder in root.find("requiredInstallFiles"):
        base |= _tree_files(STAGING / folder.attrib["source"])

    plugins = root.findall("installSteps/installStep/optionalFileGroups/group/plugins/plugin")
    results: dict[str, set[str]] = {}
    for mask in range(1 << len(plugins)):
        chosen = [p for i, p in enumerate(plugins) if mask >> i & 1]
        installed = set(base)
        for plugin in chosen:
            for folder in plugin.iter("folder"):
                installed |= _tree_files(STAGING / folder.attrib["source"])
        results["+".join(p.attrib["name"] for p in chosen) or "(nothing ticked)"] = installed
    return results


def check_branches(branches: dict[str, set[str]]) -> None:
    core = set(CORE_FILES)
    for name, files in branches.items():
        if not core <= files:
            raise RuntimeError(f"branch {name}: core files missing: {sorted(core - files)}")
        extra = files - core - {PATCH_FILE}
        if extra:
            raise RuntimeError(f"branch {name}: unexpected installed files: {sorted(extra)}")
        has_patch = PATCH_FILE in files
        wants_patch = "stepuponto" in name.lower()
        if has_patch != wants_patch:
            raise RuntimeError(f"branch {name}: StepUpOnto patch installed={has_patch}, expected {wants_patch}")
    if len(branches) != 2:
        raise RuntimeError(f"expected 2 install branches, simulated {len(branches)}")


def write_zip(version: str) -> Path:
    out = DIST / f"NPC Pathing NG {version} FOMOD.zip"
    if out.exists():
        out.unlink()
    files = sorted(p for p in STAGING.rglob("*") if p.is_file())
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in files:
            info = zipfile.ZipInfo(f.relative_to(STAGING).as_posix(), FIXED_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, f.read_bytes())
    return out


def verify_archive(out: Path) -> None:
    """Re-open the archive: member allowlist, payload hashes, FOMOD sources, no Data/ or junk."""
    allowed = {"fomod/ModuleConfig.xml", "fomod/info.xml", *DOC_FILES}
    allowed |= {f"core/{f}" for f in CORE_FILES}
    allowed.add(f"patch_stepuponto/{PATCH_FILE}")
    with zipfile.ZipFile(out) as z:
        if z.testzip() is not None:
            raise RuntimeError("archive failed its CRC test")
        names = set(z.namelist())
        if names != allowed:
            raise RuntimeError(f"archive members differ from allowlist: +{sorted(names - allowed)} -{sorted(allowed - names)}")
        for n in names:
            if Path(n).suffix.lower() in FORBIDDEN_SUFFIXES or n.split("/")[0].lower() == "data":
                raise RuntimeError(f"forbidden archive member: {n}")
            if sha256(z.read(n)) != sha256((STAGING / n).read_bytes()):
                raise RuntimeError(f"archive payload differs from staging: {n}")
        cfg = ET.fromstring(z.read("fomod/ModuleConfig.xml"))
        for elem in cfg.iter():
            src = elem.attrib.get("source")
            if src and not any(n.startswith(PureWindowsPath(src).as_posix().rstrip("/") + "/") for n in names):
                raise RuntimeError(f"archive lacks FOMOD source folder: {src}")
        # The DLL inside the archive is the one that was validated.
        dll = z.read("core/SKSE/Plugins/NPCPathingNG.dll")
        if dll != (PACKAGE / "SKSE/Plugins/NPCPathingNG.dll").read_bytes():
            raise RuntimeError("archive DLL is not the built package DLL")


def main() -> int:
    version = read_version()
    DIST.mkdir(parents=True, exist_ok=True)
    stage()
    validate_dlls(version)
    root = validate_config()
    if f"NPC Pathing NG {version}" != (root.findtext("moduleName") or ""):
        raise RuntimeError(f"moduleName {root.findtext('moduleName')!r} does not match version {version}")
    info = ET.fromstring((STAGING / "fomod" / "info.xml").read_bytes())
    if info.find("Version") is None or info.find("Version").get("MachineVersion") != version:
        raise RuntimeError("info.xml MachineVersion does not match src/version.h")
    branches = simulate_branches(root)
    check_branches(branches)
    out = write_zip(version)
    verify_archive(out)
    digest = sha256(out.read_bytes())
    (DIST / "SHA256SUMS.txt").write_text(f"{digest}  {out.name}\n", encoding="utf-8")
    for name, files in branches.items():
        print(f"branch [{name}]: {len(files)} files")
    print(f"Built {out} ({out.stat().st_size:,} bytes)")
    print(f"SHA256 {digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - report any packaging failure as one line
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
