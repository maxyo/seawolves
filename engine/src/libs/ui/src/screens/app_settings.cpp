// The application's settings for the settings screen (app_settings.h), and the consumers of the host's settings
// ("appSettings") and of the renderer's device caps ("renderCaps"), which rebuild an open screen.
#include "app_settings.h"

#include "controls.h"
#include "app_ui_names.hpp"

#include <platform/display_options.hpp>
#include <platform/pirate_frequency.hpp>

#include <stdexcept>

namespace storm::ui::screens
{

namespace
{

class RenderCapsConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "renderCaps";
    }
    void OnState(const Json &, Router &router) override
    {
        router.RebuildActive(); // «Апскейл текстур ×2» of an open settings screen follows the device's report
    }
};

class AppSettingsConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return app::channel::appSettings;
    }
    void OnState(const Json &, Router &router) override
    {
        router.RebuildActive(); // the cache facts of an open settings screen are the host's latest
    }
};

const Json &Stored(const Bindings &bindings)
{
    return bindings.State(app::channel::appSettings);
}

// What the renderer reports about the device: {"upscale": "supported" | "unsupported" | "unknown"}, whether
// «Апскейл текстур ×2» can work; "texture_processing": false for a game that ships its own texture maps.
Json Caps(const Bindings &bindings)
{
    const Json &caps = bindings.State("renderCaps");
    return caps.is_object() ? caps : Json::object();
}

// The settings only: what the host reports beside them (the device's options, the cache, the automatic preset) does
// not go back.
Json Choices(Json settings)
{
    for (const char *fact : app::host_fact::all)
        settings.erase(fact);
    return settings;
}

// The table of one opening: every option's row from the draft (the host's settings until the player changes one).
class Table
{
  public:
    explicit Table(Bindings &bindings)
        : bindings_(bindings), stored_(Stored(bindings)),
          draft_(bindings.Shared().settings_draft.is_object() ? bindings.Shared().settings_draft : stored_)
    {
    }

    Json Build()
    {
        Json groups = Json::array();
        groups.push_back(Group("map", "", MapOptions()));
        groups.push_back(Group("screen", "", ScreenOptions()));
        groups.push_back(Group("graphics", "", GraphicsOptions()));
        if (Caps(bindings_).value("texture_processing", true))
            groups.push_back(Group("textures", bindings_.Text("settings.texture_processing"), TextureOptions()));
        // Only a debuggable application reports single fx overrides; development only, so not translated.
        if (Known(app::debug::fx_msaa.key))
        {
            Json debug = Group("debug", "Отладка эффектов", DebugOptions());
            debug["tab"] = "Отладка";
            groups.push_back(debug);
        }
        Json table{{"groups", groups}};
        if (Json cache = Cache(); !cache.is_null())
            table["cache"] = cache;
        return table;
    }

  private:
    static Json Group(const char *id, const std::string &title, Json options)
    {
        return Json{{"id", id}, {"title", title}, {"tab", ""}, {"options", std::move(options)}};
    }

    // An option the host reported; without the report the row stays visible but disabled, and a tap says why.
    bool Known(const char *key) const
    {
        return draft_.is_object() && draft_.contains(key) && draft_[key].is_number_integer();
    }

    // A choice of `values` named `options`. `restart`: it applies at the next start (marked while the draft differs
    // from what the host has).
    Json Segment(const char *key, const std::string &label, std::vector<int> values, Json options,
                 bool restart = false) const
    {
        int selected = 0;
        for (size_t i = 0; i < values.size(); ++i)
            if (Known(key) && draft_[key].get<int>() == values[i])
                selected = static_cast<int>(i);
        const bool changed = Known(key) && stored_.is_object() && stored_.contains(key) && draft_[key] != stored_[key];
        return Json{{"key", key},
                    {"kind", "segment"},
                    {"label", label},
                    {"options", std::move(options)},
                    {"values", std::move(values)},
                    {"selected", selected},
                    {"on", false},
                    {"stack", true},
                    {"restart", restart && changed},
                    {"disabled", !Known(key)},
                    {"reason", Known(key) ? std::string() : bindings_.Text("settings.app_unavailable")},
                    {"hint", ""}};
    }

    // An option that is on (1) or off (0).
    Json Toggle(const char *key, const char *label) const
    {
        return Json{{"key", key},
                    {"kind", "toggle"},
                    {"label", bindings_.Text(label)},
                    {"options", Json::array()},
                    {"values", {0, 1}},
                    {"selected", 0},
                    {"on", Known(key) && draft_[key].get<int>() != 0},
                    {"stack", false},
                    {"restart", false},
                    {"disabled", !Known(key)},
                    {"reason", Known(key) ? std::string() : bindings_.Text("settings.app_unavailable")},
                    {"hint", ""}};
    }

    static void Disable(Json &option, std::string reason)
    {
        option["disabled"] = true;
        option["reason"] = std::move(reason);
    }

    // The values a host fact lists the device can show (`allowed`: of the contract's steps, when given).
    std::vector<int> Offered(const char *fact, std::span<const int> allowed = {}) const
    {
        std::vector<int> values;
        if (draft_.is_object() && draft_.contains(fact) && draft_[fact].is_array())
            for (const Json &value : draft_[fact])
                if (value.is_number_integer() && (allowed.empty() || storm::android::OneOf(allowed, value.get<int>())))
                    values.push_back(value.get<int>());
        return values;
    }

    // «Частота пиратов на карте» (platform/pirate_frequency.hpp): an app setting the game's encounter script asks for
    // at every random encounter, so it counts from the next one after «Готово».
    Json MapOptions() const
    {
        return Json::array({Segment(
            app::setting::pirate_frequency, bindings_.Text("settings.pirate_frequency"),
            std::vector<int>(std::begin(storm::game::kPirateFrequencies), std::end(storm::game::kPirateFrequencies)),
            {bindings_.Text("settings.pirates_rare"), bindings_.Text("settings.pirates_sometimes"),
             bindings_.Text("settings.pirates_often")})});
    }

    // «Экран и касания» (platform/display_options.hpp), applied with «Готово». The UI sizes are the ones the host
    // found the display can fit (ui_scale_options: the designed layout height left).
    Json ScreenOptions() const
    {
        Json options = Json::array();
        const std::vector<int> scales = Offered(app::host_fact::ui_scale_options, app::step::ui_scale.values);
        Json scaleLabels = Json::array(), transparencies = Json::array();
        for (const int scale : scales)
            scaleLabels.push_back(std::to_string(scale) + "%");
        for (const int share : app::step::hud_transparency.values)
            transparencies.push_back(share == 0 ? bindings_.Text("settings.transparency_none")
                                                : std::to_string(share) + "%");
        Json scale = Segment(app::setting::ui_scale, bindings_.Text("settings.ui_scale"), scales, scaleLabels);
        // A display with room for no larger size than 100 %: the row stays, disabled, and a tap says why.
        if (scales.size() < 2 && Known(app::setting::ui_scale))
            Disable(scale, bindings_.Text("settings.ui_scale_no_room"));
        options.push_back(scale);
        options.push_back(Segment(
            app::setting::hud_transparency, bindings_.Text("settings.hud_transparency"),
            std::vector<int>(app::step::hud_transparency.values.begin(), app::step::hud_transparency.values.end()),
            transparencies));
        options.push_back(Toggle(app::setting::left_handed, "settings.left_handed")); // the HUD mirrored
        options.push_back(Toggle(app::setting::haptics, "settings.haptics")); // hits, broadsides (android_haptics.hpp)
        return options;
    }

    // The graphics choice is the preset; it also decides the game's grass and sea detail (renderer
    // scene_detail.hpp). The graphics API in use is shown in «О программе».
    Json GraphicsOptions() const
    {
        Json options = Json::array();
        Json preset = Segment(app::setting::fx_preset, bindings_.Text("settings.graphics"), {0, 1, 2},
                              {bindings_.Text("settings.fx_low"), bindings_.Text("settings.fx_medium"),
                               bindings_.Text("settings.fx_high")});
        // The automatic preset's rung below «Низкая»: «Низкая» with the scene at a lower height, which only the
        // measurement chooses (fx_auto_render_height, while that choice stands); its height under the row.
        if (Known(app::host_fact::fx_auto_render_height) &&
            draft_[app::host_fact::fx_auto_render_height].get<int>() > 0 && Known(app::setting::fx_preset) &&
            draft_[app::setting::fx_preset].get<int>() == 0)
            preset["hint"] = Fill(bindings_.Text("settings.fx_scene_height"),
                                  {std::to_string(draft_[app::host_fact::fx_auto_render_height].get<int>())});
        options.push_back(preset);
        // «Частота кадров»: the rates the host found the display can show (frame_rate_options: whole divisors of its
        // refresh rates), applied at once with «Готово» (the engine's frame pacing).
        const std::vector<int> rates = Offered(app::host_fact::frame_rate_options);
        Json rateLabels = Json::array();
        for (const int rate : rates)
            rateLabels.push_back(std::to_string(rate));
        options.push_back(Segment(app::setting::frame_rate, bindings_.Text("settings.frame_rate"), rates, rateLabels));
        // «Качество текстур» (fx::TextureQuality): the textures' resolution at the next start.
        options.push_back(Segment(app::setting::texture_quality, bindings_.Text("settings.texture_quality"), {0, 1, 2},
                                  {bindings_.Text("settings.texture_low"), bindings_.Text("settings.texture_medium"),
                                   bindings_.Text("settings.texture_high")},
                                  true));
        return options;
    }

    // «Обработка текстур» (fx::TextureProcessing): what the phone makes from the game's textures in the background,
    // applied at once with «Готово» whatever the preset. «Параллакс» is made with the normal maps and drawn through
    // them: while «Карты нормалей» is off it is disabled with its stored value. «Апскейл текстур ×2» works where the
    // renderer reports the device can show it.
    Json TextureOptions() const
    {
        Json options = Json::array();
        options.push_back(Toggle(app::setting::texture_normal_maps, "settings.texture_normal_maps"));
        Json height = Toggle(app::setting::texture_height_maps, "settings.texture_height_maps");
        if (Known(app::setting::texture_height_maps) && Known(app::setting::texture_normal_maps) &&
            draft_[app::setting::texture_normal_maps].get<int>() == 0)
            Disable(height, bindings_.Text("settings.texture_height_maps_need_normals"));
        options.push_back(height);
        Json upscale = Toggle(app::setting::texture_upscale, "settings.texture_upscale");
        const std::string device = Caps(bindings_).value("upscale", std::string("unknown"));
        if (Known(app::setting::texture_upscale) && device != "supported")
            Disable(upscale, bindings_.Text(device == "unsupported" ? "settings.texture_upscale_unsupported"
                                                                    : "settings.texture_upscale_unknown"));
        options.push_back(upscale);
        return options;
    }

    // A debug override (raw, untranslated label), one label per offered value; applied with «Готово».
    template <std::size_t N>
    Json Debug(const app::debug::Override<N> &setting, const char *label, const char *const (&names)[N]) const
    {
        Json option = Segment(setting.key, label, std::vector<int>(setting.values.begin(), setting.values.end()),
                              Json(std::vector<std::string>(std::begin(names), std::end(names))));
        option["disabled"] = false;
        option["reason"] = "";
        return option;
    }

    // Rows to bisect rendering problems on a phone; a group of rows only when the host reports its lead key.
    Json DebugOptions() const
    {
        static constexpr const char *onOff[] = {"пресет", "выкл", "вкл"};
        Json rows = Json::array();
        const auto add = [&rows](Json row) { rows.push_back(std::move(row)); };
        // The renderer bench (renderer bench.hpp): «Готово» asks for one run, which starts once the menus are closed.
        if (Known(app::debug::bench.key))
            add(Debug(app::debug::bench, "Бенчмарк", {"выкл", "полный", "быстрый"}));
        add(Debug(app::debug::fx_msaa, "MSAA", {"пресет", "выкл", "2x", "4x"}));
        add(Debug(app::debug::fx_bloom, "Свечение (bloom)", onOff));
        add(Debug(app::debug::fx_grade, "Цветокоррекция", {"пресет", "выкл", "Карибы", "Пасмурно"}));
        add(Debug(app::debug::fx_dither, "Дизеринг", onOff));
        add(Debug(app::debug::fx_shadows, "Тени солнца", {"пресет", "выкл", "низк", "мягк"}));
        add(Debug(app::debug::fx_sea_band_limit, "Море: мягкая даль", onOff));
        add(Debug(app::debug::fx_sea_soft_edges, "Море: мягкие края", onOff));
        add(Debug(app::debug::fx_sea_specular, "Море: блик солнца", onOff));
        add(Debug(app::debug::fx_light_per_pixel, "Освещение попиксельно", onOff));
        add(Debug(app::debug::fx_light_point, "Точечные огни", onOff));
        add(Debug(app::debug::fx_light_specular, "Блики", onOff));
        add(Debug(app::debug::fx_light_normal_map, "Карты нормалей", onOff));
        add(Debug(app::debug::fx_alpha_to_coverage, "Alpha to coverage", onOff));
        add(Debug(app::debug::fx_anisotropy, "Анизотропия", {"пресет", "выкл", "4x", "8x"}));
        add(Debug(app::debug::fx_ao_volume, "Затенение (объём)", {"пресет", "выкл", "игра", "умер", "сильн"}));
        // FSR 1 and SMAA: the scene's internal height (native on displays not taller), the post AA, RCAS; «Замер GPU»
        // logs GPU timer averages, which «Поделиться логом» carries out.
        if (Known(app::debug::fx_render_height.key))
        {
            add(Debug(app::debug::fx_render_height, "Высота сцены", {"пресет", "720", "900", "1000", "1100", "экран"}));
            add(Debug(app::debug::fx_smaa, "SMAA", onOff));
            add(Debug(app::debug::fx_sharpen, "Резкость (RCAS)", onOff));
            add(Debug(app::debug::fx_gpu_profile, "Замер GPU", {"выкл", "вкл"}));
        }
        // Far LODs of islands, ships and outdoor locations: the pixels a simplified level may differ by
        // (src/libs/geometry/far_lod), in tenths of a pixel; «выкл» draws every model whole.
        if (Known(app::debug::fx_far_lod.key))
            add(Debug(app::debug::fx_far_lod, "Дальние LOD", {"пресет", "выкл", "1 px", "2 px"}));
        // The lamps' relight of locations: how many lamps cast shadows, the shadow filter, how much brighter
        // characters indoors are than the room.
        if (Known(app::debug::fx_lamp_shadows.key))
        {
            add(Debug(app::debug::fx_lamp_shadows, "Тени ламп", {"пресет", "выкл", "8", "16"}));
            add(Debug(app::debug::fx_lamp_soft, "Фильтр теней ламп", {"пресет", "3x3", "5x5"}));
            add(Debug(app::debug::fx_lamp_model_lift, "Персонажи ярче комнаты",
                      {"пресет", "0%", "20%", "50%", "100%"}));
        }
        // The share of a lamp's own colour in the light it adds to baked surfaces.
        if (Known(app::debug::fx_lamp_tint.key))
            add(Debug(app::debug::fx_lamp_tint, "Цвет ламп на стенах", {"пресет", "0%", "25%", "50%", "100%"}));
        // The one falloff of every light (fx/light_falloff.hpp): how far it reaches, its soft core, its level.
        if (Known(app::debug::fx_light_reach.key))
        {
            add(Debug(app::debug::fx_light_reach, "Огни: дальность", {"пресет", "100%", "160%", "220%", "300%"}));
            add(Debug(app::debug::fx_light_core, "Огни: мягкость у огня", {"пресет", "10 см", "40 см", "80 см"}));
            add(Debug(app::debug::fx_light_level, "Огни: яркость", {"пресет", "50%", "70%", "85%", "100%"}));
        }
        // Sunlight through the sails: its strength.
        if (Known(app::debug::fx_sail_light.key))
            add(Debug(app::debug::fx_sail_light, "Свет сквозь паруса", {"пресет", "выкл", "100%", "200%", "300%"}));
        // Our town lightmaps for the live sun (effects/town_light.hpp) against the game's.
        if (Known(app::debug::fx_town_light.key))
            add(Debug(app::debug::fx_town_light, "Свет городов", {"пресет", "игра", "наш"}));
        // The town walls relit per pixel.
        if (Known(app::debug::fx_town_walls.key))
            add(Debug(app::debug::fx_town_walls, "Стены городов", {"пресет", "игра", "наши"}));
        // Gloss maps (Высокая): per-texel highlights of the sun and lamps, metal in its own colour.
        if (Known(app::debug::fx_gloss.key))
        {
            add(Debug(app::debug::fx_gloss, "Блеск", onOff));
            add(Debug(app::debug::fx_gloss_strength, "Сила блеска", {"пресет", "50%", "100%", "200%", "300%"}));
        }
        // The characters' relief: off, normal maps, with the parallax; its strength.
        if (Known(app::debug::fx_character_relief.key))
        {
            add(Debug(app::debug::fx_character_relief, "Рельеф персонажей",
                      {"пресет", "выкл", "нормали", "параллакс"}));
            add(Debug(app::debug::fx_character_relief_strength, "Сила рельефа персонажей",
                      {"пресет", "25%", "50%", "75%", "100%"}));
        }
        // The hulls' wakes.
        if (Known(app::debug::fx_sea_wakes.key))
            add(Debug(app::debug::fx_sea_wakes, "Море: следы кораблей", onOff));
        // The renderer's thread, its frame overlap and the graphics API (read at the game's start).
        if (Known(app::debug::render_backend.key))
        {
            add(Debug(app::debug::render_backend, "Графический API", {"авто", "GLES", "Vulkan"}));
            add(Debug(app::debug::render_thread, "Поток рендера", onOff));
            add(Debug(app::debug::render_overlap, "Кадр вперёд", {"по кадру", "никогда", "всегда"}));
        }
        return rows;
    }

    // «Кэш»: what the game derived and can make again (the host's cache_bytes: -1 while it measures) and «Очистить»,
    // which the host does at the next start (cache_clear_pending), when nothing of the game holds those files. Both
    // are facts the host reports while the screen is open, not part of the draft.
    Json Cache() const
    {
        if (!stored_.is_object() || !stored_.contains(app::host_fact::cache_bytes) ||
            !stored_[app::host_fact::cache_bytes].is_number_integer())
            return Json(); // a host without the cache report: no row
        const long long bytes = stored_[app::host_fact::cache_bytes].get<long long>();
        const bool pending = stored_.value(app::host_fact::cache_clear_pending, false);
        // MB below a whole GB: «118,0 МБ», not «0,1 ГБ».
        const std::string size =
            bytes < 0 ? bindings_.Text("settings.cache_measuring")
                      : Size(bindings_, static_cast<double>(bytes), bytes < kGiB ? 0.0 : static_cast<double>(bytes));
        // Pending: the row says so in its normal colours and has no button any more.
        return Json{{"label", Fill(bindings_.Text("settings.cache"), {size})},
                    {"hint", pending ? bindings_.Text("settings.cache_pending") : std::string()},
                    {"display", pending ? std::string() : bindings_.Text("settings.cache_clear")},
                    {"disabled", !pending && bytes >= 0 && bytes < (1 << 20)}, // under a megabyte: nothing to clear
                    {"reason", bindings_.Text("settings.cache_empty")},
                    {"clear", !pending}};
    }

    Bindings &bindings_;
    const Json &stored_;
    const Json draft_;
};

} // namespace

Json AppOptions(Bindings &bindings)
{
    return Table(bindings).Build();
}

void OpenAppSettings(Bindings &bindings)
{
    bindings.Shared().settings_draft = Json();
    bindings.Shared().settings_sent = Json();
    // The cache grows while the game runs: measured again at each opening, by a host that reports a cache at all.
    if (Stored(bindings).contains(app::host_fact::cache_bytes))
        bindings.SendHost(app::host_action::app_cache_measure, "");
}

void SetAppSetting(Bindings &bindings, const std::string &key, int value)
{
    const Json table = AppOptions(bindings);
    for (const Json &group : table["groups"])
        for (const Json &option : group["options"])
            if (option["key"] == key)
            {
                if (option["disabled"].get<bool>())
                    throw std::runtime_error("the setting " + key + " cannot be set now");
                const std::vector<int> values = option["values"];
                if (std::find(values.begin(), values.end(), value) == values.end())
                    throw std::runtime_error("the setting " + key + " offers no value " + std::to_string(value));
                Json &draft = bindings.Shared().settings_draft;
                if (!draft.is_object())
                    draft = Stored(bindings);
                draft[key] = value;
                bindings.Rebuild();
                return;
            }
    throw std::runtime_error("no setting " + key + " is offered");
}

void KeepAppSettings(Bindings &bindings)
{
    SharedState &shared = bindings.Shared();
    const Json &stored = Stored(bindings);
    if (!shared.settings_draft.is_object() || !stored.is_object())
        return;
    const Json settings = Choices(shared.settings_draft);
    if (settings == (shared.settings_sent.is_null() ? Choices(stored) : shared.settings_sent))
        return;
    bindings.SendHost(app::host_action::app_settings, settings.dump());
    shared.settings_sent = settings;
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
