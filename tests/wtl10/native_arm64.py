#!/usr/bin/env python3
"""Native ARM64 CTest-equivalent for PE artifacts produced on x64 Windows.

This is an acceptance test, not a build/bootstrap script. It checks a
SHA256 provenance manifest, genuine Windows ARM64 execution, resources,
and each existing GUI/no-COM, ATL utility, W^X, and COM-client mode.
The pinned WTL10 SDI is PE-verified and exercised through real Windows menu/dialog, button and Unicode edit-change events.
"""
import hashlib
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
from manifest_contract import current_checkout_sha, read_manifest, verify_manifest

TARGETS = (
    "openatl_gui_runtime",
    "openatl_atl_core",
    "openatl_atl_path",
    "openatl_unicode",
    "openatl_unicode_utf8",
    "openatl_atl_extensions",
    "openatl_atl_mapping",
    "openatl_atl_image",
    "openatl_thunk_fault",
    "openatl_mitigation_runtime",
    "openatl_unload_runner",
    "openatl_unload_native",
    "openatl_com_client",
    "openatl_com_site",
    "openatl_wtl_unmodified_headers",
    "openatl_com_server_tests",
    "openatl_activex",
    "openatl_wtl10_sdi",
)

CASES = (
    ("openatl_gui_runtime", (), "WTL10 GUI runtime PASS: no COM apartment, W^X thunk, message maps, resource dialog, NCDESTROY, OnFinalMessage, MRU"),
    ("openatl_activex", ("events",), "ACTIVEX_OK"),
    ("openatl_activex", ("browser",), "ACTIVEX_OK"),
    ("openatl_activex", ("lifecycle",), "ACTIVEX_OK"),
    ("openatl_atl_core", (), "ATL_CORE_OK file=1 string=1 sync=1 registry=1 no_com=1"),
    ("openatl_atl_path", (), "ATL_PATH_OK unicode=1 file=1 relative=1 root=1"),
    ("openatl_unicode", (), "OPENATL_UNICODE_OK utf8=1 utf16=1 acp=1 ownership=1 win32=1"),
    ("openatl_unicode_utf8", ("--utf8-acp",), "OPENATL_UNICODE_OK utf8=1 utf16=1 acp=1 ownership=1 win32=1"),
    ("openatl_atl_extensions", (), "ATL_EXTENSIONS_OK heap=1 filetime=1 array=1 list=1 no_com=1"),
    ("openatl_atl_mapping", (), "ATL_MAPPING_OK file=1 offset=1 copy=1 shared=1 cleanup=1 no_com=1"),
    ("openatl_atl_image", (), "ATL_IMAGE_OK dib=1 attach=1 draw=1 orientation=1 codec=1 stream=1 shortwrite=1 shortread=1 budget=1 alpha=1 cleanup=1 no_com=1"),
    ("openatl_thunk_fault", (), "THUNK_FAULT_OK alloc=1 rx=1 flush=1 reinit_rw=1 free=1 recovery=1 no_rwx=1 window_denied=1 dialog_denied=1 subclass_denied=1"),
    ("openatl_mitigation_runtime", (), "OPENATL_MITIGATION_OK cfg=1 rx=1 indirect=1 deny=1"),
    ("openatl_unload_runner", (), "OPENATL_UNLOAD_OK rounds=12 events=1 lease=1 held=1 blocked=1 callers_joined=1 caller_leases=1 lease_bounds=1 thunk_freed=1 class_unregistered=1 dll_unmapped=1 no_com=1"),
    ("openatl_unload_native", (), "OPENATL_UNLOAD_OK cycles=8 blocked=1 busy=1 retire=1 free=1 no_com=1"),
    ("openatl_com_server_tests", ("--module",), "ATL_MODULE_OK locks=1 termination=1 com_balance=1 class_objects=1 loop=1"),
    ("openatl_com_server_tests", ("--core",), "OPENATL_COM_SERVER_CORE_OK identity=1 factory=1 locks=1 concurrent=1 rollback=1 registry=1 no_com=1"),
    ("openatl_com_server_tests", ("--sta",), "OPENATL_COM_SERVER_NATIVE_OK mode=sta activation=1 identity=1 locks=1 unregister=1 unload=1"),
    ("openatl_com_server_tests", ("--mta",), "OPENATL_COM_SERVER_NATIVE_OK mode=mta activation=1 identity=1 locks=1 unregister=1 unload=1"),
    ("openatl_com_client", (), "ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0"),
    ("openatl_com_site", (), "OPENATL_SITE_OK set=1 get=1 errors=1 self=1 release=1 no_com=1"),
    ("openatl_wtl_unmodified_headers", (),
     "OPENATL_WTL_ORIGINAL_MAP_OK factory=1 activate=1 cookies=1 rollback=1 thread=1 no_com=1"),
    ("openatl_wtl_unmodified_headers", ("--hyperlink",),
     "OPENATL_WTL_HYPERLINK_OK registry=1 original=1 colors=1 no_com=1 cleanup=1"),
    ("openatl_wtl_unmodified_headers", ("--start-monitor-process",),
     "OPENATL_WTL_START_MONITOR_PROCESS_OK quit=1 original=1 join=UNQUALIFIED"),
    ("openatl_com_client", ("--sta",), "ATL_COM_CLIENT_MODE_OK mode=sta apartment=1 activation=1 balance=1 changed=0"),
    ("openatl_com_client", ("--mta",), "ATL_COM_CLIENT_MODE_OK mode=mta apartment=1 activation=1 balance=1 changed=0"),
    ("openatl_com_client", ("--changed-sta",), "ATL_COM_CLIENT_MODE_OK mode=changed-sta apartment=1 activation=1 balance=1 changed=1"),
    ("openatl_com_client", ("--changed-mta",), "ATL_COM_CLIENT_MODE_OK mode=changed-mta apartment=1 activation=1 balance=1 changed=1"),
    ("openatl_com_client", ("--uninitialized",), "ATL_COM_CLIENT_MODE_OK mode=uninitialized apartment=0 activation_denied=1"),
)

# The browser test itself retains all three genuine IWebBrowser2 activation /
# navigation / retirement cycles. Permit ONLY its exact structured diagnostics;
# all other native tests must continue to have completely empty stderr.
_BROWSER_PHASES = (
    "begin", "window-created", "creating-webbrowser-control",
    "webbrowser-created", "navigating", "awaiting-document-complete",
    "document-complete", "destroying-window", "window-destroyed", "end",
)
_BROWSER_LINE = re.compile(
    r"^ACTIVEX_BROWSER cycle=(-?\d+) phase=([a-z-]+) ticks=(\d+)$"
)

def native_stderr_ok(name: str, args: tuple, stderr: str) -> bool:
    if name != "openatl_activex" or args != ("browser",):
        return not stderr.strip()
    expected = [(-1, "start")]
    for cycle in range(3):
        expected.extend((cycle, phase) for phase in _BROWSER_PHASES)
    expected.append((-1, "complete"))
    lines = stderr.strip().splitlines()
    if len(lines) != len(expected):
        return False
    previous_ticks = -1
    for raw, (cycle, phase) in zip(lines, expected):
        matched = _BROWSER_LINE.fullmatch(raw)
        if not matched or (int(matched.group(1)), matched.group(2)) != (cycle, phase):
            return False
        ticks = int(matched.group(3))
        if ticks < previous_ticks:
            return False
        previous_ticks = ticks
    return True

def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in ("llvm", "msvc"):
        raise SystemExit("Usage: python tests/wtl10/native_arm64.py ARTIFACT_DIR llvm|msvc")
    if os.name != "nt" or platform.machine().lower() not in ("arm64", "aarch64"):
        raise SystemExit("Refuse non-native Windows ARM64 acceptance")
    root = Path(sys.argv[1]).resolve(strict=True)
    original = root.name.endswith("-original")
    try:
        manifest = read_manifest(root / "manifest.json")
    except (OSError, ValueError) as error:
        raise SystemExit(f"Invalid ARM64 manifest: {error}") from error
    samples = ("guidgen", "memdlg") if sys.argv[2] == "msvc" else ("guidgen",)
    targets = (*TARGETS, *("openatl_official_" + name for name in samples))
    required = {name + ".exe" for name in targets} | {
        "openatl_unload_fixture.dll", "openatl_unload_probe.dll", "openatl_com_server_fixture.dll"}
    try:
        manifest_files = verify_manifest(manifest, required, sys.argv[2],
            current_checkout_sha() if original else None, original=original)
    except ValueError as error:
        raise SystemExit(f"Invalid ARM64 provenance: {error}") from error
    pe_validator = Path(__file__).with_name("verify_pe_machine.py")
    for filename in sorted(required):
        executable = root / filename
        actual = hashlib.sha256(executable.read_bytes()).hexdigest()
        if actual != manifest_files[filename]:
            raise SystemExit(f"ARM64 SHA256 mismatch for {filename}")
        arguments = [sys.executable, str(pe_validator), str(executable), "aarch64"]
        if filename in ("openatl_gui_runtime.exe", "openatl_unicode_utf8.exe", "openatl_wtl10_sdi.exe") or filename.startswith("openatl_official_"):
            arguments.append("--require-resources")
        subprocess.run(arguments, check=True, timeout=20)
    sdi_ui = Path(__file__).with_name("sdi_interactive.py")
    for attempt in range(1, 6):
        for name, args, expected in CASES:
            proc = subprocess.run([str(root / (name + ".exe")), *args],
                                  capture_output=True, text=True,
                                  errors="replace", timeout=20, check=False)
            last_line = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""
            if (proc.returncode != 0 or not native_stderr_ok(name, args, proc.stderr)
                    or last_line != expected):
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
        official_ui = Path(__file__).parents[1] / "official_samples" / "wtl_interactive.py"
        for sample in samples:
            official = subprocess.run([sys.executable, str(official_ui), sample,
                str(root / ("openatl_official_" + sample + ".exe"))],
                capture_output=True, text=True, errors="replace", timeout=35, check=False)
            expected = f"OFFICIAL_WTL_OK sample={sample} behavior=1 resources=1 clean_exit=1"
            if official.returncode != 0 or official.stderr.strip() or official.stdout.strip() != expected:
                raise SystemExit(f"Native ARM64 official {sample} repeat {attempt} "
                    f"exit={official.returncode} stdout={official.stdout!r} stderr={official.stderr!r}")
    print(f"OPENATL_NATIVE_ARM64_OK files={len(required)} tests={len(CASES) + 1 + len(samples)} repeats=5 "
          f"sha256=1 pe=1 resources=1 original={int(original)} "
          f"checkout={int(original)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
