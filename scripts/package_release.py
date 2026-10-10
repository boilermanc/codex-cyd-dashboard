#!/usr/bin/env python3
"""Build deterministic ESP32 release assets from PlatformIO output."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = re.compile(r"^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
ZIP_TIMESTAMP = (2026, 1, 1, 0, 0, 0)


def normalize_version(value: str) -> tuple[str, str]:
    match = VERSION_PATTERN.fullmatch(value)
    if match is None:
        raise ValueError(f"invalid semantic version: {value!r}")
    version = ".".join(match.groups())
    return version, f"v{version}"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise FileNotFoundError(f"required build file is missing: {path}")
    return path


def platformio_core_dir() -> Path:
    configured = os.environ.get("PLATFORMIO_CORE_DIR")
    return Path(configured).resolve() if configured else (Path.home() / ".platformio").resolve()


def verify_firmware_version(version: str) -> None:
    source = (ROOT / "src" / "codex-micro-ble.cpp").read_text(encoding="utf-8")
    expected = f'constexpr char kFirmwareVersion[] = "{version}";'
    if expected not in source:
        raise RuntimeError(f"firmware version does not match release: expected {expected}")


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    return info


def create_deterministic_zip(destination: Path, files: dict[str, Path], texts: dict[str, str]) -> None:
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(files):
            archive.writestr(zip_info(name), files[name].read_bytes())
        for name in sorted(texts):
            archive.writestr(zip_info(name), texts[name].encode("utf-8"))


def write_checksums(destination: Path, assets: list[Path]) -> None:
    lines = [f"{sha256(path)}  {path.name}" for path in sorted(assets, key=lambda item: item.name)]
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def flashing_guide(tag: str, merged_name: str) -> str:
    return f"""# {tag} 書き込み手順

対象: ESP32-2432S028R (ESP-WROOM-32 / ILI9341 / XPT2046)

## mergedイメージ（推奨）

初回導入または初期化用です。Bluetooth bonding、タッチ調整、画面方向を含むNVS設定は消去されます。

```powershell
python -m esptool --chip esp32 --port COM3 write_flash 0x0 {merged_name}
```

COM3は実際のCH340ポートへ置き換えてください。

## アプリだけ更新（設定維持）

既に本ファームウェアを使用中で、Bluetooth pairingと端末設定を維持する場合はこちらを使います。

```powershell
python -m esptool --chip esp32 --port COM3 write_flash 0x10000 firmware.bin
```

## 分割イメージ

```powershell
python -m esptool --chip esp32 --port COM3 write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB `
  0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

書き込み後に再起動し、Bluetooth設定で `Codex Micro` を接続してください。
"""


def package(version_input: str, environment: str, output_dir: Path) -> list[Path]:
    version, tag = normalize_version(version_input)
    verify_firmware_version(version)

    build_dir = require_file(ROOT / ".pio" / "build" / environment / "firmware.bin").parent
    core_dir = platformio_core_dir()
    esptool = require_file(core_dir / "packages" / "tool-esptoolpy" / "esptool.py")
    boot_app0 = require_file(
        core_dir
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "partitions"
        / "boot_app0.bin"
    )
    components = {
        "bootloader.bin": require_file(build_dir / "bootloader.bin"),
        "partitions.bin": require_file(build_dir / "partitions.bin"),
        "boot_app0.bin": boot_app0,
        "firmware.bin": require_file(build_dir / "firmware.bin"),
    }

    output_dir.mkdir(parents=True, exist_ok=True)
    prefix = f"esp32-codex-notifications-{tag}"
    merged = output_dir / f"{prefix}-merged.bin"
    firmware = output_dir / f"{prefix}-firmware.bin"
    bundle = output_dir / f"{prefix}-bundle.zip"
    checksums = output_dir / "SHA256SUMS.txt"

    subprocess.run(
        [
            sys.executable,
            str(esptool),
            "--chip",
            "esp32",
            "merge_bin",
            "-o",
            str(merged),
            "--flash_mode",
            "dio",
            "--flash_freq",
            "40m",
            "--flash_size",
            "4MB",
            "0x1000",
            str(components["bootloader.bin"]),
            "0x8000",
            str(components["partitions.bin"]),
            "0xe000",
            str(components["boot_app0.bin"]),
            "0x10000",
            str(components["firmware.bin"]),
        ],
        check=True,
        cwd=ROOT,
    )
    shutil.copyfile(components["firmware.bin"], firmware)

    component_manifest = {
        name: {"offset": offset, "sha256": sha256(path), "size": path.stat().st_size}
        for name, path, offset in [
            ("bootloader.bin", components["bootloader.bin"], "0x1000"),
            ("partitions.bin", components["partitions.bin"], "0x8000"),
            ("boot_app0.bin", components["boot_app0.bin"], "0xe000"),
            ("firmware.bin", components["firmware.bin"], "0x10000"),
        ]
    }
    manifest = json.dumps(
        {
            "version": version,
            "tag": tag,
            "environment": environment,
            "board": "ESP32-2432S028R",
            "merged_flash_offset": "0x0",
            "components": component_manifest,
        },
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
    ) + "\n"

    with tempfile.TemporaryDirectory() as temporary:
        merged_for_bundle = Path(temporary) / "merged.bin"
        shutil.copyfile(merged, merged_for_bundle)
        create_deterministic_zip(
            bundle,
            {**components, "merged.bin": merged_for_bundle},
            {
                "FLASHING.md": flashing_guide(tag, "merged.bin"),
                "manifest.json": manifest,
            },
        )

    release_assets = [merged, firmware, bundle]
    write_checksums(checksums, release_assets)
    return [*release_assets, checksums]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="semantic version or v-prefixed tag")
    parser.add_argument("--environment", default="cyd", help="PlatformIO environment")
    parser.add_argument("--output", type=Path, default=ROOT / "dist", help="output directory")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        assets = package(args.version, args.environment, args.output.resolve())
    except (FileNotFoundError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"release packaging failed: {error}", file=sys.stderr)
        return 1
    for asset in assets:
        try:
            display_path = asset.relative_to(ROOT)
        except ValueError:
            display_path = asset
        print(f"created {display_path} ({asset.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
