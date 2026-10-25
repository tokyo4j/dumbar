# dumbar

A simple wayland panel and taskbar built with GTK 4.

## Supported widgets

- Taskbar with the windows on the active workspace, window icons, focus, minimize, maximize, fullscreen, sticky, move to workspace, close, and drag-and-drop reordering
- System tray with StatusNotifier items and context menus
- Speaker and microphone volume controls with mute and mouse-wheel adjustment
- Battery percentage, charging state, and remaining-time display
- Clock with hover tooltip and calendar popup

## Dependencies

- gtkmm 4
- gtk4-layer-shell
- Wayland client libraries and `wayland-scanner`
- PulseAudio

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

On Arch Linux, `cd archlinux && makepkg -si` builds and installs the checked-out commit.

The Qt 6 version was written by GPT 5.6 Luna in a single day and rewritten in GTK 4 by Claude Opus 5.5. Currently, all configuration is hardcoded.
