#!/usr/bin/env python3
"""Native behavior of the unchanged, externally sourced RATL samples."""
import ctypes
from ctypes import wintypes as w
from pathlib import Path
import os
import subprocess
import sys
import time
import winreg


class Settings:
    def __init__(self, sample):
        self.sample = sample
        self.path = r"Software\ReactOS\Atl" + sample.title() + "App"
        self.values = ("x", "y", "cx", "cy") if sample == "window" else ()

    def __enter__(self):
        self.created, self.saved = [], {}
        parts = self.path.split("\\")
        for count in range(1, len(parts) + 1):
            path = "\\".join(parts[:count])
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path):
                    pass
            except FileNotFoundError:
                self.created.append(path)
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, self.path, 0, winreg.KEY_ALL_ACCESS) as key:
            for name, value in zip(self.values, (40, 60, 640, 480)):
                try:
                    self.saved[name] = winreg.QueryValueEx(key, name)
                except FileNotFoundError:
                    self.saved[name] = None
                winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, value)
        return self

    def persisted_geometry(self):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.path) as key:
            return tuple(winreg.QueryValueEx(key, name) for name in self.values)

    def __exit__(self, *_):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.path, 0, winreg.KEY_ALL_ACCESS) as key:
            for name, saved in self.saved.items():
                if saved is None:
                    try:
                        winreg.DeleteValue(key, name)
                    except FileNotFoundError:
                        pass
                else:
                    value, kind = saved
                    winreg.SetValueEx(key, name, 0, kind, value)
        for path in reversed(self.created):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                children, values, _ = winreg.QueryInfoKey(key)
            if not children and not values:
                winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)


def gui(sample, executable, close_button=2):
    u = ctypes.WinDLL("user32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    u.EnumWindows.argtypes = [callback, w.LPARAM]
    u.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    u.IsWindowVisible.argtypes = [w.HWND]
    u.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
    u.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
    u.GetDlgItem.restype = w.HWND
    u.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                               ctypes.c_int, ctypes.c_int, w.UINT]
    u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM,
                                    w.UINT, w.UINT, ctypes.POINTER(ctypes.c_size_t)]
    u.SendMessageTimeoutW.restype = ctypes.c_size_t
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 4
    process = subprocess.Popen([str(executable)], cwd=executable.parent, startupinfo=startup)
    deadline = time.monotonic() + 20
    stage = "launch"
    try:
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

        def main_window():
            found = []
            def visit(hwnd, _):
                pid = w.DWORD()
                u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == process.pid and u.IsWindowVisible(hwnd):
                    found.append(hwnd)
                return True
            visitor = callback(visit)
            if not u.EnumWindows(visitor, 0):
                raise RuntimeError("EnumWindows failed")
            return next(iter(found), None)

        def message(hwnd, code, wp=0, lp=0):
            result = ctypes.c_size_t()
            if not u.SendMessageTimeoutW(hwnd, code, wp, lp, 2, 1500, ctypes.byref(result)):
                raise RuntimeError(f"message timeout {code}")
            return result.value

        def rectangle(hwnd):
            rect = w.RECT()
            if not u.GetWindowRect(hwnd, ctypes.byref(rect)):
                raise RuntimeError("GetWindowRect failed")
            return rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top

        main = until("visible application", main_window)
        if sample == "dialog":
            until("dialog resource controls", lambda: u.GetDlgItem(main, 1) and u.GetDlgItem(main, 2))
            if not message(main, 0x007F, 1):  # WM_GETICON ICON_BIG
                raise RuntimeError("missing dialog icon resource")
            message(u.GetDlgItem(main, close_button), 0x00F5)
        else:
            until("load saved geometry", lambda: rectangle(main) == (40, 60, 640, 480))
            if not u.SetWindowPos(main, None, 80, 100, 720, 480, 0x0014):
                raise RuntimeError("SetWindowPos failed")
            until("resize/move handlers", lambda: rectangle(main) == (80, 100, 720, 480))
            if not u.PostMessageW(main, 0x0010, 0, 0):  # WM_CLOSE
                raise RuntimeError("WM_CLOSE failed")
        stage = "normal exit"
        if process.wait(timeout=4) != 0:
            raise RuntimeError(f"exit {process.returncode}")
    except Exception as error:
        raise RuntimeError(f"{sample}: {stage}: {error}") from error
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=4)


def main():
    if os.name != "nt" or len(sys.argv) != 3 or sys.argv[1] not in ("console", "dialog", "window"):
        raise RuntimeError("Requires Windows: behavior.py console|dialog|window EXE")
    sample, executable = sys.argv[1], Path(sys.argv[2]).resolve(strict=True)
    if sample == "console":
        result = subprocess.run([str(executable)], input="\n", capture_output=True,
                                text=True, timeout=10)
        expected = ["# = 3", "array[0] = 3", "array[1] = 4", "array[2] = 5", "Hit Enter key"]
        if result.returncode or result.stderr or result.stdout.splitlines() != expected:
            raise RuntimeError(f"console exit={result.returncode} output={result.stdout!r} error={result.stderr!r}")
    else:
        with Settings(sample) as settings:
            gui(sample, executable)
            if sample == "window" and settings.persisted_geometry() != tuple(
                    (value, winreg.REG_DWORD) for value in (80, 100, 720, 480)):
                raise RuntimeError("window geometry not persisted through CRegKey::QueryValue/SetDWORDValue")
            if sample == "dialog":
                gui(sample, executable, close_button=1)
    print(f"RATL_SAMPLE_OK sample={sample} behavior=1 clean_exit=1")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"RATL_SAMPLE_FAIL {error}", file=sys.stderr)
        sys.exit(1)
