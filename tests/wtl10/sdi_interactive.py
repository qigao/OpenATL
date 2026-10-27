#!/usr/bin/env python3
"""Real Windows SDI menu/dialog/control admission on a separate GUI process.

This launches the actual pinned WTL10 SDI executable, not the hidden ATL
fixture. The test drives real HWND messages and checks observable controls
rather than equating a posted command or successful PE link with admission.
Only the spawned process is eligible for shutdown/termination.
"""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import sys
import time

WM_COMMAND = 0x0111
BM_CLICK = 0x00F5
ID_FILE_NEW = 0xE100
ID_APP_EXIT = 0xE141
ID_APP_ABOUT = 0xE140
ID_VIEW_CONTROLS = 32772
ID_TOOLS_OPTIONS = 32771
IDC_BUTTON1 = 1030
IDC_EDIT1 = 1003
IDC_TREEVIEW = 1001
IDC_LISTVIEW = 1002
IDCANCEL = 2
IDOK = 1
MAX_SECONDS = 24


def main() -> int:
    if os.name != "nt" or len(sys.argv) != 2:
        raise SystemExit("Requires Windows: sdi_interactive.py ACTUAL_SDI_EXE")
    executable = Path(sys.argv[1]).resolve(strict=True)
    if executable.suffix.lower() != ".exe":
        raise SystemExit("Refuse non-executable SDI target")
    u = ctypes.WinDLL("user32", use_last_error=True)

    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    u.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    u.EnumWindows.restype = wintypes.BOOL
    u.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    u.GetWindowThreadProcessId.restype = wintypes.DWORD
    u.IsWindowVisible.argtypes = [wintypes.HWND]
    u.IsWindowVisible.restype = wintypes.BOOL
    u.IsWindow.argtypes = [wintypes.HWND]
    u.IsWindow.restype = wintypes.BOOL
    u.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    u.GetWindowTextW.restype = ctypes.c_int
    u.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
    u.SetWindowTextW.restype = wintypes.BOOL
    u.GetMenu.argtypes = [wintypes.HWND]
    u.GetMenu.restype = wintypes.HMENU
    u.GetMenuItemCount.argtypes = [wintypes.HMENU]
    u.GetMenuItemCount.restype = ctypes.c_int
    u.GetMenuItemID.argtypes = [wintypes.HMENU, ctypes.c_int]
    u.GetMenuItemID.restype = wintypes.UINT
    u.GetSubMenu.argtypes = [wintypes.HMENU, ctypes.c_int]
    u.GetSubMenu.restype = wintypes.HMENU
    u.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
    u.GetDlgItem.restype = wintypes.HWND
    u.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    u.PostMessageW.restype = wintypes.BOOL
    u.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT,
                                      wintypes.WPARAM, wintypes.LPARAM,
                                      wintypes.UINT, wintypes.UINT,
                                      ctypes.POINTER(ctypes.c_size_t)]
    u.SendMessageTimeoutW.restype = ctypes.c_size_t

    def caption(hwnd):
        buf = ctypes.create_unicode_buffer(512)
        u.GetWindowTextW(hwnd, buf, len(buf))
        return buf.value

    def windows(pid):
        found = []
        def visit(hwnd, _):
            actual = wintypes.DWORD()
            u.GetWindowThreadProcessId(hwnd, ctypes.byref(actual))
            if actual.value == pid and u.IsWindowVisible(hwnd):
                found.append((hwnd, caption(hwnd)))
            return True
        cb = callback(visit)
        if not u.EnumWindows(cb, 0):
            raise RuntimeError("EnumWindows failed")
        return found

    def has_menu_command(menu, command, depth=0):
        if not menu or depth > 8:
            return False
        count = u.GetMenuItemCount(menu)
        if count < 0:
            return False
        for n in range(count):
            if u.GetMenuItemID(menu, n) == command:
                return True
            if has_menu_command(u.GetSubMenu(menu, n), command, depth + 1):
                return True
        return False

    process = subprocess.Popen([str(executable)], cwd=str(executable.parent),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stage = "launch"
    try:
        deadline = time.monotonic() + MAX_SECONDS

        def until(what, predicate):
            nonlocal stage
            stage = what
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"SDI terminated before {what}: exit={process.returncode}")
                result = predicate()
                if result:
                    return result
                time.sleep(0.05)
            raise RuntimeError(f"timeout waiting for {what}: {windows(process.pid)!r}")

        def post(hwnd, cmd):
            if not u.PostMessageW(hwnd, WM_COMMAND, cmd, 0):
                raise RuntimeError(f"PostMessageW({cmd}) failed: {ctypes.get_last_error()}")

        frame = until("visible SDI frame",
                      lambda: next((h for h, title in windows(process.pid)
                                    if title.startswith("OpenATL + WTL Test Application")), None))
        menu = u.GetMenu(frame)
        if not menu or not all(has_menu_command(menu, command) for command in
                                (ID_FILE_NEW, ID_APP_EXIT, ID_APP_ABOUT,
                                 ID_VIEW_CONTROLS, ID_TOOLS_OPTIONS)):
            raise RuntimeError("SDI native menu missing required commands")

        post(frame, ID_FILE_NEW)
        if not u.IsWindow(frame):
            raise RuntimeError("SDI disappeared on File/New")

        post(frame, ID_APP_ABOUT)
        about = until("native About dialog", lambda: next(
            (h for h, title in windows(process.pid)
             if title.startswith("About OpenATL + WTL Test")), None))
        if not u.GetDlgItem(about, IDOK):
            raise RuntimeError("About dialog OK button missing")
        post(about, IDOK)
        until("About dialog closed", lambda: not u.IsWindow(about))

        post(frame, ID_VIEW_CONTROLS)
        def controls_window():
            return next((h for h, name in windows(process.pid)
                         if "All WTL Controls Showcase" in name), None)
        controls = until("native All Controls dialog", controls_window)
        if not all(u.GetDlgItem(controls, control) for control in
                   (IDC_BUTTON1, IDC_EDIT1, IDC_TREEVIEW, IDC_LISTVIEW)):
            raise RuntimeError("Actual WTL controls were not created")

        button = u.GetDlgItem(controls, IDC_BUTTON1)
        returned = ctypes.c_size_t()
        if not u.SendMessageTimeoutW(button, BM_CLICK, 0, 0, 0x0002, 2500,
                                     ctypes.byref(returned)):
            raise RuntimeError(f"native BM_CLICK timed out: {ctypes.get_last_error()}")
        until("native click handler changed button label",
              lambda: caption(button) == "Push (1)")

        # Exercise actual cross-process WM_SETTEXT + EN_CHANGE forwarding
        # through the original WTL10 edit control and dialog message map.
        # Unlike posting a command ID, this proves both text marshaling and
        # the visible mutation produced by the real input event.
        edit = u.GetDlgItem(controls, IDC_EDIT1)
        new_text = "OpenATL-東京"
        if not edit or not u.SetWindowTextW(edit, new_text):
            raise RuntimeError(
                f"native edit WM_SETTEXT failed: {ctypes.get_last_error()}")
        until("edit change propagated to real dialog title",
              lambda: caption(edit) == new_text and
                      caption(controls) ==
                      'All WTL Controls Showcase - "OpenATL-東京"')
        post(controls, IDCANCEL)
        until("controls dialog closed", lambda: not u.IsWindow(controls))

        post(frame, ID_TOOLS_OPTIONS)
        options = until("native Options property sheet", lambda: next(
            (h for h, title in windows(process.pid) if title == "Options"), None))
        post(options, IDCANCEL)
        until("Options property sheet closed", lambda: not u.IsWindow(options))
        post(frame, ID_APP_EXIT)
        stage = "SDI process exit"
        remaining = max(0.1, deadline - time.monotonic())
        exit_code = process.wait(timeout=remaining)
        # Official WTL10 CFrameWindowImplBase::OnDestroy explicitly calls
        # PostQuitMessage(1) for the main non-child frame. The message loop
        # returns WM_QUIT.wParam, so 1 is the pinned sample's normal exit.
        if exit_code != 1:
            raise RuntimeError(f"SDI exited with {exit_code}, expected WTL frame quit code 1")
        if any(h == frame for h, _ in windows(process.pid)):
            raise RuntimeError("SDI HWND survived process exit")
        print("OPENATL_SDI_UI_OK menu=1 controls=1 click=1 edit=1 about=1 options=1 exit=1")
        return 0
    except Exception as exc:
        print(f"OPENATL_SDI_UI_FAIL stage={stage} detail={exc}", file=sys.stderr)
        return 1
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        if process.stdout:
            process.stdout.close()
        if process.stderr:
            process.stderr.close()


if __name__ == "__main__":
    raise SystemExit(main())
