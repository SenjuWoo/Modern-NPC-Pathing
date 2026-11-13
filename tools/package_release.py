#!/usr/bin/env python3
"""Build a clean, deterministic Nexus Mods archive from package/."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import re
import shutil
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "package"
DIST = ROOT / "dist"
REQUIRED_FILES = (
    Path("Data/NPCPathingNG.esp"),
    Path("Data/MCM/Config/NPCPathingNG/config.json"),
    Path("Data/Seq/NPCPathingNG.seq"),
    Path("Data/SKSE/Plugins/NPCPathingNG.dll"),
    Path("Data/SKSE/Plugins/NPCPathingNG.ini"),
    Path("README.md"),
)
FORBIDDEN_PARTS = {
    ".git",
    ".vs",
    "build",
    "extern",
    "obj",
    "CMakeFiles",
    "__pycache__",
}
FORBIDDEN_SUFFIXES = {".pdb", ".obj", ".lib", ".exp", ".ilk", ".log", ".zip", ".7z"}
FIXED_TIMESTAMP = (2026, 7, 18, 0, 0, 0)


def read_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(NPCPathingNG VERSION ([0-9]+\.[0-9]+\.[0-9]+)", text)
    if not match:
        raise RuntimeError("Could not read semantic version from CMakeLists.txt")
    return match.group(1)


def generate_plugin() -> None:
    spec = importlib.util.spec_from_file_location("generate_esp", ROOT / "generate_esp.py")
    if not spec or not spec.loader:
        raise RuntimeError("Could not import generate_esp.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.generate(str(PACKAGE / "Data/NPCPathingNG.esp"))


def _rva_to_offset(data: bytes, section_table: int, section_count: int, rva: int) -> int:
    for index in range(section_count):
        offset = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            return raw_offset + (rva - virtual_address)
    raise RuntimeError(f"PE RVA 0x{rva:X} is outside every section")


def read_pe_exports(data: bytes) -> set[str]:
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise RuntimeError("Not a valid PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise RuntimeError("Missing PE signature")

    file_header = pe_offset + 4
    section_count = struct.unpack_from("<H", data, file_header + 2)[0]
    optional_size = struct.unpack_from("<H", data, file_header + 16)[0]
    optional = file_header + 20
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic != 0x20B:
        raise RuntimeError(f"Expected PE32+ optional header, got 0x{magic:04X}")

    export_rva, export_size = struct.unpack_from("<II", data, optional + 112)
    if not export_rva or not export_size:
        return set()
    section_table = optional + optional_size
    export_offset = _rva_to_offset(data, section_table, section_count, export_rva)
    number_of_names = struct.unpack_from("<I", data, export_offset + 24)[0]
    names_rva = struct.unpack_from("<I", data, export_offset + 32)[0]
    names_offset = _rva_to_offset(data, section_table, section_count, names_rva)

    exports: set[str] = set()
    for index in range(number_of_names):
        name_rva = struct.unpack_from("<I", data, names_offset + index * 4)[0]
        name_offset = _rva_to_offset(data, section_table, section_count, name_rva)
        end = data.find(b"\0", name_offset)
        if end < 0:
            raise RuntimeError("Unterminated PE export name")
        exports.add(data[name_offset:end].decode("ascii"))
    return exports


def validate_pe_x64(path: Path, version: str) -> None:
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise RuntimeError(f"{path} is not a valid PE DLL")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise RuntimeError(f"{path} has no PE signature")
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    if machine != 0x8664:
        raise RuntimeError(f"{path} is not an x64 DLL (machine=0x{machine:04X})")

    required_exports = {"SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version"}
    missing_exports = required_exports - read_pe_exports(data)
    if missing_exports:
        raise RuntimeError(f"{path} is missing SKSE exports: {', '.join(sorted(missing_exports))}")
    if version.encode("ascii") not in data:
        raise RuntimeError(f"{path} does not embed release version {version}")


def validate_tree(version: str, dll_version: str | None = None) -> list[Path]:
    missing = [str(path) for path in REQUIRED_FILES if not (PACKAGE / path).is_file()]
    if missing:
        raise RuntimeError("Missing release files: " + ", ".join(missing))

    # Package-only releases may ship an older, unmodified DLL (e.g. 2.4.6 ESP fix
    # reusing the 2.4.4 binary). Check the embedded string the DLL actually has.
    validate_pe_x64(PACKAGE / "Data/SKSE/Plugins/NPCPathingNG.dll", dll_version or version)

    files: list[Path] = []
    for path in sorted(PACKAGE.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(PACKAGE)
        if any(part in FORBIDDEN_PARTS for part in rel.parts):
            raise RuntimeError(f"Forbidden directory in package: {rel}")
        if path.suffix.lower() in FORBIDDEN_SUFFIXES:
            raise RuntimeError(f"Forbidden file in package: {rel}")
        files.append(rel)
    return files


def write_zip(version: str, files: list[Path]) -> Path:
    DIST.mkdir(parents=True, exist_ok=True)
    output = DIST / f"NPC Pathing NG {version}.zip"
    if output.exists():
        output.unlink()

    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for rel in files:
            info = zipfile.ZipInfo(rel.as_posix(), FIXED_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, (PACKAGE / rel).read_bytes())
    return output


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-generate", action="store_true", help="Do not regenerate the ESP/SEQ")
    parser.add_argument(
        "--dll-version",
        default=None,
        help="Version string embedded in the shipped DLL when it differs from package version "
        "(package-only releases that reuse an older binary).",
    )
    args = parser.parse_args()

    version = read_version()
    if not args.skip_generate:
        generate_plugin()
    # Prefer package/README.md when present (release notes); fall back to repo root.
    package_readme = PACKAGE / "README.md"
    root_readme = ROOT / "README.md"
    if root_readme.is_file() and not package_readme.is_file():
        shutil.copy2(root_readme, package_readme)
    files = validate_tree(version, dll_version=args.dll_version)
    output = write_zip(version, files)
    digest = sha256(output)
    (DIST / "SHA256SUMS.txt").write_text(f"{digest}  {output.name}\n", encoding="utf-8")
    print(f"Built {output} ({output.stat().st_size:,} bytes)")
    print(f"SHA256 {digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
