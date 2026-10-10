# Installation and recovery

Confirm ESP32-2432S028R/ILI9341/XPT2046 and the USB serial port. Flashing replaces the current application. Keep backups private: flash can contain Wi-Fi credentials and pairing data.

Before flashing, stop any serial helper and install esptool (`py -m pip install esptool`). Read the identity, then save all 4 MB to a private folder outside this repository:

```powershell
py -m esptool --chip esp32 --port COM11 read-mac
py -m esptool --chip esp32 --port COM11 read-flash 0 0x400000 C:\PrivateBackup\cyd-full-flash.bin
Get-FileHash C:\PrivateBackup\cyd-full-flash.bin -Algorithm SHA256
```

Replace the port and private path for your computer. Verify the file is 4,194,304 bytes and retain its SHA-256. The normal PlatformIO upload does not deliberately erase all flash; changed partition tables may still make previous data inaccessible. Do not use erase-flash for a routine update.

Recovery on the same device:

```powershell
py -m esptool --chip esp32 --port COM11 write-flash 0 C:\PrivateBackup\cyd-full-flash.bin
```

Verify the same device identity before restore, then restart it and check the previous application. Never restore another person's credential-bearing backup.

If colors are reversed, change TFT_RGB_ORDER in platformio.ini between TFT_RGB and TFT_BGR, rebuild and upload. If taps miss, use BOOT long-press calibration. Unknown means no fresh desktop live status, not necessarily a hardware fault. Confirm Codex is open, the database exists and the helper logs show no schema/IPC errors. USB Offline means the helper is stopped or disconnected. Do not run two serial helpers simultaneously.
