# hypr-overview

`hypr-overview` adds an overview mode for Hyprland's built-in `scrolling` layout.

It does not draw fake thumbnails or reimplement the layout. Instead, it snapshots the live scrolling columns, temporarily fits them all on screen, keeps that overview stable while you pick a target, and then restores the original column widths when you apply or cancel.

## Current behavior

- Only works on the active workspace when that workspace uses the built-in `scrolling` layout.
- Refuses to enter on a fullscreen workspace.
- Tracks real windows and real focus, so selection stays aligned with the live layout.
- Handles window open, close, destroy, and workspace-move churn during overview by rebuilding its snapshot.
- Supports one active overview session at a time.

## Controls

Once the plugin is loaded, it captures `SUPER + TAB` itself. Once overview is active, the plugin handles the rest of the interaction itself.

- `SUPER + TAB`: enter overview
- `Escape`: cancel overview
- `Enter` or `Space`: apply overview
- Arrow keys or `h/j/k/l`: move selection
- Mouse hover: update selection
- Left click on a tiled overview window: select and apply
- Right or middle click: cancel
- Mouse wheel: move selection left or right

## Build

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Artifact:

```bash
build/hypr-overview.so
```

## Loading the plugin

Manual load:

```bash
hyprctl plugin load /absolute/path/to/build/hypr-overview.so
```

Manual unload:

```bash
hyprctl plugin unload /absolute/path/to/build/hypr-overview.so
```

## Latest Lua config

The validated Lua-era integration is:

```lua
local overviewPlugin = "/absolute/path/to/hypr-overview/build/hypr-overview.so"

hl.on("hyprland.start", function()
    hl.timer(function()
        hl.exec_cmd("hyprctl plugin load " .. overviewPlugin)
    end, { timeout = 250, type = "oneshot" })
end)
```

A complete example lives in [examples/hypr-overview.lua](/home/joaogabriel/personal/programming/projects/hypr-overview/examples/hypr-overview.lua:1).

Why this shape matters on Hyprland `0.55.4`:

- custom plugin dispatchers are not exposed through `hl.dsp.*`
- plugin config values are not available during the same top-level Lua parse that loads the plugin
- top-level immediate calls such as `hl.plugin.hyproverview.setup(...)` do not verify cleanly during config parsing
- delayed startup loading is the reliable path that verifies cleanly today

After the plugin loads, no extra bind is required for the default trigger. `SUPER + TAB` is handled internally by the plugin.

## Config

The plugin has two configuration surfaces:

- built-in defaults, which are what the validated Lua startup-load path uses
- Hyprland config values and Lua callbacks, which are available for preloaded or non-Lua flows

Built-in defaults:

| option | default |
| --- | --- |
| `enabled` | `true` |
| `cancel_on_workspace_switch` | `true` |
| `click_select` | `true` |
| `click_apply` | `true` |
| `right_click_cancel` | `true` |
| `warp_cursor_on_exit` | `true` |
| `restore_original_on_invalid_selection` | `true` |
| `fit_margin_factor` | `1.0` |
| `wheel_steps` | `1` |
| `exit_notification` | `"off"` |

Registered config values:

Internally the plugin also registers matching `plugin:hyproverview:*` values for Hyprland's config system.

| option | type | default | meaning |
| --- | --- | --- | --- |
| `enabled` | `bool` | `true` | master enable switch |
| `cancel_on_workspace_switch` | `bool` | `true` | cancel if focus moves to another workspace during overview |
| `click_select` | `bool` | `true` | let pointer hover update selection |
| `click_apply` | `bool` | `true` | let left click on a tiled overview window apply |
| `right_click_cancel` | `bool` | `true` | let right or middle click cancel |
| `warp_cursor_on_exit` | `bool` | `true` | warp the cursor to the selected window when leaving overview |
| `restore_original_on_invalid_selection` | `bool` | `true` | fall back to the original focused window if the selection disappeared |
| `fit_margin_factor` | `float` | `1.0` | scales fitted overview widths from `0.2` to `1.0` |
| `wheel_steps` | `int` | `1` | number of selection moves per wheel step |
| `exit_notification` | `string` | `"off"` | `"off"` or `"basic"` |

## Lua API

The plugin registers these callbacks under `hl.plugin.hyproverview`:

- `toggle()`
- `apply()`
- `cancel()`
- `active()`
- `status()`
- `setup()`

These callbacks are registered by the plugin, but on Hyprland `0.55.4` they are not part of the validated top-level Lua startup path documented above. Treat them as advanced/runtime hooks rather than the primary config interface.

Return behavior:

- `toggle`, `apply`, `cancel`, and `setup` return `true` on success
- on failure they return `false, "error message"`
- `active` returns `true` or `false`
- `status` returns a table with `active`, `enabled`, `workspace_id`, `has_selection`, and the current effective options

## Classic config

If you are not on the Lua config manager, the plugin also registers classic dispatchers:

- `hyproverview_toggle`
- `hyproverview_apply`
- `hyproverview_cancel`
- `hyproverview_status`

Example:

```ini
bind = SUPER, TAB, hyproverview_toggle
```

## Validation used

Build and load validation:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
hyprctl plugin load /absolute/path/to/build/hypr-overview.so
hyprctl plugin unload /absolute/path/to/build/hypr-overview.so
```

Lua config validation against the example file:

```bash
PLUGIN=/absolute/path/to/hypr-overview/build/hypr-overview.so
tmp=$(mktemp --suffix=.lua)
sed "s#/absolute/path/to/hypr-overview/build/hypr-overview.so#$PLUGIN#" examples/hypr-overview.lua > "$tmp"
Hyprland --verify-config -c "$tmp"
rm -f "$tmp"
```

Live checks completed:

- build succeeds
- plugin loads and unloads in the running compositor
- the Lua startup-load example parses cleanly with `Hyprland --verify-config`

## Known limits

- current-workspace overview only
- scrolling layout only
- no fullscreen-workspace support
- no custom dim/background renderer yet
- no multi-workspace Niri-style global overview yet
- floating windows stay live but are excluded from overview selection
