# Cross-Platform Notes

Catslock can be made cross-platform as a product idea, but not as one shared
keyboard-hook implementation. The behavior is inherently platform-specific:
detect a double-tap of CapsLock, globally suppress keyboard input, correct the
CapsLock toggle state, and expose an out-of-band control path.

This repository remains Windows-only. The current implementation is intentionally
focused on the Win32 backend.

## Short Answer

A cross-platform Catslock would need separate native backends:

- Windows: `WH_KEYBOARD_LL` low-level keyboard hook.
- macOS: Quartz/CoreGraphics event tap.
- Ubuntu/X11: `evdev`, `libinput`, XRecord, or XInput2.
- Ubuntu/Wayland: generally not available as a normal app because Wayland
  intentionally blocks arbitrary global keyboard hooks.
- WSL: not directly possible because WSL does not own the physical keyboard
  input stack.

## Recommended Architecture

A future cross-platform version should share the state machine and keep the
actual keyboard interception native per platform.

```text
core/
  catslock_state.*
  double_tap_detector.*
  state_file.*
  logging.*
platform/
  windows/
    hook_win32.cpp
  macos/
    hook_quartz.mm
  linux/
    hook_evdev.cpp
scripts/
  catslock.ps1
  catslock.sh
```

The shared core would handle:

- `IDLE -> FIRST_TAP -> CATSLOCK_ON -> FIRST_TAP_OFF -> CATSLOCK_OFF`
- 500 ms CapsLock double-tap logic
- state file writing
- diagnostics/logging
- an abstract query/toggle control channel

Each platform backend would handle:

- global key capture
- suppressing keyboard input
- CapsLock LED/toggle correction
- privilege and permission model
- native out-of-band signaling

## Windows

The current repository implements the Windows backend:

- `WH_KEYBOARD_LL` via `SetWindowsHookEx`
- Win32 message pump
- `requireAdministrator` UAC manifest
- CapsLock physical toggle correction
- `%TEMP%\catslock.state`
- `%TEMP%\catslock.log`
- named event: `CatslockToggle`
- relaxed event security descriptor so non-admin terminals can signal an
  elevated Catslock process

Windows is the first-class supported target for this repo.

## macOS

macOS would likely use a Quartz/CoreGraphics event tap:

- `CGEventTapCreate`
- Accessibility permission requirement
- event suppression by returning `nullptr` from the event tap callback
- special handling for CapsLock, because macOS treats it differently from
  ordinary key-down/key-up input

This would probably require Objective-C++ (`.mm`) or C++ plus CoreFoundation
bridging.

## Ubuntu / Linux X11

Linux on X11 can be implemented, but there are multiple possible approaches:

- `evdev` or `libinput` for low-level device access
- `EVIOCGRAB` to exclusively grab the keyboard device
- XRecord or XInput2 for X11-level capture

The lower-level `evdev` approach is usually more reliable for suppression, but
it requires elevated privileges or access to `/dev/input/event*`, commonly via
root or membership in the `input` group.

CapsLock LED/toggle correction would need Linux-specific handling through input
state APIs or injected key events.

## Ubuntu / Linux Wayland

Wayland is the hardest target. Its security model intentionally prevents normal
applications from globally intercepting and suppressing keyboard input.

A reliable Wayland Catslock would likely require one of:

- compositor-specific support
- a privileged daemon
- desktop-environment integration
- a future portal or extension designed for this class of tool

For a normal standalone app, global keyboard suppression on Wayland should be
considered unsupported.

## WSL

WSL cannot directly implement Catslock because it does not receive or control
the physical Windows keyboard input stream.

A WSL-friendly design would make WSL a client only:

- WSL command-line tool sends query/toggle requests
- Windows Catslock process remains responsible for the actual keyboard hook
- communication could use a Windows named pipe, TCP localhost, a file/event
  bridge, or PowerShell interop

The actual hook must still run on Windows.

## Practical Plan

If Catslock ever becomes cross-platform, the pragmatic path would be:

1. Keep this repository Windows-only.
2. Extract the state machine into a small reusable core.
3. Add a platform abstraction for keyboard capture and suppression.
4. Add a `catslockctl` command for query/toggle.
5. Implement Linux X11/evdev as the next backend.
6. Implement macOS with a Quartz event tap.
7. Treat WSL as a client that controls the Windows backend.
8. Treat Wayland as unsupported unless compositor-specific integration is added.

That keeps the current project simple while leaving a clear design path for a
future multi-platform Catslock.
