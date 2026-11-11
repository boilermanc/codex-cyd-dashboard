"""Build the portable Windows helper from this checkout."""
import hashlib
import importlib.metadata
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.2.3"


def main():
    output = ROOT / "dist" / "windows" / VERSION
    output.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--console",
        "--name", "CYD-USB-Helper", "--distpath", str(output),
        "--workpath", str(ROOT / "build" / "windows"), "--specpath", str(ROOT / "build"),
    ]
    for filename in ("probe-chat-connection.py", "helper_diagnostics.py"):
        command += ["--add-data", str(ROOT / "scripts" / filename) + os.pathsep + "."]
    for module in ("ctypes", "msvcrt", "threading", "serial.tools.list_ports"):
        command += ["--hidden-import", module]
    command.append(str(ROOT / "scripts" / "usb-helper.py"))
    subprocess.run(command, check=True, cwd=ROOT)
    package = output / "CYD-USB-Helper"
    for name in ("Watch-Helper.ps1", "Set-HelperRecovery.ps1", "Stop-USB-Helper.ps1", "LICENSE", "NOTICE.md"):
        shutil.copyfile(ROOT / name, package / name)
    shutil.copyfile(ROOT / "docs" / "WINDOWS-HELPER.md", package / "WINDOWS-HELPER.md")
    shutil.copyfile(ROOT / "assets" / "licenses" / "pyserial.txt", package / "pyserial-LICENSE.txt")
    shutil.copyfile(Path(sys.base_prefix) / "LICENSE.txt", package / "Python-LICENSE.txt")
    distribution = importlib.metadata.distribution("pyinstaller")
    license_path = next(path for path in distribution.files if str(path).endswith("/licenses/COPYING.txt"))
    shutil.copyfile(distribution.locate_file(license_path), package / "PyInstaller-LICENSE.txt")
    executable = package / "CYD-USB-Helper.exe"
    subprocess.run([str(executable), "--help"], check=True, timeout=30)
    subprocess.run([str(executable), "--dry-run", "--seconds", "3"], check=True, timeout=30)
    if not (package / ".local" / "usb-helper-dry-run.json").is_file():
        raise RuntimeError("Packaged helper did not produce its health report")
    archive = output / f"CYD-USB-Helper-v{VERSION}-windows-x64.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(package.rglob("*")):
            relative = path.relative_to(package)
            if path.is_file() and ".local" not in relative.parts:
                bundle.write(path, relative)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / "SHA256SUMS.txt").write_text(f"{checksum}  {archive.name}\n", encoding="ascii")
    print(archive)


if __name__ == "__main__":
    main()
