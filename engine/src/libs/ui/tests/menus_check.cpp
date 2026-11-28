// Host checks of the menu screens (the menus' script, menu_*.cpp): admission with loud rejection, the props
// that matter and every action mapping. Synthetic models only (tests/models).
#include "fakes.h"
#include "../src/screens/screen.h"
#include "app_ui_names.hpp"
#include "../tools/script_game.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>

using namespace storm::ui;
using namespace storm::ui::screens;

// The application's pack: the menus its scripts draw are checked on their script screens.
static std::string app_pack;

static Router &Scripted(Router &router)
{
    router.SetScriptScreens(ScriptGame::Get().Entries(app_pack));
    return router;
}

static void Without(InterfaceModel &model, const char *node)
{
    model.widgets.erase(
        std::remove_if(model.widgets.begin(), model.widgets.end(), [node](const Widget &w) { return w.node == node; }),
        model.widgets.end());
}

static void GameMenu(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    router.SetInterface(Load(models, "game-menu.json"));
    ExpectEqual(surface.props["line"], "Герой Альфа · Порт Альфа · 1 <common.month_10> 1654",
                "the game in progress above the actions");
    ExpectEqual(router.Using(), "game_menu", "pause menu is a ported screen");
    ExpectEqual(surface.tag, "s-game-menu", "pause menu mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty(), "every node of the pause menu is known");
    ExpectEqual(surface.props["resume"]["label"], "Продолжить", "resume is the game's button");
    Expect(surface.props["buttons"].size() == 6 && surface.props["buttons"][2]["id"] == "quick-save" &&
               surface.props["buttons"][3]["id"] == "quick-load" && surface.props["buttons"][5]["id"] == "exit",
           "grid: save, load, quick save, quick load, settings, exit (no disabled updates)");
    ExpectEqual(surface.props["buttons"][5]["label"], "<menu.exit>",
                "the exit tile reads «Выйти», not the game's program exit");
    Expect(surface.props["buttons"][2]["disabled"] == false &&
               surface.props["buttons"][2]["label"] == "<menu.quick_save>",
           "quick save offered where the game lets the player save");
    Expect(surface.props["buttons"][3]["disabled"] == true &&
               surface.props["buttons"][3]["reason"] == "<menu.no_quick_save>" &&
               surface.props["buttons"][3]["hold"] == true,
           "quick load without its record: disabled with the reason, held when offered");
    router.OnEvent({"quick-save", "click", ""});
    Expect(surface.commands.size() >= 2 && surface.commands[surface.commands.size() - 2] == "back" &&
               surface.Last() == "quick:save",
           "quick save: the menu resumes, then the engine presses the game's key");
    Expect(surface.props["confirm"].is_null(), "no question yet");
    router.OnEvent({"resume", "click", ""});
    ExpectEqual(surface.Last(), "back", "«Продолжить» resumes through the menu's cancel (exitCancel)");
    const char *const ids[][2] = {
        {"save", "MB_SAVE"}, {"load", "MB_LOAD"}, {"options", "MB_OPTIONS"}, {"exit", "MB_EXITGAME"}};
    for (const auto &[id, node] : ids)
    {
        router.OnEvent({id, "click", ""});
        ExpectEqual(surface.Last(), std::string("widget:game_menu\t") + node + "\t-1\tpress",
                    std::string(id) + " presses its button");
    }
    {
        InterfaceModel quick = Load(models, "game-menu.json");
        quick.opening = 95;
        quick.widgets.push_back(Widget{"ANDROID_QUICK_LOAD", "text", "Герой QuickSave 1"});
        router.SetInterface(quick);
        Expect(surface.props["buttons"][3]["disabled"] == false && surface.props["buttons"][3]["hold"] == true,
               "quick load offered while its record exists");
        router.OnEvent({"quick-load", "click", ""});
        Expect(surface.commands[surface.commands.size() - 2] == "back" && surface.Last() == "quick:load",
               "quick load: resume, then the game's key");
    }

    router.SetInterface(Load(models, "game-menu-exit.json"));
    const Json confirm = surface.props["confirm"];
    Expect(confirm["preset"] == "danger" && confirm["title"] == "<menu.exit_to_main_menu_question>" &&
               confirm["text"] == "" && confirm["yes"] == "<menu.exit>",
           "exit question: «Выйти в главное меню?», not the game's question of the program exit");
    const size_t before = surface.commands.size();
    router.OnEvent({"confirm-confirm", "click", ""});
    Expect(surface.commands.size() == before + 2 && surface.commands[before] == "host app-exit-to-main-menu " &&
               surface.commands[before + 1] == "widget:game_menu\tCONFIRM_EXIT_YES\t-1\tpress",
           "«Выйти» tells the host it leads to the main menu, then answers yes");
    router.OnEvent({"confirm", "dismiss", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tCONFIRM_EXIT_NO\t-1\tpress", "the scrim answers no");
    router.OnEvent({"confirm-cancel", "click", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tCONFIRM_EXIT_NO\t-1\tpress", "«Отмена» answers no");

    // A game whose game_menu has its own exit to the main menu (MB_EXITMENU → GameOverMainMenu): it is the
    // one exit, its program exit (MB_EXITGAME) is left to the main menu, no host restart.
    InterfaceModel own_exit = Load(models, "game-menu.json");
    own_exit.opening = 96;
    for (const Widget &widget : own_exit.widgets)
        if (widget.node == "MB_EXITGAME")
        {
            Widget toMenu = widget;
            toMenu.node = "MB_EXITMENU";
            toMenu.label = "Главное меню";
            own_exit.widgets.push_back(toMenu);
            break;
        }
    router.SetInterface(own_exit);
    Expect(surface.errors.empty() && surface.props["buttons"].size() == 6 &&
               surface.props["buttons"][5]["label"] == "Главное меню",
           "own exit to the main menu: the exit tile is the game's exit to the main menu");
    router.OnEvent({"exit", "click", ""});
    ExpectEqual(surface.Last(), "widget:game_menu\tMB_EXITMENU\t-1\tpress",
                "own exit to the main menu: exit presses MB_EXITMENU");

    // TEHO's game_menu.c greys out MB_RESUME during a boarding; its cancel still resumes there.
    InterfaceModel boarding = Load(models, "game-menu.json");
    boarding.opening = 97;
    for (Widget &widget : boarding.widgets)
        if (widget.node == "MB_RESUME")
            widget.enabled = false;
    router.SetInterface(boarding);
    Expect(surface.props["resume"]["disabled"] == false,
           "boarding: «Продолжить» stays available while the cancel resumes");
    router.OnEvent({"resume", "click", ""});
    ExpectEqual(surface.Last(), "back", "boarding: «Продолжить» takes the cancel route");
    for (Widget &widget : boarding.widgets)
        if (widget.node == "MB_RESUME")
            widget.context["resumeOnCancel"] = "0";
    boarding.opening = 98;
    const size_t sent = surface.commands.size();
    router.SetInterface(boarding);
    Expect(surface.props["resume"]["disabled"] == true,
           "no resume (Buttons.Resume.enable 0): «Продолжить» unavailable");
    router.OnEvent({"resume", "click", ""});
    Expect(surface.commands.size() == sent && surface.errors.empty(), "an unavailable «Продолжить» sends nothing");

    // A place where the game allows no saving (boarding, a battle, a no-save zone): the same
    // menu, «Сохранить» shown but disabled; a tap says why only when the game gives a reason.
    InterfaceModel no_save = Load(models, "game-menu.json");
    no_save.opening = 100;
    for (Widget &widget : no_save.widgets)
        if (widget.node == "MB_SAVE")
            widget.enabled = false, widget.actions.clear(), widget.unavailable_reason = "Сейчас нельзя сохраниться";
    router.SetInterface(no_save);
    {
        Json save;
        for (const Json &button : surface.props["buttons"])
            if (button["id"] == "save")
                save = button;
        Expect(surface.props["buttons"].size() == 6 && save["disabled"] == true &&
                   save["reason"] == "Сейчас нельзя сохраниться" && surface.props["buttons"][2]["disabled"] == true &&
                   surface.props["buttons"][2]["reason"] == "Сейчас нельзя сохраниться",
               "no-save zone: «Сохранить» and the quick save disabled with the game's reason");
    }

    InterfaceModel broken = Load(models, "game-menu.json");
    Without(broken, "MB_RESUME");
    broken.opening = 99;
    router.SetInterface(broken);
    surface.now += 1.0; // a rejected opening is reported once it has settled
    router.Tick();
    Expect(!router.Active() && router.Rejection() == "MB_RESUME(button)" && surface.errors.size() == 1,
           "without «Продолжить» the pause menu is rejected loudly");
}

static void Saves(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    // The second record carries an embedded thumbnail: the pack contract draws the detail column with it.
    router.SetInterface(Load(models, "saves-load.json"));
    ExpectEqual(router.Using(), "save_load", "saves is a script screen");
    ExpectEqual(surface.tag, "s-saves", "saves mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
           "every node of the saves screen is known");
    ExpectEqual(surface.props["title"], "<save.title_load>", "load title");
    const Json rows = surface.props["rows"];
    Expect(rows.size() == 3 && rows[1]["selected"] == true, "records listed, the game's selection chosen");
    ExpectEqual(rows[0]["cells"][0], "Герой Альфа · Порт Альфа", "a record is named «<hero> · <location>»");
    ExpectEqual(rows[2]["cells"][0], "Остров Гамма", "a record written without the hero: its location");
    ExpectEqual(rows[0]["cells"][1]["text"], "1 <common.month_10> 1654", "in-game date of the record");
    ExpectEqual(rows[0]["cells"][1]["sub"], "10 <common.month_3> 2001",
                "creation date under it (another year: the year instead of the time)");
    Expect(surface.props["columns"].size() == 3 && surface.props["columns"][1]["w"] == "120px",
           "save rows: name, dates in one column, the autosave mark");
    const Json detail = surface.props["detail"];
    ExpectEqual(detail.value("preview", std::string()), "save-preview/9/123/706c61796572/6f6c64",
                "selected record retains its actual embedded preview identity");
    Expect(detail["title"] == "Бухта Бета" && detail["sub"] == "Герой Альфа" && detail["delete"] == true &&
               detail["primary"] == "<save.load>",
           "detail of the chosen record");
    Expect(detail["facts"].size() == 3 && detail["facts"][0]["v"] == "2 <common.month_10> 1654, 11:20" &&
               detail["facts"][1]["v"] == "6 <save.hours> 42 <save.minutes>" &&
               detail["facts"][2]["v"] == "11 <common.month_3> 2001",
           "in-game date, play time, created");

    router.OnEvent({"saves", "row", Drawn(surface, "/rows", 2)});
    ExpectEqual(surface.props["detail"].value("preview", std::string()), "",
                "selecting a record without an image clears the previous preview");
    ExpectEqual(surface.props["detail"]["title"], "Остров Гамма", "a row tap chooses the record locally");
    Expect(surface.commands.empty() && surface.props["detail"]["facts"].size() == 3 &&
               surface.props["detail"]["sub"] == "" &&
               surface.props["detail"]["facts"][1]["v"] == "25 <save.hours> 0 <save.minutes>",
           "nothing sent; every record has its own play time");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tactivate@9:3",
                "«Загрузить» activates the record");
    router.OnEvent({"delete", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tdelete@9:3",
                "the trash glyph deletes the record");

    InterfaceModel question = Load(models, "saves-confirm.json");
    router.SetInterface(question);
    Json confirm = surface.props["confirm"];
    Expect(confirm["preset"] == "danger" && confirm["title"] == "<save.delete>" &&
               confirm["subject"] == "Остров Гамма" && confirm["text"] == "" && confirm["yes"] == "<save.delete>",
           "delete question: «Удалить «record»?»");
    router.OnEvent({"confirm-confirm", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tCONFIRM_YES\t-1\tpress", "confirm answers yes");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tCONFIRM_NO\t-1\tpress", "← during a question answers no");

    // From the title screen (its note, UiPass "saves" {from: title}) a load needs no question.
    router.CloseInterface();
    router.Pass("saves", ViewTree{"", "", {{"from", "title", {}}}});
    router.SetInterface(Load(models, "saves-load.json"));
    router.OnEvent({"primary", "click", ""});
    surface.commands.clear();
    question.revision = 3;
    router.SetInterface(question);
    Expect(surface.commands.size() == 1 && surface.Last() == "widget:save_load\tCONFIRM_YES\t-1\tpress" &&
               surface.props["confirm"].is_null(),
           "load from the title screen: the question is answered at once");
    router.SetInterface(question);
    Expect(surface.commands.size() == 1, "and only once per revision");
    router.CloseInterface();

    router.SetInterface(Load(models, "saves-save.json"));
    ExpectEqual(surface.props["title"], "<save.title_save>", "save title");
    Expect(surface.props["rows"][0]["cells"][0]["text"] == "<save.new>" &&
               surface.props["rows"][0]["selected"] == true &&
               surface.props["rows"][0]["cells"][0]["sub"] == "Герой Альфа · Порт Дельта" &&
               surface.props["rows"][0]["cells"][1] == "",
           "«Новая запись» leads, chosen, named by the game in progress, without dates");
    Expect(surface.props["detail"]["title"] == "Порт Дельта" && surface.props["detail"]["facts"].size() == 1,
           "new record: the location, the hero, the play time only");
    ExpectEqual(surface.props["detail"]["primary"], "<save.save>", "new record: «Сохранить»");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t3\tactivate@10:4",
                "saving activates the new slot");
    router.OnEvent({"saves", "row", Drawn(surface, "/rows", 1)});
    ExpectEqual(surface.props["detail"]["primary"], "<save.overwrite>",
                "an existing record when saving: «Перезаписать»");

    // The game's name editor is accepted as proposed.
    InterfaceModel naming = Load(models, "saves-save.json");
    Widget ok;
    ok.node = "CUSTOM_SAVE_BTN_OK";
    ok.type = "button";
    ok.label = "Ok";
    Widget name;
    name.node = "CUSTOM_SAVE_NAME";
    name.type = "edit";
    name.value = "Порт Альфа";
    naming.widgets.push_back(name);
    naming.widgets.push_back(ok);
    naming.revision = 5;
    surface.commands.clear();
    router.SetInterface(naming);
    Expect(surface.commands.size() == 1 && surface.Last() == "widget:save_load\tCUSTOM_SAVE_BTN_OK\t-1\tpress",
           "name editor: accepted at once");
    router.SetInterface(naming);
    Expect(surface.commands.size() == 1, "once per revision");

    // Two profiles name a record alike (every new game starts its own profile): each row loads and deletes its own.
    InterfaceModel twins = Load(models, "saves-load.json");
    twins.opening = 48;
    for (Widget &row : twins.widgets)
        if (row.node == "ANDROID_SAVE_LIST" && row.label == "Остров Гамма")
        {
            row.label = "Порт Альфа";
            row.context["profile"] = "other";
        }
    router.SetInterface(twins);
    router.OnEvent({"saves", "row", Drawn(surface, "/rows", 2)});
    ExpectEqual(surface.props["detail"]["title"], "Остров Гамма", "the second record of the name is chosen");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tactivate@48:3",
                "«Загрузить» activates the chosen profile's record");
    router.OnEvent({"delete", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tdelete@48:3",
                "the trash glyph deletes the chosen profile's record");

    InterfaceModel empty = Load(models, "saves-load.json");
    Without(empty, "ANDROID_SAVE_LIST");
    empty.opening = 49;
    surface.errors.clear();
    router.SetInterface(empty);
    Expect(router.Using() == "save_load" && surface.props["rows"].empty() && surface.props["detail"].is_null() &&
               surface.errors.empty(),
           "no records: the empty list");
    Without(empty, "ANDROID_SAVES");
    empty.opening = 50;
    router.SetInterface(empty);
    Expect(!router.Active() && surface.errors.size() == 1 &&
               surface.errors[0].find("the engine lists no records") != std::string::npos,
           "without the engine's record list the saves screen fails loudly");
}

// The settings screen on one router: the game page and its toggles, the controls and graphics pages, the cache, a
// small display, the debug overrides, leaving and the rejection of an incomplete interface.
struct SettingsRun
{
    std::string models;
    FakeSurface surface;
    Router owned{surface};
    Router &router = Scripted(owned);
    const std::string prefix = "widget:option_screen\t";
    Json page; // the graphics page's sections, from GraphicsPage on

    void GamePage()
    {
        std::ifstream state(models + "/app-settings.json", std::ios::binary);
        router.SetState("appSettings", Json::parse(state));
        router.SetInterface(Load(models, "settings.json"));
        ExpectEqual(router.Using(), "option_screen", "settings is a script screen");
        ExpectEqual(surface.tag, "s-settings", "settings mounted");
        Expect(surface.errors.empty(), "no errors");
        Expect(std::count(surface.commands.begin(), surface.commands.end(), "host app-cache-measure ") == 1,
               "an opened settings screen has the host measure the cache as it is now");
        const Json extras = surface.props["extras"];
        Expect(extras.is_null() || extras["rows"].empty(),
               "keyboard nodes, their defaults button and colour correction are not offered: " + extras.dump());
        Expect(surface.props["tabs"].size() == 3 && surface.props["tabs"][0]["label"] == "<settings.tab_game>" &&
                   surface.props["tabs"][1]["label"] == "<settings.tab_controls>" &&
                   surface.props["tabs"][2]["label"] == "<settings.tab_sound_graphics>" &&
                   surface.props["tabs"][0]["selected"] == true,
               "three pages, the first open; no «Отладка» without debug overrides");
        Json columns = surface.props["sections"][0]["columns"];
        Expect(columns.size() == 2 && columns[0]["title"] == "<settings.sea_and_map>" &&
                   columns[1]["title"] == "<settings.markers>",
               "game page: sea and map, marks");
        const Json pirates = columns[0]["rows"][0];
        Expect(pirates["id"] == "app-pirate_frequency" && pirates["label"] == "<settings.pirate_frequency>" &&
                   pirates["selected"] == 0 && pirates["restart"] == false && pirates["disabled"] == false &&
                   pirates["options"] == Json::array({"<settings.pirates_rare>", "<settings.pirates_sometimes>",
                                                      "<settings.pirates_often>"}),
               "«Частота пиратов на карте» first, from the host, 20% («Редко») chosen, applied without a restart: " +
                   pirates.dump());
        Expect(columns[0]["rows"].size() == 4 && columns[0]["rows"][3]["label"] == "Автозапись",
               "game toggles keep the game's labels");
        router.OnEvent({columns[0]["rows"][2]["id"], "change", "1"});
        ExpectEqual(surface.Last(), prefix + "ENHANCEDSAILING_CHECKBOX\t0\t1", "toggle on");
        // The game's answer: the switch on (the prediction drawn meanwhile ends with it).
        InterfaceModel answered = Load(models, "settings.json");
        answered.revision += 1;
        for (Widget &widget : answered.widgets)
            if (widget.node == "ENHANCEDSAILING_CHECKBOX")
                widget.value = "1";
        router.Answer(surface.last_request, true, "");
        router.SetInterface(answered);
        Expect(surface.props["sections"][0]["columns"][0]["rows"][2]["on"] == true && surface.errors.empty(),
               "the game's answer shows the switch on");
        surface.commands.clear();
        router.OnEvent({"app-pirate_frequency", "change", "2"});
        Expect(surface.commands.empty() && surface.props["sections"][0]["columns"][0]["rows"][0]["selected"] == 2,
               "the pirate frequency is a draft until «Готово»");
    }

    void ControlsPage()
    {
        router.OnEvent({"tabs", "select", "1"});
        Json controls = surface.props["sections"][0]["columns"];
        Expect(controls.size() == 2 && controls[0]["title"] == "<settings.camera>" &&
                   controls[1]["title"] == "<settings.screen_touch>",
               "controls page: the camera, the screen and touch");
        const Json screen = controls[1]["rows"];
        Expect(screen.size() == 4 && screen[3]["id"] == "app-haptics" && screen[3]["kind"] == "toggle" &&
                   screen[3]["on"] == true && screen[3]["label"] == "<settings.haptics>",
               "«Вибрация» as a toggle, on: " + screen.dump());
        Expect(screen[2]["id"] == "app-left_handed" && screen[2]["kind"] == "toggle" && screen[2]["on"] == false &&
                   screen[2]["label"] == "<settings.left_handed>",
               "«Под левую руку» as a toggle: " + screen.dump());
        Expect(screen[0]["id"] == "app-ui_scale" && screen[0]["selected"] == 0 && screen[0]["restart"] == false &&
                   screen[0]["disabled"] == false && screen[0]["options"] == Json::array({"100%", "115%"}) &&
                   screen[1]["id"] == "app-hud_transparency" && screen[1]["selected"] == 0 &&
                   screen[1]["options"] == Json::array({"<settings.transparency_none>", "35%", "60%"}),
               "«Размер интерфейса» and «Прозрачность кнопок», applied at once: " + screen.dump());
        const Json camera = controls[0]["rows"];
        Expect(camera.size() == 4 && camera[0]["id"] == "sensitivity" &&
                   camera[0]["label"] == "<settings.camera_sensitivity>" && camera[0]["value"] == 50 &&
                   camera[1]["label"] == "<settings.invert_vertical>" &&
                   camera[2]["label"] == "<settings.sea_camera>" && camera[3]["label"] == "<settings.door_camera_turn>",
               "one sensitivity, «Инверсия по вертикали», sea camera, door turn; no «Всегда бегом»: " + camera.dump());
        for (const Json &row : camera)
            Expect(row["id"].get<std::string>().find("ALWAYS_RUN") == std::string::npos, "«Всегда бегом» is gone");
        surface.commands.clear();
        router.OnEvent({"sensitivity", "change", "80"});
        Expect(surface.commands.empty(), "sensitivity drag progress sends nothing");
        router.OnEvent({"sensitivity", "commit", "80"});
        Expect(surface.commands.size() == 2 &&
                   surface.commands[0] == prefix + "VMOUSE_SENSITIVITY_SLIDE\t-1\tset@12:80" &&
                   surface.commands[1] == prefix + "HMOUSE_SENSITIVITY_SLIDE\t-1\tset@12:80",
               "the one sensitivity sets both game sliders to the same position");

        router.OnEvent({"app-left_handed", "change", "1"});
        router.OnEvent({"app-ui_scale", "change", "1"});
        router.OnEvent({"app-hud_transparency", "change", "2"});
        Expect(surface.props["sections"][0]["columns"][1]["rows"][0]["selected"] == 1 &&
                   surface.props["sections"][0]["columns"][1]["rows"][1]["selected"] == 2,
               "display options are a draft");
    }

    void GraphicsPage()
    {
        router.OnEvent({"tabs", "select", "2"});
        page = surface.props["sections"];
        Expect(page.size() == 1 && page[0]["columns"].size() == 2 &&
                   page[0]["columns"][0]["title"] == "<settings.volume>" &&
                   page[0]["columns"][1]["title"] == "<settings.graphics>",
               "sound and graphics page: volumes and the graphics column");
        const Json volume = page[0]["columns"][0]["rows"];
        Expect(volume.size() == 3 && volume[0]["label"] == "<settings.music>" && volume[0]["value"] == 70,
               "volume sliders with our labels");
        surface.commands.clear();
        router.OnEvent({volume[0]["id"], "commit", "40"});
        ExpectEqual(surface.Last(), prefix + "MUSIC_SLIDE\t-1\tset@12:40", "slider release sets the position");
        Json preset = page[0]["columns"][1]["rows"][0];
        Expect(page[0]["columns"][1]["rows"].size() == 8 && preset["id"] == "app-fx_preset" &&
                   preset["selected"] == 0 && preset["restart"] == false && preset["label"] == "" &&
                   preset["options"] ==
                       Json::array({"<settings.fx_low>", "<settings.fx_medium>", "<settings.fx_high>"}),
               "the quality preset, applied at once; no graphics API row (it is in «О программе»): " + preset.dump());
        Expect(preset["hint"] == "<settings.fx_scene_height>",
               "«Низкая» at a lower measured height shows it under the row: " + preset.dump());
        const Json rate = page[0]["columns"][1]["rows"][1];
        Expect(rate["id"] == "app-frame_rate" && rate["label"] == "<settings.frame_rate>" && rate["selected"] == 1 &&
                   rate["restart"] == false && rate["disabled"] == false &&
                   rate["options"] == Json::array({"30", "60", "90", "120"}),
               "«Частота кадров» offers the display's rates, 60 chosen: " + rate.dump());
        const Json textures = page[0]["columns"][1]["rows"][2];
        Expect(textures["id"] == "app-texture_quality" && textures["selected"] == 2 && textures["restart"] == false &&
                   textures["disabled"] == false &&
                   textures["options"] ==
                       Json::array({"<settings.texture_low>", "<settings.texture_medium>", "<settings.texture_high>"}),
               "«Качество текстур» Низкое/Среднее/Высокое (the resolution only), Высокое chosen: " + textures.dump());
        router.OnEvent({"app-texture_quality", "change", "1"});
        Expect(surface.props["sections"][0]["columns"][1]["rows"][2]["restart"] == true,
               "a lower resolution applies at the next start");
        router.OnEvent({"app-texture_quality", "change", "2"});
        Expect(surface.props["sections"][0]["columns"][1]["rows"][2]["restart"] == false,
               "back to the stored resolution: no restart");
        // «Обработка текстур»: a heading and three switches, applied at once with «Готово», apart from the preset.
        const auto processing = [this](size_t row) { return surface.props["sections"][0]["columns"][1]["rows"][row]; };
        Expect(processing(4)["kind"] == "heading" && processing(4)["label"] == "<settings.texture_processing>" &&
                   processing(5)["kind"] == "toggle" && processing(5)["id"] == "app-texture_normal_maps" &&
                   processing(5)["label"] == "<settings.texture_normal_maps>" && processing(5)["on"] == true &&
                   processing(5)["disabled"] == false && processing(6)["id"] == "app-texture_height_maps" &&
                   processing(6)["label"] == "<settings.texture_height_maps>" && processing(6)["on"] == true &&
                   processing(6)["disabled"] == false && processing(7)["id"] == "app-texture_upscale" &&
                   processing(7)["label"] == "<settings.texture_upscale>" && processing(7)["on"] == false,
               "«Обработка текстур»: «Карты нормалей», «Параллакс», «Апскейл текстур ×2»: " + processing(4).dump() +
                   processing(5).dump() + processing(6).dump() + processing(7).dump());
        Expect(processing(7)["disabled"] == true && processing(7)["reason"] == "<settings.texture_upscale_unknown>",
               "«Апскейл текстур ×2» disabled with why until the renderer reports the device: " + processing(7).dump());
        router.OnEvent({"app-texture_normal_maps", "change", "0"});
        Expect(processing(5)["on"] == false && processing(6)["disabled"] == true && processing(6)["on"] == true &&
                   processing(6)["reason"] == "<settings.texture_height_maps_need_normals>",
               "without the normal maps «Параллакс» is disabled with its stored value and the reason: " +
                   processing(6).dump());
        router.OnEvent({"app-texture_normal_maps", "change", "1"});
        Expect(processing(6)["disabled"] == false && processing(6)["on"] == true,
               "the normal maps on again: the height row as it was");
        router.SetState("renderCaps", Json{{"upscale", "supported"}});
        router.OnEvent({"app-texture_upscale", "change", "1"});
        Expect(processing(7)["disabled"] == false && processing(7)["on"] == true,
               "a device that can show the ×2 offers it: " + processing(7).dump());
        router.OnEvent({"app-texture_upscale", "change", "0"});
        router.SetState("renderCaps", Json{{"upscale", "unsupported"}});
        router.OnEvent({"app-texture_height_maps", "change", "0"});
        Expect(processing(7)["disabled"] == true &&
                   processing(7)["reason"] == "<settings.texture_upscale_unsupported>" && processing(6)["on"] == false,
               "a device without ETC2 or BC textures shows the ×2 disabled with that reason: " + processing(7).dump());
        router.OnEvent({"app-texture_height_maps", "change", "1"});
        router.SetState("renderCaps", Json{{"upscale", "unsupported"}, {"texture_processing", false}});
        const Json &rows = surface.props["sections"][0]["columns"][1]["rows"];
        Expect(std::none_of(rows.begin(), rows.end(),
                            [](const Json &row) { return row.value("id", "") == "texture-processing"; }),
               "a game that ships its own texture maps: no «Обработка текстур»: " + rows.dump());
        router.SetState("renderCaps", Json{{"upscale", "unsupported"}});
    }

    void Cache()
    {
        const Json cache = page[0]["columns"][1]["rows"][3];
        Expect(cache["kind"] == "button" && cache["id"] == "app-cache-clear" && cache["label"] == "<settings.cache>" &&
                   cache["display"] == "<settings.cache_clear>" && cache["disabled"] == false,
               "«Кэш» with its size and «Очистить»: " + cache.dump());
        surface.commands.clear();
        router.OnEvent({"app-cache-clear", "click", ""});
        const Json cleared = surface.props["sections"][0]["columns"][1]["rows"][3];
        Expect(surface.commands.size() == 1 && surface.commands[0] == "host app-cache-clear " &&
                   cleared["display"] == "<settings.cache_clear>",
               "«Очистить» asks the host at once (it clears at the next start): " + Json(surface.commands).dump() +
                   cleared.dump());
        {
            std::ifstream again(models + "/app-settings.json", std::ios::binary);
            Json reported = Json::parse(again);
            reported["cache_clear_pending"] = true;
            router.SetState("appSettings", reported);
        }
        const Json pending = surface.props["sections"][0]["columns"][1]["rows"][3];
        Expect(pending["disabled"] == false && pending["display"] == "" &&
                   pending["hint"] == "<settings.cache_pending>",
               "the host's report of the pending clear shows on the open screen: " + pending.dump());
        surface.commands.clear();
        router.OnEvent({"app-cache-clear", "click", ""});
        Expect(surface.commands.empty(), "a pending clear is not asked twice");
        surface.commands.clear();
        router.OnEvent({"app-fx_preset", "change", "2"});
        router.OnEvent({"app-frame_rate", "change", "3"});
        Expect(surface.commands.empty() && surface.props["sections"][0]["columns"][1]["rows"][0]["selected"] == 2 &&
                   surface.props["sections"][0]["columns"][1]["rows"][1]["selected"] == 3,
               "renderer options are a draft");
        Expect(surface.props["sections"][0]["columns"][1]["rows"][0]["hint"] == "",
               "the measured scene height belongs to «Низкая» only");
        router.OnEvent({"done", "click", ""});
        Expect(surface.commands.size() == 2 &&
                   surface.commands[0] ==
                       "host app-settings "
                       "{\"frame_rate\":120,\"fx_preset\":2,\"haptics\":1,\"hud_transparency\":60,"
                       "\"left_handed\":1,\"pirate_frequency\":100,\"texture_height_maps\":1,\"texture_"
                       "normal_maps\":1,\"texture_quality\":2,\"texture_upscale\":0,\"ui_scale\":115}" &&
                   surface.commands[1] == prefix + "BTN_OK\t-1\tpress",
               "«Готово» hands the app options to the host; fx_preset_auto and fx_auto_render_height stay with it");
        surface.commands.clear();
        router.OnEvent({"done", "click", ""});
        Expect(surface.commands.size() == 1, "unchanged app options are not sent again");
    }

    void SmallDisplay()
    {
        {
            // A display with room for no size above 100 %: the UI size row stays, disabled, with the reason.
            FakeSurface small_surface;
            Router small(small_surface);
            Scripted(small);
            Json reported = Json::parse(std::ifstream(models + "/app-settings.json", std::ios::binary));
            reported["ui_scale_options"] = Json::array({100});
            small.SetState("appSettings", reported);
            small.SetInterface(Load(models, "settings.json"));
            small.OnEvent({"tabs", "select", "1"});
            const Json size = small_surface.props["sections"][0]["columns"][1]["rows"][0];
            Expect(size["id"] == "app-ui_scale" && size["disabled"] == true &&
                       size["reason"] == "<settings.ui_scale_no_room>" && size["options"] == Json::array({"100%"}),
                   "a display without room for a larger UI shows the size row disabled with its reason: " +
                       size.dump());
        }
        {
            // A host that reports no cache (the desktop) is not asked to measure one, and the page has no cache row.
            FakeSurface bare_surface;
            Router bare(bare_surface);
            Scripted(bare);
            Json reported = Json::parse(std::ifstream(models + "/app-settings.json", std::ios::binary));
            reported.erase("cache_bytes");
            reported.erase("cache_clear_pending");
            bare.SetState("appSettings", reported);
            bare.SetInterface(Load(models, "settings.json"));
            Expect(bare_surface.errors.empty() && std::count(bare_surface.commands.begin(), bare_surface.commands.end(),
                                                             "host app-cache-measure ") == 0,
                   "a host without a cache report is not asked to measure it");
        }
    }

    void DebugOverrides()
    {
        {
            // A debuggable host reports single effect overrides: «Отладка эффектов» has the FSR / SMAA
            // rows and «Замер GPU» (fx_gpu_profile), handed to the host with «Готово» like the others.
            FakeSurface debug_surface;
            Router debug(debug_surface);
            Scripted(debug);
            Json reported{{"fx_preset", 1}, {"texture_quality", 2}};
            for (const storm::app::debug::Entry &entry : storm::app::debug::all)
                reported[entry.key] = -1; // the preset's
            debug.SetState("appSettings", reported);
            debug.SetInterface(Load(models, "settings.json"));
            Expect(debug_surface.props["tabs"].size() == 4 && debug_surface.props["tabs"][3]["label"] == "Отладка",
                   "a debuggable host adds the untranslated «Отладка» page");
            debug.OnEvent({"tabs", "select", "3"});
            Json rows = Json::array();
            for (const Json &section : debug_surface.props["sections"])
                for (const Json &column : section["columns"])
                    if (column["title"] == "Отладка эффектов")
                        rows = column["rows"];
            const auto row = [&](const char *id) {
                for (const Json &r : rows)
                    if (r["id"] == id)
                        return r;
                return Json();
            };
            Expect(row("app-fx_render_height")["options"] ==
                           Json::array({"пресет", "720", "900", "1000", "1100", "экран"}) &&
                       row("app-fx_smaa")["label"] == "SMAA" && row("app-fx_sharpen").is_object() &&
                       row("app-fx_gpu_profile")["options"] == Json::array({"выкл", "вкл"}),
                   "effects debug: scene height, SMAA, RCAS, GPU timing: " + rows.dump());
            Expect(row("app-bench")["options"] == Json::array({"выкл", "полный", "быстрый"}) &&
                       row("app-bench")["selected"] == 0,
                   "debug: the bench request, off when the page opens: " + rows.dump());
            Expect(row("app-fx_far_lod")["options"] == Json::array({"пресет", "выкл", "1 px", "2 px"}),
                   "effects debug: far LODs: " + rows.dump());
            Expect(row("app-fx_lamp_shadows")["options"] == Json::array({"пресет", "выкл", "8", "16"}) &&
                       row("app-fx_lamp_soft").is_object() && row("app-fx_lamp_model_lift").is_object(),
                   "effects debug: lamp shadows 0/8/16, their filter, the characters' lift: " + rows.dump());
            Expect(row("app-fx_sail_light")["options"] == Json::array({"пресет", "выкл", "100%", "200%", "300%"}),
                   "effects debug: sunlight through the sails: " + rows.dump());
            Expect(row("app-fx_lamp_tint")["options"] == Json::array({"пресет", "0%", "25%", "50%", "100%"}) &&
                       row("app-render_backend")["options"] == Json::array({"авто", "GLES", "Vulkan"}) &&
                       row("app-render_thread").is_object() && row("app-render_overlap").is_object(),
                   "effects debug: lamp tint, graphics API, render thread, frame overlap: " + rows.dump());
            Expect(row("app-fx_light_reach")["options"] == Json::array({"пресет", "100%", "160%", "220%", "300%"}) &&
                       row("app-fx_light_core").is_object() && row("app-fx_light_level").is_object(),
                   "effects debug: the lights' reach, core and level: " + rows.dump());
            Expect(row("app-fx_bounce").is_null() && row("app-fx_bounce_debug").is_null() &&
                       row("app-fx_town_walls")["options"] == Json::array({"пресет", "игра", "наши"}),
                   "effects debug: town walls, and no bounce any more: " + rows.dump());
            Expect(row("app-fx_gloss").is_object() && row("app-fx_gloss_strength").is_object(),
                   "effects debug: gloss maps: " + rows.dump());
            Expect(row("app-fx_character_relief")["options"] ==
                           Json::array({"пресет", "выкл", "нормали", "параллакс"}) &&
                       row("app-fx_character_relief_strength")["options"].size() == 5,
                   "effects debug: the characters' relief and its strength: " + rows.dump());
            Expect(row("app-fx_sea_wakes").is_object(), "effects debug: the hulls' wakes: " + rows.dump());
            Expect(row("app-fx_upscale").is_null() && row("app-texture_upscale").is_null(),
                   "the neural ×2 is no debug override: «Апскейл текстур ×2» on the graphics page decides it");
            Expect(row("app-fx_town_light")["options"] == Json::array({"пресет", "игра", "наш"}),
                   "effects debug: the town light, game's or ours: " + rows.dump());
            debug.OnEvent({"app-fx_render_height", "change", "2"});
            debug.OnEvent({"app-fx_gpu_profile", "change", "1"});
            debug_surface.commands.clear();
            debug.OnEvent({"done", "click", ""});
            Expect(!debug_surface.commands.empty() &&
                       debug_surface.commands[0].find("\"fx_render_height\":900") != std::string::npos &&
                       debug_surface.commands[0].find("\"fx_gpu_profile\":1") != std::string::npos,
                   "«Готово» hands the scene height and the GPU timing to the host: " +
                       Json(debug_surface.commands).dump());
        }
    }

    void Leaving()
    {
        router.OnEvent({"header-back", "click", ""});
        ExpectEqual(surface.Last(), prefix + "BTN_CANCEL\t-1\tpress", "← drops the game's changes");
    }

    void Rejected()
    {
        InterfaceModel broken = Load(models, "settings.json");
        Without(broken, "BTN_OK");
        broken.opening = 77;
        router.SetInterface(broken);
        surface.now += 1.0; // a rejected opening is reported once it has settled
        router.Tick();
        Expect(!router.Active() && router.Rejection() == "BTN_OK(button)" && surface.errors.size() == 1,
               "without «Ок» the settings screen is rejected loudly");
    }
};

static void Settings(const std::string &models)
{
    SettingsRun run{models};
    run.GamePage();
    run.ControlsPage();
    run.GraphicsPage();
    run.Cache();
    run.Leaving();
    run.Rejected();
    // Last: their routers share the script program, and the debug page it is left on, with the run's router.
    run.SmallDisplay();
    run.DebugOverrides();
}

static void Title(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    router.SetInterface(Load(models, "title.json"));
    ExpectEqual(router.Using(), "mainmenu", "title is a ported screen");
    ExpectEqual(surface.tag, "s-title", "title mounted");
    Expect(surface.errors.empty(), "no errors");
    Expect(surface.props["primary"]["label"] == "<menu.continue>" && surface.props["buttons"].size() == 4,
           "«Продолжить» and four buttons");
    ExpectEqual(surface.props["primary"]["sub"], "Герой Альфа · Порт Альфа", "«Продолжить» names the newest record");
    Expect(surface.props["primary"]["disabled"] == false, "«Продолжить» with a record");
    {
        InterfaceModel none = Load(models, "title.json");
        Without(none, "ANDROID_CONTINUE");
        none.revision = 7;
        router.SetInterface(none);
        Expect(surface.props["primary"]["disabled"] == true && surface.props["primary"]["reason"] == "<menu.no_saves>",
               "no record: «Продолжить» disabled, the reason on a tap");
        router.SetInterface(Load(models, "title.json"));
    }
    const char *const commands[][2] = {
        {"new", "New"}, {"load", "Load"}, {"options", "Options"}, {"credits", "Credits"}, {"exit", "Exit"}};
    for (const auto &[id, command] : commands)
    {
        router.OnEvent({id, "click", ""});
        ExpectEqual(surface.Last(), std::string("main:") + command,
                    std::string(id) + " sends the engine's title command");
        Expect(NoteField(router.Take("saves"), "from") == (std::string(command) == "Load" ? "title" : ""),
               std::string(id) + ": only the save list is told it opens from the title");
    }
    router.OnEvent({"about", "click", ""});
    ExpectEqual(surface.Last(), "host app-about ", "«О программе» asks the host for its about page");

    router.OnEvent({"continue", "click", ""});
    ExpectEqual(surface.Last(), "main:Continue",
                "«Продолжить» asks the engine to load the newest record directly, no save list");
    {
        // The host's «Продолжить» (titleIntent, the app's Game Loop test intent): once, at the next title build or at
        // once.
        surface.commands.clear();
        router.SetState("titleIntent", Json{{"intent", "Continue"}});
        router.SetInterface(Load(models, "title.json"));
        Expect(surface.commands.size() == 1 && surface.Last() == "main:Continue",
               "titleIntent Continue loads the newest record directly");
        router.SetInterface(Load(models, "title.json"));
        Expect(surface.commands.size() == 1, "only once");
        InterfaceModel none = Load(models, "title.json");
        Without(none, "ANDROID_CONTINUE");
        none.revision = 11;
        router.SetInterface(none);
        router.SetState("titleIntent", Json{{"intent", "Continue"}});
        Expect(surface.commands.size() == 1 && !surface.toasts.empty() && surface.toasts.back() == "<menu.no_saves>",
               "without a record the title says there is none, nothing is sent");
        surface.errors.clear();
        router.SetInterface(Load(models, "title.json"));
        surface.commands.clear();
        router.SetState("titleIntent", Json{{"intent", "Continue"}});
        Expect(surface.commands.size() == 1 && surface.Last() == "main:Continue",
               "titleIntent while the title is up acts at once");
        const size_t errors2 = surface.errors.size();
        router.SetState("titleIntent", Json{{"intent", "New"}});
        Expect(surface.errors.size() == errors2 + 1, "an unknown intent is an error (title up)");
        surface.errors.clear();
    }
}

static void NewGame(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    router.SetInterface(Load(models, "new-game.json"));
    ExpectEqual(router.Using(), "selectcharacter", "new game is a script screen");
    ExpectEqual(surface.tag, "s-new-game", "new game mounted");
    Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
           "every node is known; the profile is not offered");
    const Json rows = surface.props["rows"];
    Expect(rows.size() == 2 && rows[0]["kind"] == "stepper" && rows[0]["text"] == "Средняя" &&
               rows[1]["kind"] == "segment" && rows[1]["label"] == "Морской бой",
           "difficulty selector and the sea battle segment");
    Expect(surface.props["toggles"].size() == 6 && surface.props["toggles"][0]["id"] == "tutorial" &&
               surface.props["toggles"][1]["id"] == "CHECK_HARDCORE",
           "six option toggles, the port's own first");
    Expect(surface.props["hero"]["name"] == "Герой Альфа" && surface.props["hero"]["nation"] == "<menu.nation_france>",
           "hero and nation");
    ExpectEqual(surface.props["hero"]["image"]["texture"], "INTERFACES/PORTRAITS/128/face_1",
                "the hero's game portrait by face");
    ExpectEqual(surface.props["toggles"][1]["label"], "<menu.hardcore>",
                "the untranslated «HardcoreGame» key gets our name");
    Expect(surface.props["setup"]["text"] == "<menu.hero_type_1>" &&
               surface.props["setup"]["note"] == "Описание занятия героя.",
           "occupation titled by its key, and its text");
    Expect(!surface.props.contains("stats"), "no attribute table");
    const std::string prefix = "widget:selectcharacter\t";
    router.OnEvent({"difficulty-right", "click", ""});
    ExpectEqual(surface.Last(), prefix + "RIGHTCHANGE_COMPLEX\t-1\tpress", "difficulty ›");
    router.OnEvent({"setup-left", "click", ""});
    ExpectEqual(surface.Last(), prefix + "LEFTCHANGE_SETUP\t-1\tpress", "occupation ‹");
    router.OnEvent({"battle", "change", "1"});
    ExpectEqual(surface.Last(), prefix + "CHECK_ARCADESAIL\t1\t1", "sea battle type chosen");
    router.OnEvent({"tutorial", "change", "1"});
    ExpectEqual(surface.Last(), prefix + "ANDROID_TUTORIAL_SKIP\t0\t1", "tutorial skip");
    router.OnEvent({surface.props["toggles"][1]["id"], "change", "1"});
    ExpectEqual(surface.Last(), prefix + "CHECK_HARDCORE\t0\t1", "option toggle");
    router.OnEvent({"bio", "click", ""});
    Expect(surface.props["extras"]["reading"] == true &&
               surface.props["extras"]["readingText"] == "Короткая биография героя.",
           "ⓘ reads the biography");
    router.OnEvent({"start", "click", ""});
    ExpectEqual(surface.Last(), prefix + "OK_BUTTON\t-1\tpress", "«Начать игру»");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "CANCEL_BUTTON\t-1\tpress", "← cancels");

    InterfaceModel window = Load(models, "new-game.json");
    Widget text;
    text.node = "CONFIRM_WINDOW_STRINGS";
    text.type = "text";
    text.label = "Начать заново?";
    Widget yes;
    yes.node = "CONFIRM_WINDOW_MB_YES";
    yes.type = "button";
    yes.label = "Да";
    window.widgets.push_back(text);
    window.widgets.push_back(yes);
    window.revision = 2;
    router.SetInterface(window);
    Expect(surface.props["modal"].is_null() && surface.Last() == prefix + "CONFIRM_WINDOW_MB_YES\t-1\tpress",
           "the profile folder question is answered for the player");
    const size_t sent = surface.commands.size();
    router.SetInterface(window);
    Expect(surface.commands.size() == sent, "once per revision");

    InterfaceModel broken = Load(models, "new-game.json");
    Without(broken, "OK_BUTTON");
    broken.opening = 66;
    router.SetInterface(broken);
    surface.now += 1.0; // a rejected opening is reported once it has settled
    router.Tick();
    Expect(!router.Active() && router.Rejection() == "OK_BUTTON(button)",
           "without «Ок» the new game screen is rejected loudly");
}

// A press the game refuses tells the player which record it was; a record the game cannot load says why before.
static void SavesRefused(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    router.SetInterface(Load(models, "saves-load.json"));
    router.OnEvent({"saves", "row", Drawn(surface, "/rows", 2)});
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t2\tactivate@9:3",
                "«Загрузить» activates the record");
    Expect(surface.last_request != 0 && surface.toasts.empty(), "the load is a widget action waiting for its answer");
    router.Answer(surface.last_request, false, "the save record stayed unselectable");
    Expect(surface.toasts.size() == 1 && surface.toasts.back() == "<save.load_failed>: Остров Гамма",
           "a refused load is told to the player with the record's name");
    router.SetInterface(Load(models, "saves-load.json"));
    Expect(surface.toasts.size() == 1, "told once");

    router.OnEvent({"delete", "click", ""});
    router.Answer(surface.last_request, false, "the save list changed");
    Expect(surface.toasts.size() == 2 && surface.toasts.back() == "<save.delete_failed>: Остров Гамма",
           "a refused delete is told as well");

    router.OnEvent({"primary", "click", ""});
    router.Answer(surface.last_request, true, "");
    InterfaceModel after = Load(models, "saves-load.json");
    after.revision = 2;
    router.SetInterface(after);
    Expect(surface.toasts.size() == 2, "an applied press says nothing");

    // The game's question answered, then refused (the record moved): told as the load it answers.
    InterfaceModel question = Load(models, "saves-confirm.json");
    question.revision = 3;
    router.SetInterface(question);
    router.OnEvent({"confirm-confirm", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tCONFIRM_YES\t-1\tpress", "confirm answers yes");
    router.Answer(surface.last_request, false, "the chosen record moved");
    Expect(surface.toasts.size() == 3 && surface.toasts.back() == "<save.load_failed>: Остров Гамма",
           "a refused answer to the load question is told");

    // A record without a readable description: the game cannot load it, and the press says so.
    InterfaceModel damaged = Load(models, "saves-load.json");
    damaged.revision = 4;
    for (Widget &row : damaged.widgets)
        if (row.node == "ANDROID_SAVE_LIST" && row.label == "Остров Гамма")
            row.context["unreadable"] = "1";
    router.SetInterface(damaged);
    router.OnEvent({"saves", "row", Drawn(surface, "/rows", 2)});
    Expect(surface.props["detail"]["disabled"] == true && surface.props["detail"]["reason"] == "<save.unreadable>" &&
               surface.props["detail"]["delete"] == true,
           "a damaged record: «Загрузить» disabled with the reason, still deletable");
    router.OnEvent({"saves", "row", Drawn(surface, "/rows", 1)});
    Expect(surface.props["detail"]["disabled"] == false, "a readable record loads");
}

static void Autosaves(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    router.SetInterface(Load(models, "saves-autosave.json"));
    const Json rows = surface.props["rows"];
    Expect(rows.size() == 4 && rows[0]["cells"][0]["text"] == "<save.autosave>" &&
               rows[0]["cells"][0]["sub"] == "Герой Альфа · Остров Гамма" && rows[0]["cells"][2]["icon"] == "energy",
           "autosaves form their own group at the top, named by the string table, marked by the bolt");
    router.OnEvent({"saves", "row", Drawn(surface, "/rows", 0)});
    Expect(surface.props["detail"]["delete"] == false && surface.props["detail"]["title"] == "Остров Гамма" &&
               surface.props["detail"]["sub"] == "<save.autosave> · Герой Альфа",
           "an autosave has no quick delete");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:save_load\tANDROID_SAVE_LIST\t1\tactivate@9:9", "an autosave loads");
    InterfaceModel saving = Load(models, "saves-autosave-save.json");
    router.SetInterface(saving);
    bool listed = false;
    for (const Json &row : surface.props["rows"])
        listed = listed || row["cells"][0].dump().find("autosave") != std::string::npos;
    Expect(!listed && surface.props["rows"].size() == 4, "autosaves are never an overwrite target");
}

static void GameOver(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    router.SetState("gameOver", Json{{"kind", "sea"}, {"language", "russian"}});
    Expect(router.Using() == "game_over" && surface.tag == "s-game-over" &&
               surface.props["title"] == "<menu.game_over_sea>",
           "game over: the defeat's title over the death picture");
    router.Back();
    Expect(surface.commands.empty() && router.Using() == "game_over", "back does nothing: the game ends it");
    const Json actions = surface.props["actions"];
    Expect(actions.size() == 3 && actions[0]["id"] == "go-load" && actions[1]["id"] == "go-options" &&
               actions[2]["id"] == "go-title",
           "the death screen offers load, settings and the title screen");
    router.OnEvent({"go-load", "click", ""});
    ExpectEqual(surface.Last(), "game_over:END", "a choice ends the death picture at once");
    router.SetState("gameOver", nullptr);
    surface.commands.clear();
    router.SetInterface(Load(models, "title.json"));
    Expect(surface.commands.size() == 1 && surface.commands[0] == "main:Load",
           "then the title screen opens the chosen load once");
    router.SetInterface(Load(models, "title.json"));
    Expect(surface.commands.size() == 1, "only once");
    router.SetState("gameOver", Json{{"kind", "land"}, {"language", "russian"}});
    router.OnEvent({"go-title", "click", ""});
    surface.commands.clear();
    InterfaceModel title = Load(models, "title.json");
    title.opening += 1;
    router.SetInterface(title);
    Expect(surface.commands.empty() && surface.errors.empty(), "«Главное меню» stays on the title screen");
    router.CloseInterface();
    router.SetState("gameOver", Json{{"kind", "sea"}});
    router.SetState("gameOver", nullptr);
    Expect(!router.Active(), "null closes it");
    router.SetState("gameOver", Json{{"kind", "victory"}});
    Expect(!router.Active() && surface.errors.size() == 1, "an unknown kind is an error");
}

// The quick save's plate (quick_save.c): the game's caption and line, nothing to press.
static void QuickSave(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    router.SetInterface(Load(models, "quick-save.json"));
    Expect(router.Using() == "quick_save" && surface.tag == "s-quick-save" && surface.errors.empty() &&
               surface.props["extras"]["rows"].empty(),
           "the quick save is a ported screen; every node is known");
    Expect(surface.props["title"] == "Быстрое сохранение" && surface.props["text"] == "Идёт сохранение...",
           "the game's caption and line");
    router.Back();
    Expect(surface.commands.empty() && router.Using() == "quick_save",
           "back does nothing: the game closes it when the record is written");
    InterfaceModel untitled = Load(models, "quick-save.json");
    Without(untitled, "INFO_WINDOW_CAPTION");
    untitled.opening += 1;
    router.SetInterface(untitled);
    Expect(surface.props["title"] == "<menu.quick_save>", "without the game's caption: our title");
}

// The engine's load watchdog (core load_watch.hpp): the player is told which record did not load.
static void LoadFailure()
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("loadFailure", Json{{"record", "SAVE\\player\\Автосохранение 1"},
                                        {"reason", "the record has no readable description"}});
    Expect(surface.alerts.size() == 1 && surface.alerts.front() == "<save.load_failed>: Автосохранение 1" &&
               surface.errors.empty(),
           "a failed load names its record to the player in an alert (never the HUD's log)");
    router.SetState("loadFailure", nullptr);
    Expect(surface.alerts.size() == 1 && surface.errors.empty(), "null says nothing");
    router.SetState("loadFailure", Json{{"record", 7}});
    Expect(surface.alerts.size() == 1 && surface.errors.size() == 1,
           "a malformed failure is an error, never a quiet drop");
}

// A save that could not be written (core ReportFailedSave): the player is told, the full storage as such.
static void SaveFailure()
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("saveFailure", Json{{"record", "SAVE\\player\\Ле Франсуа"},
                                        {"reason", "write temporary save: No space left on device"},
                                        {"space", true}});
    Expect(surface.alerts.size() == 1 && surface.alerts.front() == "<save.save_failed_space>" && surface.errors.empty(),
           "a save of the player on a full storage: the alert says the storage is full");
    router.SetState("saveFailure", nullptr);
    Expect(surface.alerts.size() == 1 && surface.errors.empty(), "null says nothing");
    router.SetState(
        "saveFailure",
        Json{{"record", "SAVE\\player\\Ле Франсуа"}, {"reason", "replace save: Permission denied"}, {"space", false}});
    Expect(surface.alerts.size() == 2 && surface.alerts.back() == "<save.save_failed>: Ле Франсуа",
           "another failure names the record");
    router.SetState("saveFailure", Json{{"record", "SAVE\\player\\Автосохранение 2"},
                                        {"reason", "write temporary save: No space left on device"},
                                        {"space", true}});
    Expect(surface.alerts.size() == 3 && surface.alerts.back() == "<save.autosave_failed_space>",
           "an autosave says it is the autosave");
    router.SetState("saveFailure", Json{{"record", "SAVE\\player\\x"}, {"reason", "?"}});
    Expect(surface.alerts.size() == 3 && surface.errors.size() == 1,
           "a malformed failure is an error, never a quiet drop");
}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    app_pack = argc > 2 ? argv[2] : "";
    GameMenu(models);
    GameOver(models);
    QuickSave(models);
    LoadFailure();
    SaveFailure();
    Saves(models);
    SavesRefused(models);
    Autosaves(models);
    Settings(models);
    Title(models);
    NewGame(models);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("menu screen checks passed\n");
    return failures ? 1 : 0;
}
