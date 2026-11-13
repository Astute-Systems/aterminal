# Changelog

## 1.1.0

- Shift+Up and Shift+Down scroll one line, and Shift+Home and Shift+End jump to the oldest line and the live screen. Full-screen applications still receive these keys.
- Insert, Delete, PageUp and PageDown send xterm modifier parameters, and Shift or Ctrl with F1-F12 and F13-F20 reach the application.
- Alt+Backspace, Ctrl+Backspace, Alt with Tab and Escape, and Ctrl+2 to Ctrl+8, Ctrl+/ and Ctrl+- send the xterm bytes.
- Keypad keys follow NumLock and support VT100 application keypad mode (`ESC =` / `ESC >`).
- Focus in and out reporting (mode 1004).

## 1.0.0

- Arm64 `.deb` packages for Ubuntu 22.04 and 26.04, cross-compiled in CI.
- The Snap Store upload now runs only for version tags, as the store holds classic snaps for review one upload at a time.
- Project links point to the Astute-Systems repository.
- Includes the 0.2.0 mouse support for terminal applications.

## 0.2.0

- Mouse support for terminal applications such as Copilot CLI: clicks, drags, motion and the wheel are reported when an application enables xterm mouse tracking (modes 1000, 1002, 1003 and SGR 1006).
- Hold Shift to bypass application mouse tracking and select text, paste, or open setup with right click.

## 0.1.1

- `exit` closes the console, and closing the last console quits aterminal.
- Consoles stay open with `--hold`, or when a command could not run.
- Released under the MIT License.
- Published to the Snap Store: `sudo snap install aterminal --classic`.

## 0.1.0

First release.

- Twelve shell consoles on F1-F12, with green, amber, and colour CRT modes.
- Retro bash prompts, message-of-the-day screens, and middle-click selection paste.
- Packages for Ubuntu 22.04, 24.04 and 26.04, AlmaLinux 9 and 10, and Arch Linux.
