# Building NPC Pathing NG

## Requirements

- Windows x64
- Visual Studio 2022 or 2026 with the Desktop C++ workload (MSVC 19.4x or newer)
- CMake 3.25+
- Python 3.10+
- vcpkg (`VCPKG_ROOT` or `VCPKG_INSTALLATION_ROOT` set; `git clone https://github.com/microsoft/vcpkg` and run `bootstrap-vcpkg.bat`)
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) tag **v14.1.0** (SE + AE + VR layouts in one library)

The plugin links the C++ runtime statically (`/MT`, vcpkg triplet `x64-windows-static`), so players need no Visual C++ Redistributable. The vcpkg dependencies come from `vcpkg.json` (manifest mode, baseline pinned) and are restored automatically on configure.

## One-command release build

From PowerShell in the repository root:

```powershell
./build_release.ps1
```

The script clones CommonLibSSE-NG at the pinned tag, configures and builds the plugin and the StepUpOnto patch (CommonLib itself compiles once, roughly 15 minutes cold), runs the unit tests, and writes the FOMOD to `dist/`.

The clean source tree intentionally does not contain a DLL.

## Manual build

```powershell
git clone https://github.com/alandtse/CommonLibSSE-NG.git extern/CommonLibSSE
git -C extern/CommonLibSSE checkout v14.1.0
git -C extern/CommonLibSSE submodule update --init --depth 1

$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset windows-msvc          # use windows-msvc-2022 on Visual Studio 2022
cmake --build build --config RelWithDebInfo --target NPCPathingNG StepUpOntoSKSE

python generate_esp.py
python -m unittest discover -s tests -v
tools\run_unit_tests.bat             # game-independent arc solver tests (MSVC only)
python tools/package_fomod.py        # dist/NPC Pathing NG <version> FOMOD.zip
```

The plugin DLL is written to `package/Data/SKSE/Plugins/NPCPathingNG.dll`; the StepUpOnto patch DLL to `build/patch_stepuponto/SKSE/Plugins/StepUpOntoSKSE.dll` and is shipped only through the FOMOD.

`package_fomod.py` re-opens the archive it wrote and fails on: a member outside the allowlist, a payload that differs from staging, a missing FOMOD source, a DLL without the SKSE exports or the version string, or an install branch that does not simulate to the expected file set.

Do not distribute `build/`, `extern/`, `vcpkg_installed/`, vcpkg packages, object files, PDBs, nested `.git` directories, or old ZIP files. They are development material and can inflate a sub-megabyte mod into hundreds of megabytes.

## CommonLibSSE-NG upgrades

The tag is pinned in three places that must change together: `BUILDING.md`, `build_release.ps1` and `.github/workflows/build.yml` (`COMMONLIB_TAG`); `tests/test_release.py` fails when they disagree. Also compare the dependency list in `vcpkg.json` with the library's own `vcpkg.json`. After an upgrade, rebuild from a clean `build/` directory.
