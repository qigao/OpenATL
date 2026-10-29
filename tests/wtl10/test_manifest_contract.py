#!/usr/bin/env python3
"""Pure-Python negative tests for original/legacy ARM64 artifact admission."""
from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import os
from unittest.mock import patch
import tempfile
import unittest

from native_arm64 import native_stdout_ok
from manifest_contract import (current_checkout_sha, make_original_manifest, read_manifest,
                               verify_manifest, verify_installed_wtl_stamp,
                               WTL_STAMP)


class ManifestContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.files = {"sample.exe": "a" * 64, "support.dll": "b" * 64}
        self.required = set(self.files)
        self.sha = "c" * 40
        self.original = make_original_manifest(self.files, "llvm", self.sha)

    def test_original_admits_only_matching_source_and_binaries(self) -> None:
        self.assertEqual(verify_manifest(self.original, self.required, "llvm",
                                         self.sha, original=True), self.files)
        self.assertEqual(verify_manifest(self.files, self.required, "llvm"),
                         self.files)

    def test_all_source_and_schema_mismatches_fail(self) -> None:
        for field, bad in (("checkout_sha", "d" * 40),
                           ("toolchain", "msvc"),
                           ("wtl_version", "10.0.10320#6"),
                           ("wtl_provider", "second-provider"),
                           ("wtl_archive_sha512", "0" * 128),
                           ("profile", "adapted"), ("schema", True)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                payload = deepcopy(self.original)
                payload["provenance"][field] = bad
                verify_manifest(payload, self.required, "llvm", self.sha,
                                original=True)

    def test_missing_extra_and_corrupt_binaries_fail(self) -> None:
        for change in (lambda p: p["files"].pop("sample.exe"),
                       lambda p: p["files"].update({"extra.exe": "f" * 64}),
                       lambda p: p["files"].update({"sample.exe": "a" * 63}),
                       lambda p: p["files"].update({"sample.exe": "A" * 64}),
                       lambda p: p.pop("provenance")):
            with self.assertRaises(ValueError):
                payload = deepcopy(self.original)
                change(payload)
                verify_manifest(payload, self.required, "llvm", self.sha,
                                original=True)

    def test_wrong_profile_and_toolchain_fail(self) -> None:
        with self.assertRaises(ValueError):
            verify_manifest(self.files, self.required, "llvm", self.sha,
                            original=True)
        with self.assertRaises(ValueError):
            verify_manifest(self.original, self.required, "llvm")
        with self.assertRaises(ValueError):
            verify_manifest(self.original, self.required, "msvc", self.sha,
                            original=True)

    def test_duplicate_json_keys_fail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text('{"sample.exe":"' + "a" * 64 +
                            '","sample.exe":"' + "b" * 64 + '"}', "utf-8")
            with self.assertRaises(ValueError):
                read_manifest(path)

    def test_checkout_matches_actual_git_head_not_synthetic_merge(self) -> None:
        with patch("manifest_contract.subprocess.check_output",
                   return_value=self.sha + chr(10)) as git:
            with patch.dict(os.environ, {
                "GITHUB_ACTIONS": "true",
                "GITHUB_SHA": "d" * 40,  # synthetic merge; must be ignored
                "OPENATL_EXPECTED_CHECKOUT_SHA": self.sha,
            }, clear=True):
                self.assertEqual(current_checkout_sha(), self.sha)
                git.assert_called_once()

    def test_checkout_rejects_mismatch_missing_and_bad_hash(self) -> None:
        with patch("manifest_contract.subprocess.check_output",
                   return_value=self.sha + chr(10)):
            for expected in ("d" * 40, "", "bad-sha"):
                with self.subTest(expected=expected):
                    with patch.dict(os.environ, {
                        "GITHUB_ACTIONS": "true",
                        "OPENATL_EXPECTED_CHECKOUT_SHA": expected,
                    }, clear=True), self.assertRaises(ValueError):
                        current_checkout_sha()

    def test_print_defaults_win32_and_wtl_parity(self) -> None:
        def stdout(default: int, raw: int, wtl: int, metadata: str) -> str:
            return (f"WTL_PRINTER_READINESS default={default} win32=0x{raw:08x} "
                    f"wtl=0x{wtl:08x} metadata={metadata} modal=UNQUALIFIED\n"
                    "OPENATL_WTL_PRINT_DEFAULTS_PARITY_OK "
                    "win32=1 cleanup=1 modal=UNQUALIFIED\n")
        name = "openatl_wtl_unmodified_headers"
        args = ("--print-defaults",)
        self.assertTrue(native_stdout_ok(name, args, stdout(1, 0, 0, "QUALIFIED")))
        # Matching errors qualify Win32 parity, never printer hardware/DC/UI.
        self.assertTrue(native_stdout_ok(
            name, args, stdout(1, 0x80070006, 0x80070006, "UNQUALIFIED")))
        self.assertTrue(native_stdout_ok(
            name, args, stdout(0, 0x80070002, 0x80070002, "UNQUALIFIED")))
        for case in (stdout(1, 0, 0x80070006, "UNQUALIFIED"),
                     stdout(1, 0x80070006, 0x80070002, "UNQUALIFIED"),
                     stdout(1, 0, 0, "UNQUALIFIED"),
                     stdout(1, 0x80070006, 0x80070006, "QUALIFIED"),
                     stdout(0, 0, 0, "QUALIFIED")):
            with self.subTest(case=case), self.assertFalse(
                native_stdout_ok(name, args, case))

    def test_real_package_stamp_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / "test-triplet" / "share" / "wtl"
            prefix.mkdir(parents=True)
            stamp = prefix / "openatl-wtl-source.txt"
            stamp.write_text(WTL_STAMP, encoding="utf-8")
            from unittest.mock import patch
            with patch.dict("os.environ", {
                "OPENATL_WTL_VCPKG_INSTALLED_DIR": directory,
                "OPENATL_WTL_VCPKG_TRIPLET": "test-triplet",
            }):
                verify_installed_wtl_stamp("")
                stamp.write_text(WTL_STAMP.replace("#7", "#6"), encoding="utf-8")
                with self.assertRaises(ValueError):
                    verify_installed_wtl_stamp("")


if __name__ == "__main__":
    unittest.main()
