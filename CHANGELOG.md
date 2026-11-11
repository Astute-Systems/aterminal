# Changelog

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
