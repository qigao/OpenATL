#!/usr/bin/env python3
"""Qualify the repository-vendored, immutable WTL 10.0.10320 source.

SourceForge/GitHub are NOT runtime download providers. Both direct native
builds and Linux/macOS PE cross-builds consume one checked-in source archive.
Native official WTL 10 sample/COM tests independently prove behavior.
"""
from pathlib import Path
import hashlib
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / "third_party/wtl/WTL10_10320_Final.zip"
SHA512 = (
    "1890711aac1ade673fb071da2f52e2c7bb30fe394b7db735ff74b4578c8c8"
    "eea8bd470924dbad013a567d0cc9b97d865060d72ea6d7330d393af5b20b1e09970"
)
SAMPLES = {
    "Aero", "Alpha", "BmpView", "GuidGen", "MDIDocVw",
    "MemDlg", "MTPad", "MTPad7", "TabBrowser", "Wizard97Test",
    "WTLExplorer",
}


def check() -> int:
    dependency = (ROOT / "cmake/OpenATLWTL.cmake").read_text("utf-8")
    recipe = (ROOT / "cmake/wtl/PrepareWtl.cmake").read_text("utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
    workflow = (ROOT / ".github/workflows/openatl-qualification.yml").read_text("utf-8")
    errors = []
    tracked = set(subprocess.check_output(
        ["git", "-C", str(ROOT), "ls-files", "-z"], timeout=30
    ).decode("utf-8").split("\0"))
    if "third_party/wtl/WTL10_10320_Final.zip" not in tracked:
        errors.append("WTL10 original source archive must be tracked in OpenATL")
    if not ARCHIVE.is_file():
        errors.append("WTL10 vendored ZIP archive missing")
    else:
        digest = hashlib.sha512(ARCHIVE.read_bytes()).hexdigest()
        if digest != SHA512:
            errors.append(f"WTL10 archive SHA512 mismatch: {digest}")
        else:
            try:
                with zipfile.ZipFile(ARCHIVE) as z:
                    infos = z.infolist()
                    names = [i.filename for i in infos]
                    if len(names) != len(set(n.casefold() for n in names)):
                        errors.append("WTL10 ZIP contains duplicate case-folded names")
                    if len(infos) != 327 or sum(i.file_size for i in infos) > 30 * 1024 * 1024:
                        errors.append("WTL10 ZIP member count/size differs from pinned release")
                    for item in infos:
                        path = item.filename.replace("\\", "/")
                        if (path != item.filename or path.startswith("/") or
                                any(part in (".", "..") for part in path.split("/"))):
                            errors.append(f"Unsafe WTL ZIP member: {item.filename}")
                            break
                        if (item.external_attr >> 16) & 0o170000 == 0o120000:
                            errors.append(f"WTL ZIP symlink forbidden: {item.filename}")
                            break
                    present = {p.split("/")[1] for p in names if p.startswith("Samples/")
                               and len(p.split("/")) > 1 and p.split("/")[1]}
                    required = {"Include/atlapp.h", "MS-PL.txt",
                                "Samples/GuidGen/GuidGen.cpp",
                                "Samples/WTLExplorer/WTLExplorer.cpp"}
                    if not required.issubset(names) or present != SAMPLES:
                        errors.append("WTL10 must retain license, headers and all 11 original samples")
                    else:
                        app = z.read("Include/atlapp.h")
                        license_bytes = z.read("MS-PL.txt")
                        if b"_WTL_VER" not in app or b"Windows Template Library" not in app:
                            errors.append("WTL10 main source header is not the expected upstream code")
                        if b"Microsoft Public License" not in license_bytes:
                            errors.append("WTL10 upstream MS-PL license missing or modified")
                        if z.getinfo("Include/atlapp.h").CRC != 0x4EF9CF3B:
                            errors.append("WTL10 atlapp.h CRC differs from pinned mirror")
                    bad = z.testzip()
                    if bad is not None:
                        errors.append(f"WTL10 bad member CRC: {bad}")
            except (OSError, zipfile.BadZipFile, RuntimeError, ValueError) as exc:
                errors.append(f"Invalid WTL ZIP: {exc}")

    if ('FetchContent_Declare(openatl_wtl_source' not in dependency
            or '"${_wtl_archive}"' not in dependency or SHA512 not in dependency
            or 'file(SHA512 "${_wtl_archive}"' not in dependency):
        errors.append("OpenATL WTL dependency must use only the local SHA512-pinned source")
    if ("downloads.sourceforge.net" in dependency or "https://" in dependency
            or "file(DOWNLOAD" in dependency or "ExternalProject" in dependency):
        errors.append("Online WTL source download or second provider forbidden")
    if 'target_include_directories(openatl_atl INTERFACE "${_wtl_headers}")' not in dependency:
        errors.append("OpenATL::ATL must expose build-local adapted WTL headers")
    if ("function(_openatl_replace_between" not in recipe or
            "class CAppModule : public ATL::CComModule" not in recipe):
        errors.append("Build-local WTL adapter must retain its source-preserving contract")
    if ("atlapp.h-openatl-ui-only.patch" in recipe or
            (ROOT / "ports/wtl/atlapp.h-openatl-ui-only.patch").exists()):
        errors.append("Reject obsolete WTL source patches")
    if "if(DEFINED OPENATL_WTL_INCLUDE_DIR)" not in cmake:
        errors.append("Alternative unchecked WTL include root must remain rejected")
    if "wtltest-ui-only.patch" in workflow:
        errors.append("Official/native WTL sample patch forbidden")
    if "--preset" not in workflow or "cmake --build" not in workflow:
        errors.append("Original WTL sample builds and CTest must remain enabled")

    if errors:
        for error in errors:
            print("WTL_PROVENANCE_FAIL " + error, file=sys.stderr)
        return 1
    print("WTL_PROVENANCE_OK version=10.0.10320 archive=repository-owned "
          "source=single-verified-zip license=MS-PL samples=11 "
          "sha512=verified external_downloads=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(check())
