# 37RCode

37RCode is a native C++ Windows 10/11 desktop app for live microphone monitoring, system-audio loopback routing, and realtime voice controls in a custom Coffee Smooth UI.

## Features

- Win32 + Direct2D/DirectWrite custom UI, rendered from a high-frequency frame loop targeting 240 FPS where the machine and compositor allow it.
- WASAPI device enumeration for active render and capture devices.
- Separate selections for headset output, microphone input, and output/router.
- Live microphone monitoring to the selected headset output.
- PC-sound monitoring via WASAPI loopback from the current Windows default render endpoint to the selected router output.
- Feedback protection for PC monitoring when loopback source and router are the same endpoint.
- Realtime DSP on the microphone route:
  - Volume/input gain
  - Low-shelf bass boost/cut
  - Mid peaking EQ
  - High-shelf treble boost/cut
  - FFT overlap-add pitch shifting, +/- 12 semitones
- Device refresh on Windows device-change events plus periodic active-device refresh.
- Custom brown/gold Coffee Smooth app icon.

## Build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 Build Tools or Visual Studio 2022 with the C++ desktop workload
- Windows 10 SDK

From PowerShell:

```powershell
.\scripts\build-release.ps1
```

The release executable is written to:

```text
bin\Release\37RCode.exe
```

You can also open `37RCode.sln` in Visual Studio and build `Release|x64`.

## Notes

The app uses shared-mode WASAPI so it does not take exclusive control of devices. For PC-sound monitoring, Windows audio is captured from the current default output device with loopback capture. If that source is the same as the selected router, 37RCode blocks the route to avoid a self-loop.

Pitch shifting is a realtime in-process DSP implementation and adds a small processing latency by design. With pitch set to 0 semitones, the pitch stage is bypassed.
