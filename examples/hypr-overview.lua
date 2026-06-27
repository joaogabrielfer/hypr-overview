local overviewPlugin = "/absolute/path/to/hypr-overview/build/hypr-overview.so"

hl.on("hyprland.start", function()
    hl.timer(function()
        hl.exec_cmd("hyprctl plugin load " .. overviewPlugin)
    end, { timeout = 250, type = "oneshot" })
end)
