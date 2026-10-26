#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Portions of original/patched code excerpts below originate from:
# ReactOS ATL statreg.h, Copyright 2005 Jacek Caban,
# Copyright 2009 Andrew Hill. See RATL COPYING.LIB and upstream source:
# https://github.com/katahiromz/RATL/blob/2e2efa182d7c6ee9037454d23780991fb8543084/include/atl/statreg.h
"""Strictly test-only RATL P1-D recursive deletion patch for isolated checkout.

Never modifies pinned RATL source, vendored OpenATL, or installed SDK.
Retain LGPL/GPL license boundaries; publish original/patch test results separately.
"""
from pathlib import Path
import subprocess
import sys

PIN = "2e2efa182d7c6ee9037454d23780991fb8543084"

OLD = """        while (RegEnumKeyExW(parentKey, 0, szBuffer, &dwSize, NULL, NULL, NULL, &time) == ERROR_SUCCESS)
        {
            lRes = RegDeleteTreeX(childKey, szBuffer);
            if (lRes != ERROR_SUCCESS)
                return lRes;
            dwSize = sizeof(szBuffer) / sizeof(szBuffer[0]);
        }
        RegCloseKey(childKey);
        return RegDeleteKeyW(parentKey, subKeyName);"""
NEW = """        // Enumerate the opened child, not the parent. The old code
        // repeatedly selected the parent's first key and failed to
        // traverse nested descendants (or incorrectly returned early).
        while (RegEnumKeyExW(childKey, 0, szBuffer, &dwSize, NULL, NULL, NULL, &time) == ERROR_SUCCESS)
        {
            lRes = RegDeleteTreeX(childKey, szBuffer);
            if (lRes != ERROR_SUCCESS)
            {
                RegCloseKey(childKey); // no handle leak on recursive failure
                return lRes;
            }
            dwSize = sizeof(szBuffer) / sizeof(szBuffer[0]);
        }
        RegCloseKey(childKey);
        return RegDeleteKeyW(parentKey, subKeyName);"""


# The original implementation ignores the recursive-delete result at both
# script call sites. A deterministic access-denied injection must propagate
# and must never overwrite the existing subtree on ForceRemove.
DELETE_CALL_OLD = """                else if (key_type == DO_DELETE)
                {
                    RegDeleteTreeX(parent_key, buf->str);
                }"""
DELETE_CALL_NEW = """                else if (key_type == DO_DELETE)
                {
                    lres = RegDeleteTreeX(parent_key, buf->str);
                    if (lres != ERROR_SUCCESS && lres != ERROR_FILE_NOT_FOUND)
                    {
                        hres = HRESULT_FROM_WIN32(lres);
                        break;
                    }
                }"""
FORCE_CALL_OLD = """                    if (key_type == FORCE_REMOVE)
                        RegDeleteTreeX(parent_key, buf->str);
                    lres = RegCreateKeyW(parent_key, buf->str, &hkey);"""
FORCE_CALL_NEW = """                    if (key_type == FORCE_REMOVE)
                    {
                        lres = RegDeleteTreeX(parent_key, buf->str);
                        if (lres != ERROR_SUCCESS && lres != ERROR_FILE_NOT_FOUND)
                        {
                            hres = HRESULT_FROM_WIN32(lres);
                            break;
                        }
                    }
                    lres = RegCreateKeyW(parent_key, buf->str, &hkey);"""

INJECT_OLD = """        ATLASSERT(parentKey != NULL);
        lRes = RegOpenKeyExW(parentKey, subKeyName, 0, KEY_READ | KEY_WRITE, &childKey);"""
INJECT_NEW = """        ATLASSERT(parentKey != NULL);
#if defined(RATL_P1D_TEST_DENY_DELETE)
        // Fault injection is compiled only into a separately named P1-D
        // diagnostic PE, NEVER into upstream or OpenATL.
        if (lstrcmpW(subKeyName, L"RATL_P1D_DENIED") == 0)
            return ERROR_ACCESS_DENIED;
#endif
        lRes = RegOpenKeyExW(parentKey, subKeyName, 0, KEY_READ | KEY_WRITE, &childKey);"""


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: ratl-rgs-nested-test-patch.py TEMP_RATL_CHECKOUT")
    root=Path(sys.argv[1]).resolve(strict=True)
    sha=subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"],text=True).strip()
    if sha != PIN:
        raise SystemExit(f"Refuse non-pinned RATL checkout: {sha}")
    header=root/"include"/"atl"/"statreg.h"
    contents=header.read_text(encoding="utf-8")
    for old, new in (
        (OLD, NEW),
        (DELETE_CALL_OLD, DELETE_CALL_NEW),
        (FORCE_CALL_OLD, FORCE_CALL_NEW),
        (INJECT_OLD, INJECT_NEW),
    ):
        if contents.count(old) != 1:
            raise SystemExit("Refuse unexpected RATL source: "+old[:65])
        contents=contents.replace(old,new)
    header.write_text(contents,encoding="utf-8")
    print("Test-only RATL nested delete, HRESULT propagation, fault hook applied")


if __name__=="__main__":
    main()
