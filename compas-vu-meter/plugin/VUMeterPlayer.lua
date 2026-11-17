-- The built-in Now Playing screen with a stereo level meter under the cover,
-- in three forms: VU Meter Player has two slim bars, VU Dial Player two round
-- dials with needles, and VU Spectrum Player ten bars, one per octave.
--
-- The meter is drawn by the player itself: the layouts only name the widgets
-- (vu_left, vu_right, vu_peak_left, vu_peak_right, for the dials
-- vu_needle_left, vu_needle_right, and for the spectrum vu_band_1 to
-- vu_band_10 with their vu_band_peak_ caps) and the player moves them. A player
-- without the meter would leave them still, so the layouts are only offered
-- where the capability is present.
--
-- Swiping across the meter on Now Playing, or tapping it, switches between
-- the three.

plugin.define({
    id = "org.krishnendu.vu_meter_player",
    name = "VU Meter Player",
    version = "1.3.0",
    api_min = 19,
})

local T = {
    de = {
        ["VU Meter Player"] = "VU-Meter-Player",
        ["VU Dial Player"] = "VU-Zeiger-Player",
        ["VU Spectrum Player"] = "VU-Spektrum-Player",
    },
    es = {
        ["VU Meter Player"] = "Reproductor con vúmetro",
        ["VU Dial Player"] = "Reproductor con vúmetro de aguja",
        ["VU Spectrum Player"] = "Reproductor con analizador de espectro",
    },
    fi = {
        ["VU Meter Player"] = "VU-mittarisoitin",
        ["VU Dial Player"] = "VU-osoitinsoitin",
        ["VU Spectrum Player"] = "VU-spektrisoitin",
    },
    fr = {
        ["VU Meter Player"] = "Lecteur avec vumètre",
        ["VU Dial Player"] = "Lecteur avec vumètre à aiguille",
        ["VU Spectrum Player"] = "Lecteur avec analyseur de spectre",
    },
    it = {
        ["VU Meter Player"] = "Lettore con VU meter",
        ["VU Dial Player"] = "Lettore con VU meter ad ago",
        ["VU Spectrum Player"] = "Lettore con analizzatore di spettro",
    },
    ja = {
        ["VU Meter Player"] = "VUメータープレーヤー",
        ["VU Dial Player"] = "VUダイヤルプレーヤー",
        ["VU Spectrum Player"] = "VUスペクトラムプレーヤー",
    },
    nl = {
        ["VU Meter Player"] = "VU-meterspeler",
        ["VU Dial Player"] = "VU-wijzerspeler",
        ["VU Spectrum Player"] = "VU-spectrumspeler",
    },
    pt = {
        ["VU Meter Player"] = "Reprodutor com VU meter",
        ["VU Dial Player"] = "Reprodutor com VU meter de ponteiro",
        ["VU Spectrum Player"] = "Reprodutor com analisador de espectro",
    },
    ru = {
        ["VU Meter Player"] = "Плеер с VU-индикатором",
        ["VU Dial Player"] = "Плеер со стрелочным VU-индикатором",
        ["VU Spectrum Player"] = "Плеер с анализатором спектра",
    },
    uk = {
        ["VU Meter Player"] = "Плеєр із VU-індикатором",
        ["VU Dial Player"] = "Плеєр зі стрілковим VU-індикатором",
        ["VU Spectrum Player"] = "Плеєр з аналізатором спектра",
    },
    vi = {
        ["VU Meter Player"] = "Trình phát có đồng hồ VU",
        ["VU Dial Player"] = "Trình phát có đồng hồ VU kim",
        ["VU Spectrum Player"] = "Trình phát có phổ tần",
    },
    zh_CN = {
        ["VU Meter Player"] = "VU 表播放器",
        ["VU Dial Player"] = "VU 指针表播放器",
        ["VU Spectrum Player"] = "VU 频谱播放器",
    },
}
local function tr(s)
    return plugin.tr(s, T)
end

local LAYOUTS = {
    meter = {
        id = "plugin.vu_meter_player",
        xml = "VUMeterPlayer/player_layouts/vu_meter_player.xml",
        name = tr("VU Meter Player"),
    },
    dial = {
        id = "plugin.vu_dial_player",
        xml = "VUMeterPlayer/player_layouts/vu_dial_player.xml",
        name = tr("VU Dial Player"),
    },
    spectrum = {
        id = "plugin.vu_spectrum_player",
        xml = "VUMeterPlayer/player_layouts/vu_spectrum_player.xml",
        name = tr("VU Spectrum Player"),
    },
}

if plugin.has_capability and plugin.has_capability("ui.player_layout_xml")
        and plugin.has_capability("ui.player_layout_meter") then
    -- Listed in Settings > Personalize > Player Layout, in this order.
    plugin.set_player_layout(LAYOUTS.meter)
    plugin.set_player_layout(LAYOUTS.dial)
    plugin.set_player_layout(LAYOUTS.spectrum)

    -- A swipe across the meter goes to the next kind or the one before, and
    -- a tap to the next: bars, dials, spectrum, bars. Each layout names its
    -- neighbours, so no state is kept here. The
    -- change lasts until the player restarts; the layout chosen in Settings
    -- is the one it starts with.
    if plugin.has_capability("ui.player_actions") then
        for name, layout in pairs(LAYOUTS) do
            plugin.register_player_action("vu_to_" .. name, function()
                plugin.set_player_layout(layout)
            end)
        end
    end
end
