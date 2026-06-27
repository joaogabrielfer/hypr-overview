# hypr-overview

`hypr-overview` provides a Niri-style overview for Hyprland's built-in
`scrolling` layout:

- every workspace is a row;
- scrolling columns are arranged horizontally inside their workspace row;
- windows stacked in a column retain their vertical arrangement;
- empty and single-column workspaces remain visible;
- previews use the live Wayland surface content.

The plugin renders an overlay. It does not resize windows or mutate scrolling
column widths.

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
`examples/hypr-overview.lua` into your Hyprland 0.55 Lua config and change:

```lua
local overviewKey = "SUPER + O"
```

to your preferred key combination.

The legacy hyprlang equivalent is in `examples/hypr-overview.conf`.

This repository does not modify your Hyprland configuration.

## Interaction

- Toggle again to apply the selected window and close.
- Existing Hyprland focus/workspace shortcuts continue working while open.
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

| option | default |
| --- | --- |
| `enabled` | `true` |
| `click_select` | `true` |
| `click_apply` | `true` |
| `right_click_cancel` | `true` |
| `padding` | `30` |
| `row_gap` | `18` |
| `column_gap` | `10` |
| `window_gap` | `8` |
| `rounding` | `10` |
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
