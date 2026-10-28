#!/usr/bin/env python3
"""Behavior tests for unchanged official WTL GuidGen and MemDlg applications."""
import ctypes
from ctypes import wintypes as w
from pathlib import Path
import os
import re
import subprocess
import sys
import time
import winreg


class GuidSettings:
    """Restore the sample's one setting and remove only newly empty keys."""
    key = r"Software\Microsoft\WTL Samples\GUIDGEN"

    def __enter__(self):
        self.created = []
        parts = self.key.split("\\")
        for index in range(1, len(parts) + 1):
            prefix = "\\".join(parts[:index])
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, prefix):
                    pass
            except FileNotFoundError:
                self.created.append(prefix)
        self.saved = None
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, self.key, 0, winreg.KEY_ALL_ACCESS) as key:
            try:
                self.saved = winreg.QueryValueEx(key, "GUID Type")
            except FileNotFoundError:
                pass
            winreg.SetValueEx(key, "GUID Type", 0, winreg.REG_DWORD, 3)
        return self

    def __exit__(self, *_):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.key, 0, winreg.KEY_ALL_ACCESS) as key:
            if self.saved is None:
                try:
                    winreg.DeleteValue(key, "GUID Type")
                except FileNotFoundError:
                    pass
            else:
                value, kind = self.saved
                winreg.SetValueEx(key, "GUID Type", 0, kind, value)
        for prefix in reversed(self.created):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, prefix) as key:
                children, values, _ = winreg.QueryInfoKey(key)
            if not children and not values:
                winreg.DeleteKey(winreg.HKEY_CURRENT_USER, prefix)


def run(sample, executable):
    u = ctypes.WinDLL("user32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    u.EnumWindows.argtypes = [callback, w.LPARAM]
    u.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
    u.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    u.IsWindowVisible.argtypes = [w.HWND]
    u.IsWindow.argtypes = [w.HWND]
    u.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
    u.GetDlgItem.restype = w.HWND
    u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM,
                                    w.UINT, w.UINT, ctypes.POINTER(ctypes.c_size_t)]
    u.SendMessageTimeoutW.restype = ctypes.c_size_t

    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 4  # SW_SHOWNOACTIVATE: do not activate the test frame.
    process = subprocess.Popen([str(executable)], cwd=executable.parent, startupinfo=startup)
    stage = "launch"
    deadline = time.monotonic() + 24
    try:
        def windows(parent=None):
            result = []
            def visit(hwnd, _):
                pid = w.DWORD()
                u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == process.pid and u.IsWindowVisible(hwnd):
                    result.append(hwnd)
                return True
            visitor = callback(visit)
            if parent is None:
                if not u.EnumWindows(visitor, 0):
                    raise RuntimeError("EnumWindows failed")
            else:
                u.EnumChildWindows(parent, visitor, 0)
            return result

        def until(label, predicate):
            nonlocal stage
            stage = label
            while time.monotonic() < deadline:
                result = predicate()
                if result:
                    return result
                if process.poll() is not None:
                    raise RuntimeError(f"early exit {process.returncode}")
                time.sleep(0.02)
            raise TimeoutError(label)

        def message(hwnd, msg, wp=0, lp=0):
            result = ctypes.c_size_t()
            if not u.SendMessageTimeoutW(hwnd, msg, wp, lp, 2, 1500, ctypes.byref(result)):
                raise RuntimeError(f"unresponsive window, msg={msg}")
            return result.value

        def text(hwnd):
            buffer = ctypes.create_unicode_buffer(1024)
            message(hwnd, 0x000D, len(buffer), ctypes.addressof(buffer))
            return buffer.value

        def command(hwnd, command_id):
            if not u.PostMessageW(hwnd, 0x0111, command_id, 0):
                raise RuntimeError("WM_COMMAND failed")

        def button(hwnd, control_id):
            control = u.GetDlgItem(hwnd, control_id)
            if not control:
                raise RuntimeError(f"missing control {control_id}")
            message(control, 0x00F5)  # BM_CLICK, real command notification.

        main = until("main window", lambda: next(iter(windows()), None))
        if sample == "guidgen":
            results = until("GUID output control", lambda: u.GetDlgItem(main, 1004))
            pattern = re.compile(r"\{[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}\}")
            original = until("registry GUID format", lambda: pattern.search(text(results)))
            button(main, 1005)
            generated = until("new GUID differs", lambda: (found := pattern.search(text(results))) and
                              found.group() != original.group() and found)
            for control, expected in ((1000, "IMPLEMENT_OLECREATE"), (1001, "DEFINE_GUID"),
                                      (1002, "static const GUID")):
                button(main, control)
                until(expected + " format", lambda: expected in text(results))
                if pattern.search(text(results)).group().upper() != generated.group().upper():
                    raise RuntimeError("format selection changed the GUID value")
            button(main, 1003)
            until("registry format restored", lambda: pattern.fullmatch(text(results).strip()))
            if not u.PostMessageW(main, 0x0112, 0x0010, 0):
                raise RuntimeError("about command failed")
        else:
            command(main, 0xE140)  # Resource-based About dialog.

        def about():
            for hwnd in windows():
                if hwnd == main:
                    continue
                name = ctypes.create_unicode_buffer(64)
                u.GetClassNameW(hwnd, name, len(name))
                if name.value == "#32770" and u.GetDlgItem(hwnd, 1):
                    return hwnd
            return None

        dialog = until("resource About", about)
        expected = "GUIDGEN Application" if sample == "guidgen" else "MemDlg Application"
        until("About text", lambda: any(expected in text(child) for child in windows(dialog)))
        button(dialog, 1)
        until("resource About closed", lambda: not u.IsWindow(dialog))
        if sample == "memdlg":
            command(main, 32775)
            dialog = until("memory-template About", about)
            until("memory-template text", lambda: any("MemDlg Application" in text(child)
                                                      for child in windows(dialog)))
            button(dialog, 1)
            until("memory-template About closed", lambda: not u.IsWindow(dialog))
            command(main, 0xE141)
        else:
            button(main, 2)  # Close (not Copy); leave the clipboard untouched.
        stage = "clean exit"
        code = process.wait(timeout=4)
        expected_exit = 2 if sample == "guidgen" else 1  # WTL frame WM_QUIT code.
        if code != expected_exit:
            raise RuntimeError(f"exit {code}, expected {expected_exit}")
        print(f"OFFICIAL_WTL_OK sample={sample} behavior=1 resources=1 clean_exit=1")
        return 0
    except Exception as error:
        print(f"OFFICIAL_WTL_FAIL sample={sample} stage={stage}: {error}", file=sys.stderr)
        return 1
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=4)


if __name__ == "__main__":
    if os.name != "nt" or len(sys.argv) != 3 or sys.argv[1] not in ("guidgen", "memdlg"):
        raise SystemExit("Requires Windows: wtl_interactive.py guidgen|memdlg EXE")
    executable = Path(sys.argv[2]).resolve(strict=True)
    if sys.argv[1] == "guidgen":
        with GuidSettings():
            sys.exit(run(sys.argv[1], executable))
    else:
        sys.exit(run(sys.argv[1], executable))
