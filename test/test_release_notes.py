from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from release_notes import changelog_section, release_body  # noqa: E402


class ReleaseNotesTests(unittest.TestCase):
    def test_extracts_only_the_requested_version(self) -> None:
        changelog = "## [Unreleased]\n\n## [0.4.1] - 2026-09-23\n\n### Fixed\n\n- 修正\n\n## [0.4.0] - 2026-09-14\n- 旧版\n"
        section = changelog_section(changelog, "v0.4.1")
        self.assertIn("- 修正", section)
        self.assertNotIn("旧版", section)

    def test_release_body_includes_notes_and_changelog(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "docs" / "releases").mkdir(parents=True)
            (root / "docs" / "releases" / "v0.4.1.md").write_text("# v0.4.1\n説明\n", encoding="utf-8")
            (root / "CHANGELOG.md").write_text("## [0.4.1] - 2026-09-23\n- 変更\n", encoding="utf-8")
            body = release_body("v0.4.1", root)
            self.assertIn("説明", body)
            self.assertIn("## [0.4.1]", body)
            self.assertIn("- 変更", body)


if __name__ == "__main__":
    unittest.main()
