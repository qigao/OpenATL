#!/usr/bin/env python3
"""Native ARM64 CTest-equivalent for PE artifacts produced on x64 Windows.

This is an acceptance test, not a build/bootstrap script. It checks a
SHA256 provenance manifest, genuine Windows ARM64 execution, resources,
and each existing GUI/no-COM, ATL utility, W^X, and COM-client mode.
The pinned WTL10 SDI is PE-verified and exercised through real Windows menu/dialog, button and Unicode edit-change events.
"""
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

TARGETS = (
    "openatl_gui_runtime",
    "openatl_atl_core",
    "openatl_atl_path",
    "openatl_atl_extensions",
    "openatl_atl_mapping",
    "openatl_atl_image",
    "openatl_thunk_fault",
    "openatl_mitigation_runtime",
    "openatl_unload_runner",
    "openatl_unload_native",
    "openatl_com_client",
    "openatl_wtl10_sdi",
)

CASES = (
    ("openatl_gui_runtime", (), "WTL10 GUI runtime PASS: no COM apartment, W^X thunk x64/ARM64, message maps, resource dialog, NCDESTROY, OnFinalMessage, MRU"),
    ("openatl_atl_core", (), "ATL_CORE_OK file=1 string=1 sync=1 registry=1 no_com=1"),
    ("openatl_atl_path", (), "ATL_PATH_OK unicode=1 file=1 relative=1 root=1"),
    ("openatl_atl_extensions", (), "ATL_EXTENSIONS_OK heap=1 filetime=1 array=1 list=1 no_com=1"),
    ("openatl_atl_mapping", (), "ATL_MAPPING_OK file=1 offset=1 copy=1 shared=1 cleanup=1 no_com=1"),
    ("openatl_atl_image", (), "ATL_IMAGE_OK dib=1 attach=1 draw=1 orientation=1 codec=1 stream=1 shortwrite=1 shortread=1 budget=1 alpha=1 cleanup=1 no_com=1"),
    ("openatl_thunk_fault", (), "THUNK_FAULT_OK alloc=1 rx=1 flush=1 reinit_rw=1 free=1 recovery=1 no_rwx=1 window_denied=1 dialog_denied=1"),
    ("openatl_mitigation_runtime", (), "OPENATL_MITIGATION_OK cfg=1 rx=1 indirect=1 deny=1"),
    ("openatl_unload_runner", (), "OPENATL_UNLOAD_OK rounds=12 events=1 lease=1 held=1 blocked=1 callers_joined=1 caller_leases=1 lease_bounds=1 thunk_freed=1 class_unregistered=1 dll_unmapped=1 no_com=1"),
    ("openatl_unload_native", (), "OPENATL_UNLOAD_OK cycles=8 blocked=1 busy=1 retire=1 free=1 no_com=1"),
    ("openatl_com_client", (), "ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0"),
    ("openatl_com_client", ("--sta",), "ATL_COM_CLIENT_MODE_OK mode=sta apartment=1 activation=1 balance=1 changed=0"),
    ("openatl_com_client", ("--mta",), "ATL_COM_CLIENT_MODE_OK mode=mta apartment=1 activation=1 balance=1 changed=0"),
    ("openatl_com_client", ("--changed-sta",), "ATL_COM_CLIENT_MODE_OK mode=changed-sta apartment=1 activation=1 balance=1 changed=1"),
    ("openatl_com_client", ("--changed-mta",), "ATL_COM_CLIENT_MODE_OK mode=changed-mta apartment=1 activation=1 balance=1 changed=1"),
    ("openatl_com_client", ("--uninitialized",), "ATL_COM_CLIENT_MODE_OK mode=uninitialized apartment=0 activation_denied=1"),
)

def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/wtl10/native_arm64.py ARTIFACT_DIR")
    if os.name != "nt" or platform.machine().lower() not in ("arm64", "aarch64"):
        raise SystemExit("Refuse non-native Windows ARM64 acceptance")
    root = Path(sys.argv[1]).resolve(strict=True)
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8-sig"))
    required = {name + ".exe" for name in TARGETS} | {
        "openatl_unload_fixture.dll", "openatl_unload_probe.dll"}
    if set(manifest) != required:
        raise SystemExit(f"ARM64 provenance set mismatch: missing={required-set(manifest)}, unexpected={set(manifest)-required}")
    pe_validator = Path(__file__).with_name("verify_pe_machine.py")
    for filename in sorted(required):
        executable = root / filename
        actual = hashlib.sha256(executable.read_bytes()).hexdigest()
        if actual != manifest[filename].lower():
            raise SystemExit(f"ARM64 SHA256 mismatch for {filename}")
        arguments = [sys.executable, str(pe_validator), str(executable), "aarch64"]
        if filename in ("openatl_gui_runtime.exe", "openatl_wtl10_sdi.exe"):
            arguments.append("--require-resources")
        subprocess.run(arguments, check=True, timeout=20)
    sdi_ui = Path(__file__).with_name("sdi_interactive.py")
    for attempt in range(1, 6):
        for name, args, expected in CASES:
            proc = subprocess.run([str(root / (name + ".exe")), *args],
                                  capture_output=True, text=True,
                                  errors="replace", timeout=20, check=False)
            last_line = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""
            if proc.returncode != 0 or proc.stderr.strip() or last_line != expected:
                raise SystemExit(
                    f"Native ARM64 {name} {args} repeat {attempt} "
                    f"exit={proc.returncode} stdout={proc.stdout!r} "
                    f"stderr={proc.stderr!r} expected={expected!r}")
        sdi = subprocess.run([sys.executable, str(sdi_ui),
                              str(root / "openatl_wtl10_sdi.exe")],
                             capture_output=True, text=True,
                             errors="replace", timeout=45, check=False)
        if (sdi.returncode != 0 or sdi.stderr.strip() or
                sdi.stdout.strip() !=
                "OPENATL_SDI_UI_OK menu=1 controls=1 click=1 edit=1 about=1 options=1 exit=1"):
            raise SystemExit(
                f"Native ARM64 real SDI UI repeat {attempt} "
                f"exit={sdi.returncode} stdout={sdi.stdout!r} "
                f"stderr={sdi.stderr!r}")
    print("OPENATL_NATIVE_ARM64_OK files=14 tests=17 repeats=5 "
          "sha256=1 pe=1 resources=1")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
