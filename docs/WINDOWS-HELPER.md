# Windows helper with automatic recovery

Download the Windows x64 ZIP from the GitHub release and extract the entire
folder to a permanent location. Keep Codex desktop open. Python is bundled.
The helper works with existing v0.2.0 dashboard firmware.

## Configure your board

Stop your previous helper and close any serial monitor or web flasher.
Open PowerShell in the extracted folder. Replace COM11 with the CYD port shown
in Windows Device Manager. Run this once to read its identity.

```powershell
$serial = [System.IO.Ports.SerialPort]::new('COM11', 115200)
$serial.ReadTimeout = 3000
try {
    $serial.Open()
    $serial.WriteLine('CYD_HELLO')
    do { $reply = $serial.ReadLine().Trim() }
    until ($reply -match '^CYD_USB_READY 1 [0-9a-f]{12}$')
    $reply
} finally {
    $serial.Dispose()
}
```

Create the connection file using your port and the 12 characters printed after
`CYD_USB_READY 1`. The values below are examples.

```powershell
@{port='COM11'; device_id='0123456789ab'} | ConvertTo-Json | Set-Content .\connection.json -Encoding ASCII
```

## Enable recovery

Run these commands from the extracted folder. They register a task for your
Windows account and start it immediately.

```powershell
.\Set-HelperRecovery.ps1 -InstallDirectory (Get-Location).Path
schtasks.exe /Run /TN "Sweetwater Crowes Nest Helper"
```

Use one startup method for this board. If you used the earlier source checkout,
run `Set-USB-HelperStartup.ps1 -Remove` from that old checkout. Remove any
manually created CYD startup shortcut too. Keep the new folder at its installed
location.

The task starts at sign-in with unlimited running time. Its supervisor restarts
failed workers after two seconds. A worker that makes no progress for 45 seconds
exits so recovery can restart it. Windows also retries a failed supervisor.
The helper reopens the USB connection after eight seconds without board
acknowledgments and searches other CH340 ports for the saved identity.

To stop intentionally, run `Stop-USB-Helper.ps1` in this folder. The supervisor
honors that request. Run the task again to resume. To remove future automatic
startup, run `Set-HelperRecovery.ps1 -Remove`. Stop the helper separately if it
is still running.

Health and bounded transport logs are stored in the extracted folder's
`.local` directory. Health reports can contain local chat titles and IDs.
Reset reasons are recorded when the board sends its boot messages. Automatic
recovery requires the USB device to remain reachable by Windows.

## Build the download

On Windows x64 with Python 3.11 and this repository's helper dependencies,
install PyInstaller 6.16.0 and run the build command.

```powershell
python -m pip install -r requirements.txt pyinstaller==6.16.0
python scripts/build_windows_helper.py
```

The builder runs the packaged help and read-only smoke checks, includes runtime
license notices, excludes local health data, and writes the ZIP and SHA-256
checksum under `dist/windows/0.2.3`.
