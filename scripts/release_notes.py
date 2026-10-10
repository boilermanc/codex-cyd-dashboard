#!/usr/bin/env python3
"""Combine versioned release notes with the matching CHANGELOG section."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def changelog_section(changelog: str, tag: str) -> str:
    version = tag.removeprefix("v")
    header = f"## [{version}]"
    lines = changelog.splitlines()
    start = next((i for i, line in enumerate(lines) if line.startswith(header + " - ")), None)
    if start is None:
        raise ValueError(f"CHANGELOG entry missing: {tag}")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## [")), len(lines))
    return "\n".join(lines[start:end]).strip()


def release_body(tag: str, root: Path = ROOT) -> str:
    if re.fullmatch(r"v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", tag) is None:
        raise ValueError(f"invalid release tag: {tag}")
    notes = (root / "docs" / "releases" / f"{tag}.md").read_text(encoding="utf-8").strip()
    changes = changelog_section((root / "CHANGELOG.md").read_text(encoding="utf-8"), tag)
    return f"{notes}\n\n{changes}\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(release_body(args.tag), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
