# Codex CYD Dashboard

[![Test and build](https://github.com/boilermanc/codex-cyd-dashboard/actions/workflows/ci.yml/badge.svg)](https://github.com/boilermanc/codex-cyd-dashboard/actions/workflows/ci.yml)

A touchscreen view of your local Codex chats on an ESP32 Cheap Yellow Display. Built by [Sweetwater Technology](https://sweetwater.technology/resources/).

![Dashboard overview](docs/media/dashboard-overview.png)

[Watch the 26-second demo](https://github.com/boilermanc/codex-cyd-dashboard/raw/refs/heads/main/docs/media/demo.mp4)

Each card shows its project, shortened chat title and live status. Multiple chats in the same project get separate cards. Tap a card to open that exact chat on your Windows computer. Tap the Chats heading or the Agents tab to move through pages of six chats.

| Card | Meaning |
| --- | --- |
| Blue - Working | Chat is actively running |
| Amber - Needs you | Waiting for your input or approval |
| Green - Done | Finished with an unread result |
| Gray - Idle | Not running |
| Red - Error | Runtime system error |
| Gray - Unknown | No fresh live status available |

![Separate chats](docs/media/separate-chats.png)

## Supported hardware and software

- ESP32-2432S028R, ESP-WROOM-32, ILI9341 320 x 240 display, XPT2046 resistive touch, 4 MB flash.
- Windows 11 with Codex desktop. Local chats only; remote/cloud chats are not supported.
- Python 3.11+ and PlatformIO Core. A USB data cable is required.
- Bluetooth is optional for the Actions and Navigate tabs. Agent cards use USB, independent of unnamed Micro Bluetooth slots.

Boards with ST7789 displays or ESP32-S3 processors need different firmware. Check your board before flashing. The tested display needs `TFT_RGB_ORDER=TFT_RGB`; other ILI9341 batches may use BGR.

## macOS support: help wanted

The USB dashboard helper currently supports Windows only. A macOS version still needs to be built and tested; the existing helper should not be treated as Mac-compatible.

We do not currently have a Mac available for development or device testing. Contributions from someone with a Mac running Codex desktop and a supported CYD are welcome.

The port needs to:

- Investigate and implement the macOS connection for live Codex chat status; the current Windows named-pipe transport cannot be reused unchanged.
- Verify the local chat/project database location and schema on macOS.
- Support selecting Mac USB serial ports and reconnecting to the identified CYD.
- Open the exact selected Codex chat using a macOS URL-opening mechanism.
- Provide Mac setup instructions and a background helper launcher.
- Test Working, Needs you, Done, Idle, disconnect recovery, paging and exact-chat taps on real hardware; assess optional BLE Actions and Navigate separately.

The goal is to reuse the existing CYD firmware, card layout and USB protocol where possible. Any required firmware changes must be identified through testing. This is a desktop Mac helper project, not an iPhone or iPad app. Please open a GitHub issue to coordinate investigation and share your macOS, Codex desktop and board versions. Do not attach private chat databases, message content or device backups.

## Get the latest version

Download the source ZIP from [the latest release](https://github.com/boilermanc/codex-cyd-dashboard/releases/latest), or update your clean clone from `main`. Existing users: stop the helper, run `git pull --ff-only`, then restart `Start-USB-Helper.ps1` with your usual port and optional device ID. If you have local edits, preserve them before updating. ZIP users should extract into a new folder and follow the Python environment setup below. Version 0.2.1 adds optional Windows sign-in startup; see [release notes](docs/releases/v0.2.1.md). No firmware update is needed when upgrading from v0.2.0. Version 0.2.0 adds visible stale-status age and requires updating both the helper and firmware: stop the helper, update the source, build and flash the intended CYD using the installation steps below, then restart the helper. The earlier v0.1.0 diagnostics-only update did not require reflashing.

## Install

1. Clone this repository and open PowerShell in its folder.
2. Create the helper environment:

   ```powershell
   py -3.11 -m venv .venv
   .\.venv\Scripts\python.exe -m pip install -r requirements.txt
   ```

3. Install PlatformIO Core separately, then build:

   ```powershell
   py -m pip install platformio==6.2.0
   pio run -e cyd
   ```

4. Back up the existing device before replacing its application; see [installation and recovery](docs/INSTALL.md). Find its serial port in Windows Device Manager. Flash only the intended board:

   ```powershell
   pio run -e cyd --target upload --upload-port COM11
   ```

   Replace `COM11` with your device's port. Startup prints `CODEX_CYD_READY`.

5. Keep Codex desktop open and start the helper:

   ```powershell
   .\Start-USB-Helper.ps1 -Port COM11
   ```

   The green USB indicator means the helper is connected. To stop: `.\Stop-USB-Helper.ps1`.

For foreground diagnostics:

```powershell
.\.venv\Scripts\python.exe scripts/usb-helper.py --port COM11
.\.venv\Scripts\python.exe scripts/usb-helper.py --dry-run --seconds 20
```

An optional `--device-id` (or launcher `-DeviceId`) pins the MAC-derived 12-digit identity reported by `CYD_HELLO`. The helper accepts protocol-v1 firmware on the explicitly selected port; it never scans or writes other ports. `--codex-home` overrides the local Codex data folder, defaulting to `CODEX_HOME` or `~/.codex`.

## Start automatically after a restart

After setting up the Python environment and verifying the correct CYD port, install a startup shortcut for your Windows account:

```powershell
.\Set-USB-HelperStartup.ps1 -Port COM11
```

Use your board's port, and optionally add `-DeviceId` with the same identity used by the manual launcher. No administrator access is needed. At your next Windows sign-in, the shortcut runs the existing launcher hidden in the background. Keep Codex desktop open for live chat status; the helper retries connections while the board or desktop is unavailable. Startup does not run before sign-in and does not restart a helper that crashes during the session.

The checkout and its `.venv` must stay at the same path. Run the command again to change the port or device identity. Each checkout has its own shortcut: remove the old one before moving the folder or enabling another installation for the same board. `-WhatIf` previews installation or removal without making changes.

```powershell
.\Set-USB-HelperStartup.ps1 -Remove
```

Removal leaves the current helper running. Use `Stop-USB-Helper.ps1` to stop it now; stopping alone leaves automatic startup enabled for the next sign-in. Installing startup does not flash firmware or change Bluetooth pairing.

## Controls

Agents: chat cards and paging. Actions: upstream BLE shortcuts for Fast, approval, rejection, fork, microphone and send. Navigate: BLE navigation/dial controls. Configure those bindings in Codex Micro settings after Bluetooth pairing. The helper never sends prompts or approvals. BOOT short-press rotates the display 180 degrees. BOOT long-press starts touch calibration. The current dashboard keeps the screen awake at full brightness.

## Compatibility and privacy

This is an unofficial extension of [roflsunriz/esp32-codex-notifications](https://github.com/roflsunriz/esp32-codex-notifications), based on commit `a96b80c5b8375aa347bed1826807d5c3b4b89b28`. It uses an undocumented local Codex IPC pipe and a read-only `state_5.sqlite` connection, tested against desktop 26.1007.2314.0. Future app updates may require changes.

The helper receives snapshots in memory but does not save message bodies or credentials. Ignored `.local/` health reports include chat titles and IDs; do not share them without reviewing them. No OpenAI API key or cloud service is required. Lost status becomes Unknown; lost USB becomes Offline and disables taps. Taps are tied to the displayed chat ID and helper session, so reordered cards cannot open another chat.

Screenshots and video are labeled fictional sample UI, not photographs or captures of the physical device. They illustrate the layout; device fonts may differ. The public firmware uses the bundled openly licensed Atkinson Hyperlegible font.

## Development

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s test -p "test_*.py"
powershell -NoProfile -File test/test_startup.ps1
pio run -e cyd
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

C++ host tests need a C++17 compiler and CMake. Font regeneration additionally needs Pillow (`pip install Pillow`) and `python scripts/generate-fonts.py`. See [verification](docs/VERIFICATION.md), [contributing](CONTRIBUTING.md) and [security](SECURITY.md).

## License and credits

Firmware and helper: MIT, preserving upstream notices in [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md). Font: SIL Open Font License in `assets/fonts/OFL.txt`. Sweetwater branding remains owned by Sweetwater Technology. Demo music was synthesized locally for this project. This project is not endorsed by OpenAI, Work Louder or Espressif.

## Connection diagnostics

The helper records bounded local events in `.local/usb-helper-events.jsonl` (1 MB plus two rotated backups). Events include USB verification/ACK and desktop connection transitions, loop delays, heartbeat, and sanitized exception classes/OS codes. Chat text, names and identifiers are excluded. Launchers retain timestamped stdout/stderr files across restarts; inspect these locally before sharing.

Temporary database and health-report file errors are retried. Unexpected Python exceptions restart the runtime after two seconds; normal stop and finite test runs do not restart. This does not restart an externally killed process or resolve a blocked desktop pipe. A missing heartbeat with a running process suggests a stalled loop. Holding a touchscreen contact can currently interrupt accepted firmware updates. These are diagnostic leads, not a confirmed cause of past disconnects.

## Visible status age

The Agents header shows the age of the last accepted USB frame. Cards turn gray and display `Stale 12s` (then minutes/hours) when their desktop status is 12 seconds old, or immediately when the helper knows its source disconnected. Loss of USB updates for 8 seconds also makes cards stale; the clock continues on the board while the desktop sleeps. Only fresh desktop state restores a live status color. Older helpers still support the USB timeout, but per-chat age requires the updated helper and firmware together. Empty cards show No chat; an unseen chat shows Unknown.
