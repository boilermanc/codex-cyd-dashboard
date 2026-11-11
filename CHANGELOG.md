# Changelog

## [0.2.3] - 2026-11-11

- Reopen silent USB connections and require a verified identity before accepting acknowledgments.
- Exit stalled helper workers so recovery can restart them.
- Include a Windows worker supervisor, sign-in recovery task installer, and portable setup guide.
- Record board startup and reset reasons when received.
- Build and smoke-test the portable Windows download in CI.

## [0.2.2] - 2026-11-07

- Find the saved CYD on another CH340 port when Windows changes its COM assignment.

## [0.2.1] - 2026-10-13

### Added

- Optional per-user Windows startup shortcut for the USB helper, with configurable COM port and optional device identity.
- Removal and `-WhatIf` support for startup configuration.
- Isolated Windows shortcut tests in the CI helper job and startup setup instructions in the README.

This release changes Windows startup tooling only. Firmware is unchanged from v0.2.0.

Earlier release notes are available on the repository's GitHub Releases page.
