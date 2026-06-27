-- Bind the overview to SUPER + TAB
hl.bind("SUPER + TAB", function()
    hl.plugin.hyproverview.toggle()
end)

-- Return and keypad Enter are built into the overview itself and apply the
-- current selection without needing another bind.

-- Optional appearance and interaction settings:
-- hl.config({
--     plugin = {
--         hyproverview = {
--             max_visible_workspaces = 3,
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
