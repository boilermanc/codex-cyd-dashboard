from __future__ import annotations

import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

from scripts.package_release import create_deterministic_zip, normalize_version, write_checksums


class PackageReleaseTest(unittest.TestCase):
    def test_normalize_version(self) -> None:
        self.assertEqual(normalize_version("v0.1.0"), ("0.1.0", "v0.1.0"))
        self.assertEqual(normalize_version("2.3.4"), ("2.3.4", "v2.3.4"))
        with self.assertRaises(ValueError):
            normalize_version("v1")
        with self.assertRaises(ValueError):
            normalize_version("v1.2.3/asset")
        with self.assertRaises(ValueError):
            normalize_version("v01.2.3")

    def test_zip_is_deterministic_and_sorted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.bin"
            source.write_bytes(b"firmware")
            first = root / "first.zip"
            second = root / "second.zip"
            files = {"z.bin": source, "a.bin": source}
            texts = {"manifest.json": "{}\n", "FLASHING.md": "flash\n"}
            create_deterministic_zip(first, files, texts)
            create_deterministic_zip(second, files, texts)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(
                    archive.namelist(),
                    ["a.bin", "z.bin", "FLASHING.md", "manifest.json"],
                )

    def test_checksums_are_sorted_and_correct(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            beta = root / "beta.bin"
            alpha = root / "alpha.bin"
            beta.write_bytes(b"beta")
            alpha.write_bytes(b"alpha")
            output = root / "SHA256SUMS.txt"
            write_checksums(output, [beta, alpha])
            self.assertEqual(
                output.read_text(encoding="utf-8").splitlines(),
                [
                    f"{hashlib.sha256(b'alpha').hexdigest()}  alpha.bin",
                    f"{hashlib.sha256(b'beta').hexdigest()}  beta.bin",
                ],
            )


if __name__ == "__main__":
    unittest.main()
