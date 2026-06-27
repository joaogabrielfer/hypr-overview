-- hypr-overview never installs a key binding itself. Change this to any
-- combination you want.
local overviewKey = "SUPER + O"

local function toggleOverview()
    local plugin = hl.plugin.hyproverview

    if plugin == nil then
        hl.notification.create({
            text = "hypr-overview is not loaded; run: hyprpm reload",
            timeout = 3500,
            color = "#f04444",
        })
        return
    end

    -- Entry failures are also displayed by the plugin as a notification.
    plugin.toggle()
end

hl.bind(overviewKey, toggleOverview)

-- Return and keypad Enter are built into the overview itself and apply the
-- current selection without needing another bind.

-- Optional appearance and interaction settings:
-- hl.config({
--     plugin = {
--         hyproverview = {
--             padding = 30,
--             row_gap = 18,
--             column_gap = 10,
--             window_gap = 8,
--             rounding = 10,
--             animation_duration_ms = 220,
--             background_opacity = 0.94,
--             background_color = "#101018",
--             row_color = "#242432",
--             active_row_color = "#303044",
--             selection_color = "#7aa2f7",
--         },
--     },
-- })

-- Optional explicit controls. Existing focus binds keep working while the
-- overview is open, so these usually are not needed:
--
-- hl.bind("ALT + Left", function() hl.plugin.hyproverview.move("left") end)
-- hl.bind("ALT + Right", function() hl.plugin.hyproverview.move("right") end)
-- hl.bind("ALT + Up", function() hl.plugin.hyproverview.move("up") end)
-- hl.bind("ALT + Down", function() hl.plugin.hyproverview.move("down") end)
