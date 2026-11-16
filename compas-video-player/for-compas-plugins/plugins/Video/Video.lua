-- Video: the card's Video folder, in the player's own lists.
--
-- A plugin cannot decode a film -- it is Lua, and the decoding takes all of
-- the processor in C -- so the playing is done by the Video external app
-- (<card>/.apps/video). This is its front door: the list is drawn by the
-- player, in the player's fonts and theme, with names in any script, and a
-- tap hands the file to the app. When the film ends or is closed the app
-- exits and this list is back.
--
-- Install: copy this file to <card>/.plugins/Video.lua, then
-- Settings > System > Plugins > Refresh Plugins. The row is under More.
--
-- The app, its source and how to convert a film for this screen:
-- https://github.com/bidhata/Hiby-R1-Mod

plugin.define({
    id = "org.krishnendu.video",
    name = "Video",
    version = "0.1.1",
    api_min = 14,
})

local APP_ID = "video"
local FOLDER = "Video"

-- What the app's manifest lists under "opens". A file with another extension
-- would be refused by plugin.launch_app, so it is not shown at all.
local EXTENSIONS = { mp4 = true, m4v = true, mkv = true, avi = true, mov = true }

local function can(capability)
    return plugin.has_capability ~= nil and plugin.has_capability(capability)
end

local function note(text)
    plugin.show_list("Video", { { label = text, wrap = true } }, function() end)
end

local function app_installed()
    if not can("apps.list") then
        return true -- cannot tell; launch_app will say
    end
    for _, app in ipairs(plugin.list_apps()) do
        if app.id == APP_ID then
            return true
        end
    end
    return false
end

local function videos(dir)
    local names = {}
    for _, entry in ipairs(plugin.list_dir(dir)) do
        local ext = entry.name:match("%.([^.]+)$")
        if not entry.dir and ext and EXTENSIONS[ext:lower()] then
            names[#names + 1] = entry.name
        end
    end
    table.sort(names, function(a, b) return a:lower() < b:lower() end)
    return names
end

local function open()
    if not can("apps.launch") then
        note("This version of the player cannot start external apps. Update the player.")
        return
    end
    if not app_installed() then
        note("The Video app is not installed. Download it from github.com/bidhata/Hiby-R1-Mod " ..
             "and copy its video folder to .apps on the card.")
        return
    end

    local dir = plugin.sd_root() .. "/" .. FOLDER
    local names = videos(dir)
    if #names == 0 then
        note("No videos. Put them in the Video folder on the card, converted for this screen: " ..
             "see github.com/bidhata/Hiby-R1-Mod for how.")
        return
    end

    local rows = {}
    for i, name in ipairs(names) do
        -- Without the extension: every row would end in the same four letters.
        rows[i] = { label = name:gsub("%.[^.]+$", ""), wrap = true }
    end

    plugin.show_list("Video", rows, function(index)
        local ok, reason = plugin.launch_app(APP_ID, dir .. "/" .. names[index])
        if not ok then
            plugin.show_toast("Video did not start: " .. tostring(reason))
        end
    end)
end

-- Under More, next to External Apps, where the player has room for it; in
-- Additional Tools on a player that does not take rows there.
if can("ui.more_menu") then
    plugin.register_list_item("more", "Video", open)
else
    plugin.register_list_item("settings", "Video", open)
end
