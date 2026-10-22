#!/usr/bin/env python3
"""Offline regression for numeric CMake vs public C++ prerelease version identity."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def validate(cmake: str, header: str, consumer: str) -> str:
    package = re.search(
        r"\bproject\(\s*libaudition\s+VERSION\s+(\d+)\.(\d+)\.(\d+)\b",
        cmake,
    )
    public = re.search(
        r'\bset\(LIBAUDITION_PUBLIC_VERSION "\$\{PROJECT_VERSION\}([^"]*)"\)',
        cmake,
    )
    if not package or not public:
        raise AssertionError("Missing numeric or public version declaration")
    parts = tuple(int(value) for value in package.groups())
    expected = ".".join(map(str, parts)) + public.group(1)
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", expected):
        raise AssertionError("Invalid public version")
    for field, component in zip(("Major", "Minor", "Patch"), parts):
        match = re.search(r"\bkVersion" + field + r"\s*=\s*(\d+)", header)
        if not match or int(match.group(1)) != component:
            raise AssertionError("Public numeric component mismatch: " + field)
    match = re.search(r'\bkVersion\s*=\s*"([^"]+)"', header)
    if not match or match.group(1) != expected:
        raise AssertionError("Public prerelease identity mismatch")
    if cmake.count('--public-version "${LIBAUDITION_PUBLIC_VERSION}"') != 2:
        raise AssertionError("Core and Sherpa installed consumers must check public version")
    if 'args.public_version or args.version' not in consumer:
        raise AssertionError("Installed consumer must check public version separately")
    return expected


class ReleaseVersionContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.header = (ROOT / "include/audition/core/version.hpp").read_text(encoding="utf-8")
        cls.consumer = (ROOT / "tests/integration/verify_installed_package.py").read_text(encoding="utf-8")

    def test_current_package_and_public_version(self) -> None:
        self.assertEqual(validate(self.cmake, self.header, self.consumer), "1.0.0-rc.1")

    def test_changed_public_version_is_rejected(self) -> None:
        with self.assertRaisesRegex(AssertionError, "prerelease identity"):
            validate(self.cmake, self.header.replace("1.0.0-rc.1", "1.0.0"), self.consumer)

    def test_changed_numeric_version_is_rejected(self) -> None:
        with self.assertRaisesRegex(AssertionError, "numeric component"):
            validate(self.cmake.replace("VERSION 1.0.0", "VERSION 1.0.1"), self.header, self.consumer)

    def test_missing_consumer_wiring_is_rejected(self) -> None:
        with self.assertRaisesRegex(AssertionError, "Core and Sherpa"):
            validate(self.cmake.replace('--public-version "${LIBAUDITION_PUBLIC_VERSION}"', "", 1),
                     self.header, self.consumer)


if __name__ == "__main__":
    unittest.main()
