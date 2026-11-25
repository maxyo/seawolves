// Settings (interface "option_screen"): three pages on a rail («Игра», «Управление», «Звук и графика»), each in
// columns of C-Setting rows; a debuggable app adds «Отладка», its single fx overrides. The game's own controls apply as
// they change and are kept or dropped with BTN_OK («Готово») / BTN_CANCEL (←), with no unsaved-changes question. The
// application's options come from the platform host on the state channel "appSettings", are kept as a draft and go
// back with «Готово» (Bindings::SendHost). Fills the pack screen <s-settings>.
#include "controls.h"
#include "app_ui_names.hpp"

#include <platform/display_options.hpp>
#include <platform/pirate_frequency.hpp>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"BTN_OK", "button", true},
    {"BTN_CANCEL", "button", true},
    {"MUSIC_SLIDE", "slider"},
    {"SOUND_SLIDE", "slider"},
    {"DIALOG_SLIDE", "slider"},
    {"VMOUSE_SENSITIVITY_SLIDE", "slider"},
    {"HMOUSE_SENSITIVITY_SLIDE", "slider"},
    {"SEACAMPERSP_SLIDE", "slider"},
    {"AUTOSAVE_CHECKBOX", "check"},
    {"DIRECTSAIL_CHECKBOX", "check"},
    {"ENHANCEDSAILING_CHECKBOX", "check"},
    {"INVERT_MOUSE_CHECKBOX", "check"},
    {"CAMERASWING_CHECKBOX", "check"},
    {"QUESTMARK_CHECKBOX", "check"},
    {"SHIPMARK_CHECKBOX", "check"},
    {"BATTLE_MODE_CHECKBOX", "check"},
    // Not offered: colour correction (unavailable in the Android renderer), keyboard
    // bindings and their defaults (mobile only), the game's effects the quality preset decides (glow, sky
    // rotation, dynamic lights, simple sea, crew on deck, grass and sea detail: renderer scene_detail.hpp),
    // «Всегда бегом» (the move stick's travel decides walk or run), the game's HUD scale (the portable HUD
    // replaces the game's), the game's own captions and pictures of its layout.
    {"HERB_CHECKBOX", "check"},
    {"SEA_DETAILS_SLIDE", "slider"},
    {"ALWAYS_RUN_CHECKBOX", "check"},
    {"HUD_SLIDE", "slider"},
    {"HUD_DESCRIP_TEXT", "text"},
    {"BTN_CONTROLS_DEFAULT", "button"},
    {"GLOW_SLIDE", "slider"},
    {"ROTATESKY_CHECKBOX", "check"},
    {"DYNAMICLIGHTS_CHECKBOX", "check"},
    {"SIMPLESEA_CHECKBOX", "check"},
    {"CREWONDECK_CHECKBOX", "check"},
    {"GAMMA_SLIDE", "slider"},
    {"BRIGHT_SLIDE", "slider"},
    {"CONTRAST_SLIDE", "slider"},
    {"CONTROLS_LIST", "choice"},
    {"CHANGEKEY_*", "*"},
    {"KEY_CHOOSER", "*"},
    {"TABBTN_*", "image|button"},
    {"TABSTR_*", "text"},
    {"TITLE", "heading|text"},
    {"TITLES_STR", "text"},
    {"TITLES_IMAGES", "image"},
    {"HARDWARE_SETTINGS", "text"},
    {"SOFTWARE_SETTINGS", "text"},
    {"MAIN_BACK", "image"},
};

// «Игра», «Управление», «Звук и графика»; a debuggable app adds the untranslated «Отладка» (TabCount).
const char *const kTabs[] = {StringKey("settings.tab_game"), StringKey("settings.tab_controls"),
                             StringKey("settings.tab_sound_graphics")};

// The application's renderer options as the host last reported them.
Json &AppSettings()
{
    static Json settings;
    return settings;
}

// What the renderer reports about the device (UI channel "renderCaps"): {"upscale": "supported" | "unsupported" |
// "unknown"}, whether «Апскейл текстур ×2» can work.
Json &RenderCaps()
{
    static Json caps = Json::object();
    return caps;
}

class RenderCapsConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "renderCaps";
    }
    void OnState(const Json &state, Router &) override
    {
        RenderCaps() = state.is_object() ? state : Json::object();
    }
};

class AppSettingsConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return app::channel::appSettings;
    }
    void OnState(const Json &state, Router &router) override
    {
        AppSettings() = state;
        router.RebuildActive(); // the cache facts of an open settings screen are the host's latest
    }
};

struct Column
{
    std::string title;
    Json rows = Json::array();
};

class SettingsScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-settings";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        if (!draft_.is_object())
        {
            draft_ = AppSettings();
            bindings.SendHost(app::host_action::app_cache_measure,
                              ""); // the cache grows while the game runs: its size as of now
        }

        std::vector<std::pair<std::string, std::vector<Column>>> sections;
        const int tabs_count = TabCount();
        tab_ = std::clamp(tab_, 0, tabs_count - 1);
        if (tab_ == 0)
            sections.push_back({"", GamePage(admitted, bindings)});
        else if (tab_ == 1)
            sections.push_back({"", ControlsPage(admitted, bindings)});
        else if (tab_ == 2)
            sections.push_back({"", SoundGraphicsPage(admitted, bindings)});
        else
            sections.push_back({"", DebugPage(bindings)});

        Json view = Json::object();
        view["title"] = bindings.Text("settings.title");
        Json tabs = Json::array();
        for (int i = 0; i < tabs_count; ++i)
            tabs.push_back(Json{
                {"label", i < static_cast<int>(std::size(kTabs)) ? bindings.Text(kTabs[i]) : std::string("Отладка")},
                {"selected", i == tab_}});
        view["tabs"] = tabs;
        bindings.On("tabs", "select", [this, &bindings](const Event &event) {
            int tab = 0;
            if (!EventNumber(bindings, event, tab))
                return;
            tab_ = std::clamp(tab, 0, TabCount() - 1);
            bindings.Rebuild();
        });
        Json pages = Json::array();
        for (const auto &[title, columns] : sections)
        {
            Json cols = Json::array();
            for (const Column &column : columns)
                cols.push_back(Json{{"title", column.title}, {"rows", column.rows}});
            pages.push_back(Json{{"title", title}, {"columns", cols}});
        }
        view["sections"] = pages;

        const Widget *ok = admitted.One("BTN_OK");
        const Widget *cancel = admitted.One("BTN_CANCEL");
        view["done"] = Json{{"label", bindings.Text("settings.done")},
                            {"disabled", !ok->Offers("press")},
                            {"reason", ok->unavailable_reason},
                            {"busy", bindings.Pending("done")}};
        bindings.On("done", "", [this, &bindings, ok](const Event &) {
            // The settings only: what the host reports beside them (the device's options, the cache, the automatic
            // preset) does not go back.
            const auto choices = [](Json settings) {
                for (const char *fact : app::host_fact::all)
                    settings.erase(fact);
                return settings;
            };
            if (draft_.is_object() && AppSettings().is_object() && choices(draft_) != choices(AppSettings()))
            {
                const Json settings = choices(draft_);
                bindings.SendHost(app::host_action::app_settings, settings.dump());
                AppSettings().update(settings);
            }
            bindings.Send(*ok, "press");
        });
        bindings.On("header-back", "", [&bindings, cancel](const Event &) { bindings.Back(cancel); });
        return view;
    }

  private:
    // One game control (or the radio group of a node) as a row; nothing when absent.
    static void GameRow(const Admission &admitted, Bindings &bindings, Column &column, const char *node,
                        const char *label = nullptr)
    {
        const std::vector<const Widget *> widgets = admitted.All(node);
        if (widgets.empty())
            return;
        const std::vector<std::string> labels =
            label ? std::vector<std::string>{bindings.Text(label)} : std::vector<std::string>();
        ControlRows(bindings, widgets, std::string(node) + "-", labels, column.rows);
    }

    std::vector<Column> GamePage(const Admission &admitted, Bindings &bindings)
    {
        // «Игра»: the game's rules at sea and on the map, and its marks.
        Column sea{bindings.Text("settings.sea_and_map")}, markers{bindings.Text("settings.markers")};
        // «Частота пиратов на карте» (platform/pirate_frequency.hpp): an app setting the game's encounter script
        // asks for at every random encounter, so it counts from the next one after «Готово».
        const std::vector<int> frequencies(std::begin(storm::game::kPirateFrequencies),
                                           std::end(storm::game::kPirateFrequencies));
        Segment(bindings, sea, app::setting::pirate_frequency, "settings.pirate_frequency", frequencies,
                {bindings.Text("settings.pirates_rare"), bindings.Text("settings.pirates_sometimes"),
                 bindings.Text("settings.pirates_often")},
                false);
        GameRow(admitted, bindings, sea, "DIRECTSAIL_CHECKBOX");
        GameRow(admitted, bindings, sea, "ENHANCEDSAILING_CHECKBOX");
        GameRow(admitted, bindings, sea, "AUTOSAVE_CHECKBOX");
        GameRow(admitted, bindings, markers, "QUESTMARK_CHECKBOX");
        GameRow(admitted, bindings, markers, "SHIPMARK_CHECKBOX");
        GameRow(admitted, bindings, markers, "BATTLE_MODE_CHECKBOX");
        return {sea, markers};
    }

    std::vector<Column> ControlsPage(const Admission &admitted, Bindings &bindings)
    {
        // «Управление»: the camera (the touch drag goes through the game's mouse sensitivity and inversion).
        Column camera{bindings.Text("settings.camera")};
        Sensitivity(admitted, bindings, camera);
        GameRow(admitted, bindings, camera, "INVERT_MOUSE_CHECKBOX");
        if (!camera.rows.empty() && camera.rows.back()["id"] == "INVERT_MOUSE_CHECKBOX-0")
            camera.rows.back()["label"] = bindings.Text("settings.invert_vertical");
        GameRow(admitted, bindings, camera, "SEACAMPERSP_SLIDE", "settings.sea_camera");
        GameRow(admitted, bindings, camera, "CAMERASWING_CHECKBOX");
        if (!camera.rows.empty() && camera.rows.back()["id"] == "CAMERASWING_CHECKBOX-0")
            camera.rows.back()["label"] =
                bindings.Text("settings.door_camera_turn"); // the game's «Поворот камеры» reads as any turn
        // «Экран и касания»: the app's display options (platform/display_options.hpp), applied with «Готово». The
        // UI sizes are the ones the host found the display can fit (ui_scale_options: the designed layout height
        // left).
        Column screen{bindings.Text("settings.screen_touch")};
        std::vector<int> scales;
        Json scaleLabels = Json::array(), transparencies = Json::array();
        if (draft_.is_object() && draft_.contains(app::host_fact::ui_scale_options) &&
            draft_[app::host_fact::ui_scale_options].is_array())
            for (const Json &scale : draft_[app::host_fact::ui_scale_options])
                if (scale.is_number_integer() && storm::android::OneOf(app::step::ui_scale.values, scale.get<int>()))
                {
                    scales.push_back(scale.get<int>());
                    scaleLabels.push_back(std::to_string(scales.back()) + "%");
                }
        for (const int share : app::step::hud_transparency.values)
            transparencies.push_back(share == 0 ? bindings.Text("settings.transparency_none")
                                                : std::to_string(share) + "%");
        Segment(bindings, screen, app::setting::ui_scale, "settings.ui_scale", scales, scaleLabels, false);
        // A display with room for no larger size than 100 %: the row stays, disabled, and a tap says why.
        if (scales.size() < 2 && Known(app::setting::ui_scale))
        {
            screen.rows.back()["disabled"] = true;
            screen.rows.back()["reason"] = bindings.Text("settings.ui_scale_no_room");
        }
        Segment(bindings, screen, app::setting::hud_transparency, "settings.hud_transparency",
                std::vector<int>(app::step::hud_transparency.values.begin(), app::step::hud_transparency.values.end()),
                transparencies, false);
        Toggle(bindings, screen, app::setting::left_handed, "settings.left_handed"); // the HUD mirrored left to right
        Toggle(bindings, screen, app::setting::haptics,
               "settings.haptics"); // «Вибрация»: hits, broadsides, boarding (android_haptics.hpp)
        return {camera, screen};
    }

    std::vector<Column> SoundGraphicsPage(const Admission &admitted, Bindings &bindings)
    {
        // «Звук и графика». The graphics choice is the preset; it also decides the game's grass and sea detail
        // (renderer scene_detail.hpp). The graphics API in use is shown in «О программе».
        Column sound{bindings.Text("settings.volume")}, graphics{bindings.Text("settings.graphics")};
        GameRow(admitted, bindings, sound, "MUSIC_SLIDE", "settings.music");
        GameRow(admitted, bindings, sound, "SOUND_SLIDE", "settings.sound_effects");
        GameRow(admitted, bindings, sound, "DIALOG_SLIDE", "settings.voices");
        // The preset's three names stand right under the column's title «Графика»: an empty label (every row
        // carries every field the template reads; strict host renders refuse a missing one).
        const Json presets = {bindings.Text("settings.fx_low"), bindings.Text("settings.fx_medium"),
                              bindings.Text("settings.fx_high")};
        Segment(bindings, graphics, app::setting::fx_preset, "settings.graphics", {0, 1, 2}, presets, false);
        graphics.rows.back()["label"] = "";
        // The automatic preset's rung below «Низкая»: «Низкая» with the scene at a lower height, which only the
        // measurement chooses (fx_auto_render_height, while that choice stands); its height under the row.
        if (Known(app::host_fact::fx_auto_render_height) &&
            draft_[app::host_fact::fx_auto_render_height].get<int>() > 0 && Known(app::setting::fx_preset) &&
            draft_[app::setting::fx_preset].get<int>() == 0)
            graphics.rows.back()["hint"] =
                Fill(bindings.Text("settings.fx_scene_height"),
                     {std::to_string(draft_[app::host_fact::fx_auto_render_height].get<int>())});
        // «Частота кадров»: the rates the host found the display can show (frame_rate_options: whole divisors
        // of its refresh rates), applied at once with «Готово» (the engine's frame pacing).
        std::vector<int> rates;
        Json rateLabels = Json::array();
        if (draft_.is_object() && draft_.contains(app::host_fact::frame_rate_options) &&
            draft_[app::host_fact::frame_rate_options].is_array())
            for (const Json &rate : draft_[app::host_fact::frame_rate_options])
                if (rate.is_number_integer())
                {
                    rates.push_back(rate.get<int>());
                    rateLabels.push_back(std::to_string(rates.back()));
                }
        Segment(bindings, graphics, app::setting::frame_rate, "settings.frame_rate", rates, rateLabels, false);
        // «Качество текстур» (fx::TextureQuality): Низкое / Среднее / Высокое, the textures' resolution at the next
        // start (marked only when the choice differs from the stored one).
        const bool restart = Known(app::setting::texture_quality) && AppSettings().is_object() &&
                             AppSettings().contains(app::setting::texture_quality) &&
                             draft_[app::setting::texture_quality] != AppSettings()[app::setting::texture_quality];
        Segment(bindings, graphics, app::setting::texture_quality, "settings.texture_quality", {0, 1, 2},
                {bindings.Text("settings.texture_low"), bindings.Text("settings.texture_medium"),
                 bindings.Text("settings.texture_high")},
                restart);
        CacheRow(bindings, graphics);
        TextureProcessing(bindings, graphics);
        return {sound, graphics};
    }

    std::vector<Column> DebugPage(Bindings &bindings)
    {
        // «Отладка», only when a debuggable app reports single fx overrides (app::debug): rows to
        // bisect rendering problems on a phone, applied with «Готово» like the preset. Development only, so the
        // texts are not translated.
        Column debug{"Отладка эффектов"};
        static constexpr const char *onOff[] = {"пресет", "выкл", "вкл"};
        // The renderer bench (renderer bench.hpp): «Готово» asks for one run, which starts once the menus are closed.
        if (Known(app::debug::bench.key))
            DebugSegment(bindings, debug, app::debug::bench, "Бенчмарк", {"выкл", "полный", "быстрый"});
        DebugSegment(bindings, debug, app::debug::fx_msaa, "MSAA", {"пресет", "выкл", "2x", "4x"});
        DebugSegment(bindings, debug, app::debug::fx_bloom, "Свечение (bloom)", onOff);
        DebugSegment(bindings, debug, app::debug::fx_grade, "Цветокоррекция", {"пресет", "выкл", "Карибы", "Пасмурно"});
        DebugSegment(bindings, debug, app::debug::fx_dither, "Дизеринг", onOff);
        DebugSegment(bindings, debug, app::debug::fx_shadows, "Тени солнца", {"пресет", "выкл", "низк", "мягк"});
        DebugSegment(bindings, debug, app::debug::fx_sea_band_limit, "Море: мягкая даль", onOff);
        DebugSegment(bindings, debug, app::debug::fx_sea_soft_edges, "Море: мягкие края", onOff);
        DebugSegment(bindings, debug, app::debug::fx_sea_specular, "Море: блик солнца", onOff);
        DebugSegment(bindings, debug, app::debug::fx_light_per_pixel, "Освещение попиксельно", onOff);
        DebugSegment(bindings, debug, app::debug::fx_light_point, "Точечные огни", onOff);
        DebugSegment(bindings, debug, app::debug::fx_light_specular, "Блики", onOff);
        DebugSegment(bindings, debug, app::debug::fx_light_normal_map, "Карты нормалей", onOff);
        DebugSegment(bindings, debug, app::debug::fx_alpha_to_coverage, "Alpha to coverage", onOff);
        DebugSegment(bindings, debug, app::debug::fx_anisotropy, "Анизотропия", {"пресет", "выкл", "4x", "8x"});
        DebugSegment(bindings, debug, app::debug::fx_ao_volume, "Затенение (объём)",
                     {"пресет", "выкл", "игра", "умер", "сильн"});
        // FSR 1 and SMAA: the scene's internal height (native on displays not taller),
        // the post AA, RCAS;
        // «Замер GPU» logs GPU timer averages, which «Поделиться логом» carries out.
        if (Known(app::debug::fx_render_height.key))
        {
            DebugSegment(bindings, debug, app::debug::fx_render_height, "Высота сцены",
                         {"пресет", "720", "900", "1000", "1100", "экран"});
            DebugSegment(bindings, debug, app::debug::fx_smaa, "SMAA", onOff);
            DebugSegment(bindings, debug, app::debug::fx_sharpen, "Резкость (RCAS)", onOff);
            DebugSegment(bindings, debug, app::debug::fx_gpu_profile, "Замер GPU", {"выкл", "вкл"});
        }
        // Far LODs of islands, ships and outdoor locations: the pixels a simplified level may differ by
        // (src/libs/geometry/far_lod), in tenths of a pixel; «выкл» draws every model whole.
        if (Known(app::debug::fx_far_lod.key))
            DebugSegment(bindings, debug, app::debug::fx_far_lod, "Дальние LOD", {"пресет", "выкл", "1 px", "2 px"});
        // The lamps' relight of locations: how many lamps cast shadows, the
        // shadow filter, how much brighter characters indoors are than the room, the lamp views.
        if (Known(app::debug::fx_lamp_shadows.key))
        {
            DebugSegment(bindings, debug, app::debug::fx_lamp_shadows, "Тени ламп", {"пресет", "выкл", "8", "16"});
            DebugSegment(bindings, debug, app::debug::fx_lamp_soft, "Фильтр теней ламп", {"пресет", "3x3", "5x5"});
            DebugSegment(bindings, debug, app::debug::fx_lamp_model_lift, "Персонажи ярче комнаты",
                         {"пресет", "0%", "20%", "50%", "100%"});
        }
        // The share of a lamp's own colour in the light it adds to baked surfaces.
        if (Known(app::debug::fx_lamp_tint.key))
            DebugSegment(bindings, debug, app::debug::fx_lamp_tint, "Цвет ламп на стенах",
                         {"пресет", "0%", "25%", "50%", "100%"});
        // The one falloff of every light (fx/light_falloff.hpp): how far it reaches, its soft core, its level.
        if (Known(app::debug::fx_light_reach.key))
        {
            DebugSegment(bindings, debug, app::debug::fx_light_reach, "Огни: дальность",
                         {"пресет", "100%", "160%", "220%", "300%"});
            DebugSegment(bindings, debug, app::debug::fx_light_core, "Огни: мягкость у огня",
                         {"пресет", "10 см", "40 см", "80 см"});
            DebugSegment(bindings, debug, app::debug::fx_light_level, "Огни: яркость",
                         {"пресет", "50%", "70%", "85%", "100%"});
        }
        // Sunlight through the sails: its strength.
        if (Known(app::debug::fx_sail_light.key))
            DebugSegment(bindings, debug, app::debug::fx_sail_light, "Свет сквозь паруса",
                         {"пресет", "выкл", "100%", "200%", "300%"});
        // Our town lightmaps for the live sun (effects/town_light.hpp) against the game's.
        if (Known(app::debug::fx_town_light.key))
            DebugSegment(bindings, debug, app::debug::fx_town_light, "Свет городов", {"пресет", "игра", "наш"});
        // The town walls relit per pixel.
        if (Known(app::debug::fx_town_walls.key))
            DebugSegment(bindings, debug, app::debug::fx_town_walls, "Стены городов", {"пресет", "игра", "наши"});
        // Gloss maps (Высокая): per-texel highlights of the sun and lamps, metal in its own colour.
        if (Known(app::debug::fx_gloss.key))
        {
            DebugSegment(bindings, debug, app::debug::fx_gloss, "Блеск", onOff);
            DebugSegment(bindings, debug, app::debug::fx_gloss_strength, "Сила блеска",
                         {"пресет", "50%", "100%", "200%", "300%"});
        }
        // The characters' relief (feat/char-relief): off, normal maps, with the parallax; its strength.
        if (Known(app::debug::fx_character_relief.key))
        {
            DebugSegment(bindings, debug, app::debug::fx_character_relief, "Рельеф персонажей",
                         {"пресет", "выкл", "нормали", "параллакс"});
            DebugSegment(bindings, debug, app::debug::fx_character_relief_strength, "Сила рельефа персонажей",
                         {"пресет", "25%", "50%", "75%", "100%"});
        }
        // The hulls' wakes.
        if (Known(app::debug::fx_sea_wakes.key))
            DebugSegment(bindings, debug, app::debug::fx_sea_wakes, "Море: следы кораблей", onOff);
        // The renderer's thread, its frame overlap and the graphics API (read at the game's start).
        if (Known(app::debug::render_backend.key))
        {
            DebugSegment(bindings, debug, app::debug::render_backend, "Графический API", {"авто", "GLES", "Vulkan"});
            DebugSegment(bindings, debug, app::debug::render_thread, "Поток рендера", onOff);
            DebugSegment(bindings, debug, app::debug::render_overlap, "Кадр вперёд", {"по кадру", "никогда", "всегда"});
        }
        return {debug};
    }

    // «Обработка текстур» (fx::TextureProcessing): what the phone makes from the game's textures in the background,
    // three switches applied at once with «Готово» whatever the preset. «Параллакс» is made with the normal maps and
    // drawn through them: while «Карты нормалей» is off it is disabled with its stored value, and a tap says why.
    // «Апскейл текстур ×2» works where the renderer reports the device can show it (channel "renderCaps"); elsewhere
    // disabled with why.
    void TextureProcessing(Bindings &bindings, Column &column)
    {
        column.rows.push_back(Json{{"kind", "heading"},
                                   {"id", "texture-processing"},
                                   {"label", bindings.Text("settings.texture_processing")}});
        Toggle(bindings, column, app::setting::texture_normal_maps, "settings.texture_normal_maps");
        Toggle(bindings, column, app::setting::texture_height_maps, "settings.texture_height_maps");
        if (Known(app::setting::texture_height_maps) && Known(app::setting::texture_normal_maps) &&
            draft_[app::setting::texture_normal_maps].get<int>() == 0)
        {
            column.rows.back()["disabled"] = true;
            column.rows.back()["reason"] = bindings.Text("settings.texture_height_maps_need_normals");
        }
        Toggle(bindings, column, app::setting::texture_upscale, "settings.texture_upscale");
        const std::string upscale = RenderCaps().value("upscale", std::string("unknown"));
        if (Known(app::setting::texture_upscale) && upscale != "supported")
        {
            column.rows.back()["disabled"] = true;
            column.rows.back()["reason"] = bindings.Text(
                upscale == "unsupported" ? "settings.texture_upscale_unsupported" : "settings.texture_upscale_unknown");
        }
    }

    // «Кэш»: what the game derived and can make again (the host's cache_bytes: -1 while it measures) and «Очистить»,
    // which the host does at the next start (cache_clear_pending), when nothing of the game holds those files. Both
    // are facts the host reports while the screen is open, not part of the draft.
    void CacheRow(Bindings &bindings, Column &column)
    {
        const Json &facts = AppSettings();
        if (!facts.is_object() || !facts.contains(app::host_fact::cache_bytes) ||
            !facts[app::host_fact::cache_bytes].is_number_integer())
            return; // a host without the cache report: no row
        const long long bytes = facts[app::host_fact::cache_bytes].get<long long>();
        const bool pending = facts.value(app::host_fact::cache_clear_pending, false);
        // MB below a whole GB: «118,0 МБ», not «0,1 ГБ».
        const std::string size =
            bytes < 0 ? bindings.Text("settings.cache_measuring")
                      : Size(bindings, static_cast<double>(bytes), bytes < kGiB ? 0.0 : static_cast<double>(bytes));
        // Pending: the row says so in its normal colours and has no button any more; «Очистить» is pressed and held
        // (destructive actions: the c-setting button confirms by a hold).
        column.rows.push_back(
            Json{{"kind", "button"},
                 {"id", "app-cache-clear"},
                 {"label", Fill(bindings.Text("settings.cache"), {size})},
                 {"hint", pending ? bindings.Text("settings.cache_pending") : std::string()},
                 {"display", pending ? std::string() : bindings.Text("settings.cache_clear")},
                 {"disabled", !pending && bytes >= 0 && bytes < (1 << 20)}, // under a megabyte: nothing to clear
                 {"reason", bindings.Text("settings.cache_empty")}});
        bindings.On("app-cache-clear", "", [&bindings](const Event &) {
            if (!AppSettings().value(app::host_fact::cache_clear_pending, false))
                bindings.SendHost(app::host_action::app_cache_clear, ""); // the host reports the pending clear
        });
    }

    // The debug page exists only when the host reports debug overrides (a debuggable app).
    int TabCount() const
    {
        return static_cast<int>(std::size(kTabs)) + (Known(app::debug::fx_msaa.key) ? 1 : 0);
    }

    // «Чувствительность»: one slider for the game's two mouse sensitivities (the touch camera drag goes through
    // them): it shows the vertical one and sets both to the same position.
    void Sensitivity(const Admission &admitted, Bindings &bindings, Column &column)
    {
        const Widget *vertical = admitted.One("VMOUSE_SENSITIVITY_SLIDE");
        const Widget *horizontal = admitted.One("HMOUSE_SENSITIVITY_SLIDE");
        if (!vertical || !horizontal)
            return;
        const auto percent = [&bindings](const Widget &slider) {
            const int span = std::max(1, slider.max - slider.min);
            const int value = Amount(bindings, slider.value, slider.node.c_str());
            return std::clamp(static_cast<int>(std::lround(100.0 * (value - slider.min) / span)), 0, 100);
        };
        const auto position = [](const Widget &slider, double share) {
            const int span = std::max(1, slider.max - slider.min);
            const int step = std::max(1, slider.step);
            const double wanted = slider.min + share * span;
            const int snapped = slider.min + static_cast<int>(std::lround((wanted - slider.min) / step)) * step;
            return std::to_string(std::clamp(snapped, slider.min, slider.max));
        };
        bindings.Ignore("camera-sensitivity", "change"); // drag progress: the position goes out on release
        bindings.On("camera-sensitivity", "commit", [&bindings, vertical, horizontal, position](const Event &event) {
            double share = 0.0;
            if (!EventNumber(bindings, event, share))
                return;
            share = std::clamp(share, 0.0, 100.0) / 100.0;
            bindings.Send(*vertical, "set", position(*vertical, share));
            bindings.Send(*horizontal, "set", position(*horizontal, share));
        });
        column.rows.push_back(Json{{"kind", "slider"},
                                   {"id", "camera-sensitivity"},
                                   {"label", bindings.Text("settings.camera_sensitivity")},
                                   {"value", percent(*vertical)},
                                   {"disabled", !vertical->Offers("set") || !horizontal->Offers("set")},
                                   {"reason", vertical->unavailable_reason}});
    }

    // A renderer option the host reported; without the report the row stays visible but
    // disabled, and a tap says why.
    bool Known(const char *key) const
    {
        return draft_.is_object() && draft_.contains(key) && draft_[key].is_number_integer();
    }

    // `restart`: the option applies at the next start (marked so); `stack`: the options go
    // under the label in a full-width segment.
    void Segment(Bindings &bindings, Column &column, const char *key, const char *label, std::vector<int> values,
                 Json options, bool restart = true, bool stack = true)
    {
        int selected = 0;
        for (size_t i = 0; i < values.size(); ++i)
            if (Known(key) && draft_[key].get<int>() == values[i])
                selected = static_cast<int>(i);
        const std::string id = std::string("app-") + key;
        column.rows.push_back(Json{{"kind", "segment"},
                                   {"id", id},
                                   {"label", bindings.Text(label)},
                                   {"options", options},
                                   {"selected", selected},
                                   {"stack", stack},
                                   {"restart", restart},
                                   {"disabled", !Known(key)},
                                   {"reason", Known(key) ? std::string() : bindings.Text("settings.app_unavailable")}});
        bindings.On(id, "change", [this, &bindings, key, values](const Event &event) {
            size_t index = 0;
            if (!EventNumber(bindings, event, index))
                return;
            if (index < values.size() && Known(key))
                draft_[key] = values[index];
            bindings.Rebuild();
        });
    }

    // An app option that is on (1) or off (0) as a toggle row, a draft applied with «Готово».
    void Toggle(Bindings &bindings, Column &column, const char *key, const char *label)
    {
        const std::string id = std::string("app-") + key;
        column.rows.push_back(Json{{"kind", "toggle"},
                                   {"id", id},
                                   {"label", bindings.Text(label)},
                                   {"on", Known(key) && draft_[key].get<int>() != 0},
                                   {"disabled", !Known(key)},
                                   {"reason", Known(key) ? std::string() : bindings.Text("settings.app_unavailable")}});
        bindings.On(id, "change", [this, &bindings, key](const Event &event) {
            if (Known(key))
                draft_[key] = event.value == "1" ? 1 : 0;
            bindings.Rebuild();
        });
    }

    // A debug override row (raw, untranslated label), one label per offered value; applied with «Готово» without a
    // restart.
    template <std::size_t N>
    void DebugSegment(Bindings &bindings, Column &column, const app::debug::Override<N> &setting, const char *label,
                      const char *const (&options)[N])
    {
        const char *key = setting.key;
        int selected = 0;
        for (size_t i = 0; i < N; ++i)
            if (draft_[key].get<int>() == setting.values[i])
                selected = static_cast<int>(i);
        const std::string id = std::string("app-") + key;
        column.rows.push_back(Json{{"kind", "segment"},
                                   {"id", id},
                                   {"label", label},
                                   {"options", options},
                                   {"selected", selected},
                                   {"stack", true},
                                   {"restart", false},
                                   {"disabled", false},
                                   {"reason", ""}});
        bindings.On(id, "change", [this, &bindings, &setting](const Event &event) {
            size_t index = 0;
            if (!EventNumber(bindings, event, index))
                return;
            if (index < N)
                draft_[setting.key] = setting.values[index];
            bindings.Rebuild();
        });
    }

    int tab_ = 0;
    Json draft_; // the renderer options as the player set them on this opening
};

} // namespace

std::unique_ptr<Screen> CreateSettingsScreen()
{
    return std::make_unique<SettingsScreen>();
}

std::unique_ptr<StateConsumer> CreateAppSettingsConsumer()
{
    return std::make_unique<AppSettingsConsumer>();
}

std::unique_ptr<StateConsumer> CreateRenderCapsConsumer()
{
    return std::make_unique<RenderCapsConsumer>();
}

} // namespace storm::ui::screens
