# hypr-overview

`hypr-overview` provides a Niri-style overview for Hyprland's built-in
`scrolling` layout:

- every workspace is a row;
- each workspace row keeps a monitor-like aspect ratio instead of stretching
  edge to edge;
- wide scrolling workspaces expand laterally to reflect the full tape width
  before clipping to the monitor;
- scrolling columns are arranged horizontally inside their workspace row;
- windows stacked in a column retain their vertical arrangement;
- empty and single-column workspaces remain visible;
- previews use the live Wayland surface content.

https://github.com/user-attachments/assets/a32fdfc6-8437-4186-9c7d-bd6fd386291e


The plugin renders an animated overlay. It does not resize windows or mutate
scrolling column widths.

## Install with hyprpm

```bash
hyprpm add https://github.com/joaogabrielfer/hypr-overview
hyprpm enable hypr-overview
hyprpm reload
```

Verify that it is loaded:

```bash
hyprctl plugin list
```

The output must include `Plugin hypr-overview`.

## Bind it

The plugin does not reserve `SUPER+TAB` or any other key. Copy
`examples/hypr-overview.lua` or this snippet into your Hyprland 0.55 Lua config and the bind to the one you want:

```lua
hl.bind("SUPER + TAB", function()
    hl.plugin.hyproverview.toggle()
end)
```

to your preferred key combination.

The legacy hyprlang equivalent is in `examples/hypr-overview.conf`.

This repository does not modify your Hyprland configuration.

`hl.plugin.hyproverview.toggle()` is intended to be bound directly. The example
uses a direct Lua bind, not a wrapper helper.

## Interaction

- Toggle again to apply the selected window and close.
- `Return` or keypad enter applies the current selection and zooms back in.
- Existing Hyprland focus/workspace shortcuts continue working while open.
- When there are more workspaces than fit, the overview shows a sliding window
  of rows instead of shrinking every workspace.
- Hover selects a window.
- Left click selects and opens a window or workspace.
- Right or middle click cancels and returns to the original workspace/window.
- Mouse wheel moves through workspace rows.

## Lua API

Hyprland 0.55 exposes these functions under `hl.plugin.hyproverview`:

- `toggle()`
- `apply()`
- `cancel()`
- `move("left" | "right" | "up" | "down")`
- `active()`
- `status()`

Actions return `true` on success or `false, error` on failure.

## Legacy dispatchers

- `hyproverview_toggle`
- `hyproverview_apply`
- `hyproverview_cancel`
- `hyproverview_move` with `left`, `right`, `up`, or `down`
- `hyproverview_status`

## Configuration

All settings are optional and use the `plugin:hyproverview` namespace.

Hyprland must load the plugin before evaluating your config for these keys to
exist. `hyprpm enable ...` plus `hyprpm reload` is supported. Loading the
plugin later from an autostart timer is too late for `hl.config({ plugin = {
hyproverview = ... } })`.

| option | default |
| --- | --- |
| `enabled` | `true` |
| `click_select` | `true` |
| `click_apply` | `true` |
| `right_click_cancel` | `true` |
| `max_visible_workspaces` | `3` |
| `padding` | `30` |
| `row_gap` | `18` |
| `column_gap` | `10` |
| `window_gap` | `8` |
| `rounding` | `10` |
| `animation_duration_ms` | `220` |
| `background_opacity` | `0.94` |
| `background_color` | `#101018` |
| `row_color` | `#242432` |
| `active_row_color` | `#303044` |
| `selection_color` | `#7aa2f7` |

See `examples/hypr-overview.lua` for a Lua configuration block.

## Manual build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
hyprctl plugin load "$PWD/build/hypr-overview.so"
```

Hyprland plugins are ABI-specific. Build against headers matching the running
Hyprland version.

## Current scope

- workspaces on the focused monitor
- normal workspaces (special workspaces are excluded)
- scrolling layouts receive native column/stack positioning
- other layouts fall back to a horizontal window row
- floating windows are not shown yet
