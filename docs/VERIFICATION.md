# Verification

The original USB chat dashboard was exercised on Windows 11 and an ESP32-2432S028R. User verified exact-chat taps, blue Working cards and amber Needs-you state. The portable helper adds configurable ports, optional device identity and Codex home; those changes are covered by unit tests. Public firmware uses openly licensed Atkinson Hyperlegible instead of local Windows fonts.

The demo is 26 seconds at 1920x1080/30fps with H.264/AAC and fast-start. Full decode, encoded hero/transition frames and normal-speed playback passed. Sample chats are fictional. Local build/test and CI results are recorded separately; a pending CI run is not a passing check.

Local preparation (2026-10-10): 25 Python unit tests passed; CMake/MSVC Release build and both native suites (ui-model and usb-cards) passed with assertions enabled. Public font firmware compiled and linked with the local ESP32 Arduino 2.0.17 toolchain and produced an application image. Standard PlatformIO dependency installation was still underway at initial publication. Device update used the existing locally tested font, darker Done fill (RGB565 0x11E6) and an application-only write to the confirmed CYD; private recovery hash was checked. Fresh USB acknowledgment and desktop connection were confirmed afterward.
