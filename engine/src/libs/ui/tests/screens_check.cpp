// Host checks for screen routing: admission (KNOWN / REQUIRED / extras), the generic
// classification and the mapping of component events to engine actions. Synthetic
// models only (tests/models); no GL context, no game data.
#include "fakes.h"
#include "../tools/script_game.h"
#include "../src/hud/hud.h"
#include "../src/picture_atlas.h"
#include "../src/screens/amount.h"
#include "../src/screens/controls.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <fstream>
#include <map>
#include <iterator>

using namespace storm::ui;
using namespace storm::ui::screens;

// The engine answers every widget action sent since the last answers (Router::Answer, as Host::Answer).
// The application's pack: the trade windows the application's scripts draw are checked on their script screens.
static std::string app_pack;

static Router &Scripted(Router &router)
{
    router.SetScriptScreens(ScriptGame::Get().Entries(app_pack));
    return router;
}

static void AnswerAll(Router &router, FakeSurface &surface, bool applied, const std::string &reason = {})
{
    for (; surface.answered < surface.actions.size(); ++surface.answered)
        if (const uint64_t request = surface.actions[surface.answered].request)
            router.Answer(request, applied, reason);
}

static Widget *Find(InterfaceModel &model, const char *node, const char *type)
{
    for (Widget &widget : model.widgets)
        if (widget.node == node && widget.type == type)
            return &widget;
    return nullptr;
}

static Widget Make(const char *node, const char *type, const char *label = "", std::vector<std::string> actions = {})
{
    Widget widget;
    widget.node = node;
    widget.type = type;
    widget.label = label;
    widget.actions = std::move(actions);
    return widget;
}

static void Admission_()
{
    static constexpr NodeRule rules[] = {
        {"TABLE_LIST", "choice", true}, {"INFO_TEXT", "text"}, {"QTY_*", "text|button"}};
    InterfaceModel model;
    model.widgets = {Make("table_list", "choice", "row", {"select"}),
                     Make("INFO_TEXT", "button", "Info", {"press"}),
                     Make("QTY_Result", "text", "42"),
                     Make("B_MOD", "button", "Mod", {"press"}),
                     Make("MAIN_BACK", "image"),
                     Make("SCROLL_LIST", "image"),
                     Make("DEBUG_INFO", "text", "fps")};
    const Admission admitted = Admit(model, rules);
    Expect(admitted.accepted, "required node present (case-insensitive) → accepted");
    Expect(admitted.known.size() == 2, "known: the table row and the QTY_ prefix text");
    Expect(admitted.extras.size() == 2, "a known node with an unexpected type and an unknown button are extras");
    Expect(admitted.One("QTY_RESULT") != nullptr && admitted.Label("qty_result") == "42", "lookup by node");

    model.widgets.erase(model.widgets.begin());
    Expect(!Admit(model, rules).accepted, "missing required node → not accepted");
    model.widgets.push_back(Make("TABLE_LIST", "text", "not a table"));
    Expect(!Admit(model, rules).accepted, "required node with another type → not accepted");

    Expect(Hidden(Make("ANY_SCROLL", "stepper")) && !Hidden(Make("SHIPS_SCROLL", "choice", "Ship", {"select"})),
           "scroll bars are hidden, scroll lists are not");
    Expect(Hidden(Make("EMPTY", "text")) && !Hidden(Make("NAME", "edit")), "empty text hidden, empty edit shown");
}

static void Roles()
{
    Expect(RoleOf(Make("EXIT_BTN", "button", "x")) == ButtonRole::Exit, "EXIT* node is the exit");
    Expect(RoleOf(Make("B1", "button", "Отмена")) == ButtonRole::Exit, "«Отмена» is the exit");
    Expect(RoleOf(Make("B_OK", "button", "Дальше")) == ButtonRole::Confirm, "*_OK node confirms");
    Expect(RoleOf(Make("B2", "button", "Готово")) == ButtonRole::Confirm, "«Готово» confirms");
    Expect(RoleOf(Make("B3", "button", "Удалить запись")) == ButtonRole::Danger, "«Удалить» is dangerous");
    Expect(RoleOf(Make("I_SHIP_2", "button", "Корабль")) == ButtonRole::Section &&
               SectionOf(Make("I_SHIP_2", "button")) == "ship",
           "section navigation");
    Expect(RoleOf(Make("B4", "button", "Подробнее")) == ButtonRole::Other, "anything else is secondary");
    // One case per shipped language (en is covered by the checks of the shared words above).
    Expect(RoleOf(Make("B5", "button", "Zurück")) == ButtonRole::Exit, "de: «Zurück» is the exit");
    Expect(RoleOf(Make("B6", "button", "Bestätigen")) == ButtonRole::Confirm, "de: «Bestätigen» confirms");
    Expect(RoleOf(Make("B7", "button", "Eintrag löschen")) == ButtonRole::Danger, "de: «löschen» is dangerous");
    Expect(RoleOf(Make("B8", "button", "Cancelar")) == ButtonRole::Exit, "es: «Cancelar» is the exit");
    Expect(RoleOf(Make("B9", "button", "Sí")) == ButtonRole::Confirm, "es: «Sí» confirms");
    Expect(RoleOf(Make("B10", "button", "Eliminar partida")) == ButtonRole::Danger, "es: «Eliminar» is dangerous");
    Expect(RoleOf(Make("B11", "button", "Annuler")) == ButtonRole::Exit, "fr: «Annuler» is the exit");
    Expect(RoleOf(Make("B12", "button", "Terminé")) == ButtonRole::Confirm, "fr: «Terminé» confirms");
    Expect(RoleOf(Make("B13", "button", "Supprimer la sauvegarde")) == ButtonRole::Danger,
           "fr: «Supprimer» is dangerous");
    Expect(RoleOf(Make("B14", "button", "Indietro")) == ButtonRole::Exit, "it: «Indietro» is the exit");
    Expect(RoleOf(Make("B15", "button", "Conferma")) == ButtonRole::Confirm, "it: «Conferma» confirms");
    Expect(RoleOf(Make("B16", "button", "Elimina partita")) == ButtonRole::Danger, "it: «Elimina» is dangerous");
}

static void Commands()
{
    Action action;
    action.screen = "store";
    action.opening = 7;
    action.node = "TABLE_LIST";
    action.index = 3;
    action.verb = "select";
    action.value = "row-3";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tselect@7:row-3", "versioned select");
    action.value.clear();
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tselect", "plain select");
    action.verb = "toggle";
    action.value = "1";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\t1", "check value");
    action.verb = "set";
    action.value = "25";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tset@7:25", "slider position");
    action.verb = "press";
    action.value.clear();
    action.guard = "row-3";
    ExpectEqual(EngineCommand(action), "widget:store\tTABLE_LIST\t3\tguard@7:row-3|press", "guarded press");
    Action back;
    back.screen = "store";
    back.verb = "back";
    ExpectEqual(EngineCommand(back), "back", "global back");
    Action plain;
    plain.node = "button-1";
    plain.verb = "click";
    ExpectEqual(EngineCommand(plain), "", "an action of a plain document is no engine command");
}

// Local feedback (Bindings::Predict): the result of a tap that needs no decision of the game is in the view the
// tap's own event builds; the game's steps before its answer do not change it; the game's answer confirms it, or
// corrects it with an error line.
static void Predictions(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "generic-settings.json");
    router.SetInterface(model);
    const auto toggle = [&]() -> const Json & { return surface.props["left"][0]["rows"][0]; };
    const std::string id = toggle()["id"];
    const auto game_sets = [&](const char *value) {
        for (Widget &widget : model.widgets)
            if (widget.node == "CHECK_OPTIONS" && widget.index == 0)
                widget.value = value;
        ++model.revision;
        router.SetInterface(model);
    };
    Expect(toggle()["on"] == true && router.Predicted() == 0, "the toggle is on, nothing predicted yet");

    router.OnEvent({id, "change", "0"});
    Expect(router.Predicted() == 1 && toggle()["on"] == false,
           "the tap's own build shows the toggle off, before any answer");
    Expect(router.Answering(), "the game is asked all the same");
    ++model.revision;
    router.SetInterface(model); // a step of the game that does not answer the tap: the toggle is still on there
    Expect(toggle()["on"] == false && surface.errors.empty(),
           "the game's steps before its answer do not change what was predicted");
    AnswerAll(router, surface, true);
    game_sets("0");
    Expect(toggle()["on"] == false && surface.errors.empty() && !router.Answering(),
           "the game's answer confirms it: nothing changes, nothing is logged");

    router.OnEvent({id, "change", "1"});
    Expect(toggle()["on"] == true, "the next tap is predicted too");
    AnswerAll(router, surface, true);
    ++model.revision;
    router.SetInterface(model); // the game answered and left the option off
    Expect(toggle()["on"] == false, "the game decided otherwise: its state is shown");
    Expect(surface.errors.size() == 1 &&
               surface.errors[0].find("UI prediction corrected: screen generic " + id + " change 1") == 0 &&
               surface.errors[0].find("/on: predicted true, the game has false") != std::string::npos,
           "and the correction is an error line that says what differed: " +
               (surface.errors.empty() ? std::string("none") : surface.errors[0]));
    surface.errors.clear();

    router.OnEvent({id, "change", "1"});
    Expect(toggle()["on"] == true, "predicted again");
    AnswerAll(router, surface, false, "locked");
    Expect(toggle()["on"] == false && surface.errors.size() == 1 &&
               surface.errors[0].find("UI prediction corrected") == 0,
           "a refusal corrects the prediction at once, loudly");
    surface.errors.clear();

    // An event whose handler asks the game nothing predicts nothing (the screen's own state is its result).
    Json view =
        Json{{"rows", Json::array({Json{{"id", "a"}, {"selected", true}}, Json{{"id", "b"}, {"selected", false}}})}};
    Bindings::SelectOnly(view["rows"], 1);
    Expect(view["rows"][0]["selected"] == false && view["rows"][1]["selected"] == true,
           "SelectOnly moves the selection");
    Expect(Bindings::Patch(view, "a", "on", true) && view["rows"][0]["on"] == true &&
               !Bindings::Patch(view, "nobody", "on", true),
           "Patch sets the field of the object with that id, and says when there is none");
}

// A hero section tapped in the header is selected in the tap's frame. Its press ends the interface and the next
// opens in a later game frame, with no model of this one in between: the prediction stays until the new screen
// replaces it, whatever comes meanwhile; a refused press corrects it loudly.
static void SectionPrediction(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "tradebook.json");
    router.SetInterface(model);
    Expect(surface.props["headerSection"] == "tradebook" && router.Predicted() == 0,
           "the trade book's own section is selected");

    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:tradebook\tI_SHIP\t-1\tpress", "the tap presses the ship section's button");
    Expect(router.Predicted() == 1 && surface.props["headerSection"] == "ship",
           "the tap's own build selects the ship section");
    AnswerAll(router, surface, true);
    router.SetInterface(model); // the engine's model after its answer: the trade book is still the game's interface
    Expect(surface.props["headerSection"] == "ship" && surface.errors.empty() && router.Answering(),
           "the applied press is awaited until the next interface");
    Expect(surface.props.contains("towns") && !surface.props["towns"].empty(), "the trade book's body stays meanwhile");
    const size_t sent = surface.actions.size();
    router.OnEvent({"header", "section", "items"});
    Expect(surface.actions.size() == sent && surface.props["headerSection"] == "ship" && surface.errors.empty() &&
               !surface.logs.empty() &&
               surface.logs.back().find("screen tradebook: section items tapped while ship opens") == 0,
           "another section tapped while the ship opens is not sent nor drawn, and is logged");
    InterfaceModel ship = Load(models, "ship.json");
    router.SetInterface(ship);
    Expect(surface.tag == "s-ship" && surface.props["headerSection"] == "ship" && surface.errors.empty() &&
               !router.Answering(),
           "the ship screen replaces it with the same section selected");

    router.SetInterface(model);
    router.OnEvent({"header", "section", "items"});
    Expect(surface.props["headerSection"] == "items", "predicted again");
    AnswerAll(router, surface, false, "locked");
    Expect(surface.props["headerSection"] == "tradebook" && surface.errors.size() == 1 &&
               surface.errors[0].find("UI prediction corrected: screen tradebook header section items") == 0 &&
               surface.errors[0].find("/headerSection: predicted \"items\", the game has \"tradebook\"") !=
                   std::string::npos,
           "a refused press shows the game's section again and logs the correction: " +
               (surface.errors.empty() ? std::string("none") : surface.errors[0]));
    Expect(!router.Answering(), "and nothing is awaited any more");
    surface.errors.clear();

    router.OnEvent({"header", "section", "tradebook"});
    Expect(router.Predicted() == 2 && surface.props["headerSection"] == "tradebook",
           "the section shown asks the game nothing and predicts nothing");
}

static void Generic(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "generic-split.json"));
    ExpectEqual(router.Using(), "generic", "unknown interface → generic layout");
    Expect(router.Rejection().empty() && surface.errors.empty(),
           "an unknown interface in the generic layout is no error");
    Expect(surface.logs.size() == 1 &&
               surface.logs[0].rfind("screen mod_bounty: no ported screen, generic layout; nodes=TITLE,TABLE_TARGETS",
                                     0) == 0,
           "generic use is logged once with the node set");
    router.SetInterface(Load(models, "generic-split.json"));
    Expect(surface.logs.size() == 1, "the same node set is not logged again");
    Expect(!router.OnEvent({"nobody", "click", ""}) && surface.errors.size() == 1 &&
               surface.errors[0] == "screen generic: no mapping for event nobody click ",
           "an event without a mapping is an error");
    surface.errors.clear();
    ExpectEqual(surface.tag, "s-generic", "generic screen mounted");
    ExpectEqual(surface.props.value("layout", ""), "split", "table + long text → Split");
    ExpectEqual(surface.props.value("title", ""), "Доска розыска", "heading → title");
    Expect(surface.props["left"].size() == 1 && surface.props["left"][0]["kind"] == "table", "main block is the table");
    Expect(surface.props["left"][0]["rows"].size() == 5 && surface.props["left"][0]["rows"][4]["disabled"] == true,
           "disabled row keeps its reason");
    Expect(surface.props["right"].size() == 1 && surface.props["right"][0]["paragraphs"].size() == 2,
           "detail = the text, two paragraphs");
    Expect(surface.props["actions"].size() == 2 && surface.props["actions"][1]["variant"] == "primary" &&
               surface.props["actions"][1]["label"] == "Взять",
           "«Взять» is the primary, bottom-right");
    Expect(surface.props["extras"]["actions"].empty(), "no «⋯» without overflow");

    Expect(router.OnEvent({"g0", "row", "2"}), "table row event is bound");
    ExpectEqual(surface.Last(), "widget:mod_bounty\tTABLE_TARGETS\t2\tselect@7:t2", "row tap selects by row identity");
    router.OnEvent({"g0", "row", "4"});
    Expect(surface.commands.size() == 1, "a row without verbs sends nothing");
    router.OnEvent({"g-primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:mod_bounty\tB_TAKE\t-1\tpress", "primary presses its button");
    Expect(surface.props["actions"][1]["busy"] == true, "pressed button is busy until the next revision");
    InterfaceModel next = Load(models, "generic-split.json");
    next.revision = 2;
    router.SetInterface(next);
    Expect(surface.props["actions"][1]["busy"] == false, "next revision clears busy");
    router.OnEvent({"g-primary", "click", ""});
    Expect(surface.props["actions"][1]["busy"] == true && surface.actions.back().request != 0,
           "a widget press carries a request and looks busy");
    AnswerAll(router, surface, true);
    Expect(surface.props["actions"][1]["busy"] == true, "applied: busy until the model that follows");
    router.SetInterface(next); // the engine sends the model again after its answer, unchanged here
    Expect(surface.props["actions"][1]["busy"] == false,
           "the model after the answer ends busy, without a new revision");
    router.OnEvent({"g-primary", "click", ""});
    AnswerAll(router, surface, false, "the button cannot be pressed now");
    Expect(surface.props["actions"][1]["busy"] == false && !surface.logs.empty() &&
               surface.logs.back().find("cannot be pressed") != std::string::npos,
           "a refusal ends busy at once and is logged");
    const size_t errors_before = surface.errors.size();
    router.OnEvent({"g-primary", "click", ""});
    surface.now = 6.0;
    router.Tick();
    Expect(surface.props["actions"][1]["busy"] == false && surface.errors.size() == errors_before + 1,
           "no answer within 5 s is an error, and the press stops looking busy");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:mod_bounty\tEXIT_BTN\t-1\tpress", "← presses the exit button");

    router.SetInterface(Load(models, "generic-settings.json"));
    ExpectEqual(surface.props.value("layout", ""), "settings", "controls only → Settings");
    const Json rows = surface.props["left"][0]["rows"];
    Expect(rows.size() == 6, "two toggles, one segment, slider, field, stepper");
    Expect(rows[2]["kind"] == "segment" && rows[2]["label"] == "Сложность" && rows[2]["selected"] == 1,
           "radio group → segment named by the preceding text");
    Expect(rows[3]["kind"] == "slider" && rows[3]["value"] == 60, "slider shows percent of its range");
    Expect(surface.props["danger"]["label"] == "Удалить настройки", "danger button on the left");
    const std::string prefix = "widget:mod_options\t";
    router.OnEvent({rows[0]["id"], "change", "0"});
    ExpectEqual(surface.Last(), prefix + "CHECK_OPTIONS\t0\t0", "toggle off");
    router.OnEvent({rows[2]["id"], "change", "2"});
    ExpectEqual(surface.Last(), prefix + "CHECK_DIFF\t2\t1", "segment chooses the option");
    router.OnEvent({rows[3]["id"], "commit", "47"});
    ExpectEqual(surface.Last(), prefix + "SLIDE_VOL\t-1\tset@7:25", "slider percent → position snapped to the step");
    router.OnEvent({rows[4]["id"], "focus", "Аделина"});
    ExpectEqual(surface.text.text, "Аделина", "field asks for the editor with its text");
    router.SubmitText(surface.text.token, "Мария", true);
    ExpectEqual(surface.Last(), prefix + "EDIT_NAME\t-1\tМария", "accepted text goes to the edit box");
    router.OnEvent({rows[5]["id"].get<std::string>() + "-right", "click", ""});
    ExpectEqual(surface.Last(), prefix + "FLAG_CHANGER\t-1\tright", "stepper arrow");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "B_CANCEL\t-1\tpress", "«Отмена» is not drawn: ← sends it");

    // More than three buttons: two stay, the rest go under «⋯».
    InterfaceModel many;
    many.screen = "mod_many";
    many.opening = 3;
    many.widgets = {Make("T", "text", "Выберите действие"), Make("B_A", "button", "Раз", {"press"}),
                    Make("B_B", "button", "Два", {"press"}), Make("B_C", "button", "Три", {"press"}),
                    Make("B_D", "button", "Четыре", {"press"})};
    router.SetInterface(many);
    ExpectEqual(surface.props.value("layout", ""), "reading", "text and buttons only → Reading");
    Expect(surface.props["actions"].size() == 2 && surface.props["extras"]["rows"].size() == 2 &&
               surface.props["extras"]["actions"].size() == 1,
           "overflow buttons sit in the «Прочее» sheet");
    router.OnEvent({"extras-more", "click", ""});
    Expect(surface.props["extras"]["open"] == true, "«⋯» opens the sheet");
    router.OnEvent({surface.props["extras"]["rows"][1]["id"], "click", ""});
    ExpectEqual(surface.Last(), "widget:mod_many\tB_D\t-1\tpress", "sheet row presses its button");
    Expect(surface.props["extras"]["open"] == false, "the sheet closes after an action");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "back", "no exit button → global back");

    // Section buttons of an interface with no ported screen are rows of «Прочее»; their
    // captions leave the content.
    InterfaceModel book;
    book.screen = "mod_journal";
    book.opening = 5;
    book.widgets = {Make("TITLE", "heading", "Журнал торговли"), Make("I_SHIP", "button", "Корабли", {"press"}),
                    Make("I_SHIP_2", "button", "Корабли", {"press"}), Make("STR_BUTTONS", "text", "Корабли")};
    router.SetInterface(book);
    Expect(!surface.props.contains("sections") && surface.props["left"].empty() &&
               surface.props["extras"]["rows"].size() == 1,
           "section buttons leave the content for «Прочее», their captions with them");

    router.CloseInterface();
    Expect(surface.tag.empty() && !router.Active(), "closing unmounts the screen");
}

// The shared quantity flow (AmountDraft, controls.h) on one open quantity control: changing the amount (steps,
// a drag, its end) sends nothing to the game, however long it settles; the confirm button sends exactly one move
// (`one_move` checks its commands); a second press before the game answers sends nothing more. `moves` tells a
// command that moves something. A transfer (`origin` >= 0, what our side holds) has no «Отмена»
// at all: the amount set back to the origin drops the draft. A setting kept as a draft (`origin` < 0:
// the treasurer's order) keeps «Отмена», which drops it; `reopen` then answers what it did.
static void DraftFlow(const std::string &name, Router &router, FakeSurface &surface, const std::string &control,
                      int value, const std::string &label, const std::function<bool(const std::string &)> &moves,
                      const std::function<bool(const std::vector<std::string> &)> &one_move,
                      const std::function<void()> &reopen, int origin = -1)
{
    const auto dump = [&surface]() { return Json(surface.commands).dump(); };
    surface.commands.clear();
    router.OnEvent({control, "change", std::to_string(value - 1)});
    router.OnEvent({control, "change", std::to_string(value)});
    router.OnEvent({control, "commit", std::to_string(value)});
    surface.now += 2.0;
    router.Tick();
    Expect(surface.commands.empty(), name + ": a quantity change sends nothing to the game: " + dump());
    const Json &actions = surface.props["actions"];
    const bool cancel = actions.is_array() && std::any_of(actions.begin(), actions.end(), [](const Json &a) {
                            return a.value("id", "") == "deal-cancel";
                        });
    Expect(surface.props["primary"]["label"] == label && surface.props["primary"]["disabled"] == false,
           name + ": the changed amount offers its confirm: " + surface.props["primary"].dump());
    if (origin >= 0)
    {
        Expect(!cancel, name + ": a transfer has no «Отмена»: " + actions.dump());
        router.OnEvent({control, "commit", std::to_string(origin)});
        router.Tick();
        Expect(std::none_of(surface.commands.begin(), surface.commands.end(), moves) &&
                   surface.props["primary"]["label"] != label,
               name + ": the amount set back drops the draft, moving nothing: " + dump());
    }
    else
    {
        Expect(cancel && actions.back()["label"] == "<common.cancel>",
               name + ": a setting offers «Отмена»: " + actions.dump());
        router.OnEvent({"deal-cancel", "click", ""});
        router.Tick();
        Expect(std::none_of(surface.commands.begin(), surface.commands.end(), moves),
               name + ": «Отмена» moves nothing: " + dump());
        Expect(surface.props["primary"]["label"] != label, name + ": «Отмена» drops the amount set");
        reopen();
    }
    router.OnEvent({control, "commit", std::to_string(value)});
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(one_move(surface.commands), name + ": the confirm sends one move: " + dump());
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(surface.commands.empty() && surface.props["primary"]["busy"] == true,
           name + ": a second press before the game answers sends nothing: " + dump());
}

// The game's amount window answers what was offered (store.c ChangeQTY_EDIT clamps it and writes the result): our side
// after the deal in QTY_INFO_SHIP_QTY, a store's «Деньги N, вес W ц» in QTY_Result, the hold after it in CAPACITY
// (empty: unchanged); the model after the answers is the next revision.
static void GameAnswers(Router &router, FakeSurface &surface, InterfaceModel &model, int ours,
                        const std::string &result = {}, const std::string &capacity = {})
{
    AnswerAll(router, surface, true);
    for (Widget &widget : model.widgets)
    {
        if (widget.node == "QTY_INFO_SHIP_QTY")
            widget.label = std::to_string(ours);
        if (widget.node == "QTY_Result" && !result.empty())
            widget.label = result;
        if (widget.node == "CAPACITY" && !capacity.empty())
            widget.label = capacity;
    }
    ++model.revision;
    router.SetInterface(model);
}

// The trade screens' deal on one open amount window of `model` (our side `origin`): a move offers its change to the
// game's window at once (`edit` is the prefix of its commands, a zero first); the control and the button then show
// what the game answered (`answered`, `label`); the amount set back to the origin leaves nothing to confirm; the
// confirm sends the game's amount and its OK (`ok`); a second press before the game answers sends nothing.
static void OfferFlow(const std::string &name, Router &router, FakeSurface &surface, InterfaceModel model, int origin,
                      int value, int answered, const std::string &label, const std::string &edit, const std::string &ok)
{
    const auto dump = [&surface]() { return Json(surface.commands).dump(); };
    router.SetInterface(model);
    surface.commands.clear();
    router.OnEvent({"goods", "change", std::to_string(value)});
    Expect(surface.commands == std::vector<std::string>{edit + "0", edit + std::to_string(value - origin)},
           name + ": a move offers its amount to the game's window: " + dump());
    GameAnswers(router, surface, model, answered);
    Expect(surface.props["deal"]["value"] == answered && surface.props["primary"]["label"] == label &&
               surface.props["primary"]["disabled"] == false,
           name + ": the control and the button show the game's amount: " + surface.props["primary"].dump());
    router.OnEvent({"goods", "change", std::to_string(origin)});
    GameAnswers(router, surface, model, origin);
    Expect(surface.props["primary"].is_null() || surface.props["primary"]["label"] != label,
           name + ": the amount set back leaves nothing to confirm: " + surface.props["primary"].dump());
    router.OnEvent({"goods", "change", std::to_string(value)});
    GameAnswers(router, surface, model, answered);
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(surface.commands == std::vector<std::string>{edit + "0", edit + std::to_string(answered - origin), ok},
           name + ": the confirm sends the game's amount and its OK: " + dump());
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(surface.commands.empty() && surface.props["primary"]["busy"] == true,
           name + ": a second press before the game answers sends nothing: " + dump());
}

// The store on one router: the deal of the selected row, its amounts and limits, the confirmation, leaving, the ship
// switch and the rejection of an incomplete interface.
struct StoreRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    InterfaceModel open_deal; // store-deal.json, from DealOpens on
    StoreRun(std::string path) : models(std::move(path))
    {
        Scripted(router);
    }

    Json Quantity()
    {
        return surface.props["deal"];
    }

    void Opening()
    {
        router.SetInterface(Load(models, "store.json"));
        ExpectEqual(router.Using(), "store", "store is a ported screen");
        ExpectEqual(surface.tag, "s-trade", "store screen mounted");
        Expect(surface.props["list"]["rows"].size() == 8 && surface.props["primary"].is_null(),
               "goods listed, no deal yet");
        ExpectEqual(surface.props["extras"]["note"], "Членам купеческой гильдии — скидка 5 % до 1 декабря",
                    "unknown text → parchment note");
        Expect(surface.props["extras"]["rows"].size() == 1 &&
                   surface.props["extras"]["rows"][0]["title"] == "Гильдейский счёт",
               "unknown button → «Прочее» row");
        Expect(surface.logs.size() == 1 && surface.logs[0] == "screen store extras=B_GUILD,GUILD_NOTE",
               "extras logged once");
        ExpectEqual(surface.Last(), "widget:store\tTABLE_LIST\t0\tactivate@7:goods:0:ship:0",
                    "a shop opens the deal of the row the game selected");
        surface.now += 0.6;
        router.Tick();
        Expect(surface.errors.size() == 1, "no window for it within the hold: said");
        surface.errors.clear();
        InterfaceModel one_line = Load(models, "store.json");
        for (Widget &widget : one_line.widgets)
            if (widget.node == "CAPACITY")
                widget.label = "Дедвейт: 189 / 700"; // as the game writes it
        router.SetInterface(one_line);
        Expect(surface.props["stats"][0]["icon"] == "capacity" && surface.props["stats"][0]["value"] == "189 / 700",
               "a one-line figure is split at its colon; the hold is its icon");
        Expect(surface.logs.size() == 1 && surface.errors.empty(), "the same node set is not logged again");
    }

    void RowTaps()
    {
        router.OnEvent({"goods", "row", "1"});
        ExpectEqual(surface.Last(), "widget:store\tTABLE_LIST\t1\tactivate@7:goods:1:ship:0",
                    "row tap opens the deal for that row");
        {
            FakeSurface sel_surface;
            Router sel(sel_surface);
            Scripted(sel);
            sel.SetInterface(Load(models, "store.json"));
            sel.OnEvent({"goods", "row", "1"});
            // The game selects the tapped row in one loop and opens its window in the next: the frame
            // between is not shown, the selection and its deal appear together.
            InterfaceModel chosen = Load(models, "store.json");
            chosen.revision = 30;
            for (Widget &widget : chosen.widgets)
                if (widget.node == "TABLE_LIST")
                    widget.value = widget.index == 1 ? "1" : "0";
            const Json before = sel_surface.props;
            AnswerAll(sel, sel_surface, true);
            sel.SetInterface(chosen);
            Expect(sel_surface.props == before,
                   "the selection without its deal is held back: the screen stays as it was");
            InterfaceModel opened = Load(models, "store-deal.json");
            opened.revision = 31;
            for (Widget &widget : opened.widgets)
                if (widget.node == "TABLE_LIST")
                    widget.value = widget.index == 1 ? "1" : "0";
            sel.SetInterface(opened);
            Expect(sel_surface.props["list"]["rows"][1]["selected"] == true && sel_surface.props.contains("deal") &&
                       !sel_surface.props["deal"].is_null(),
                   "the deal arrives: the row selected and its deal in the same frame: " +
                       sel_surface.props["list"]["rows"][1].dump());
            // A tap whose window never comes is shown selected after the hold (and said).
            sel.OnEvent({"goods", "row", "2"});
            InterfaceModel lone = Load(models, "store.json");
            lone.revision = 32;
            for (Widget &widget : lone.widgets)
                if (widget.node == "TABLE_LIST")
                    widget.value = widget.index == 2 ? "1" : "0";
            AnswerAll(sel, sel_surface, true);
            sel.SetInterface(lone);
            Expect(sel_surface.props["list"]["rows"][2]["selected"] != true, "held while its window may still come");
            sel_surface.now += 0.6;
            sel.Tick();
            Expect(sel_surface.props["list"]["rows"][2]["selected"] == true && !sel_surface.errors.empty(),
                   "no window within the hold: shown selected, and said: " +
                       (sel_surface.errors.empty() ? std::string() : sel_surface.errors.back()));
            sel_surface.errors.clear();
        }
        {
            // A tap the game refuses (regression: "the game refused TABLE_LIST: the node did not take it", then 0.5 s
            // later "opened no window ... shown selected"): the refusal is the answer, the wait ends at once with no
            // error, and the game's own selection stays shown (the refused row is not shown selected).
            FakeSurface ref_surface;
            Router ref(ref_surface);
            Scripted(ref);
            ref.SetInterface(Load(models, "store.json"));
            AnswerAll(ref, ref_surface, true);
            ref_surface.errors.clear();
            ref.OnEvent({"goods", "row", "3"});
            ExpectEqual(ref_surface.Last(), "widget:store\tTABLE_LIST\t3\tactivate@7:goods:3:ship:0",
                        "the tapped row's deal is asked for");
            AnswerAll(ref, ref_surface, false,
                      "the node did not take it: 'activate@7:goods:3:ship:0' is not for tr4 now");
            Expect(ref_surface.props["list"]["rows"][3]["selected"] != true &&
                       ref_surface.props["list"]["rows"][0]["selected"] == true,
                   "refused: the game's selection is shown at once, not the refused row: " +
                       ref_surface.props["list"]["rows"].dump());
            const size_t sent = ref_surface.actions.size();
            ref_surface.now += 0.6;
            ref.Tick();
            Expect(ref_surface.errors.empty(),
                   "refused: no hold error after kSelectHold: " +
                       (ref_surface.errors.empty() ? std::string() : ref_surface.errors.back()));
            Expect(ref_surface.actions.size() == sent, "refused: nothing is retried");
            ref.OnEvent({"goods", "row", "2"});
            ExpectEqual(ref_surface.Last(), "widget:store\tTABLE_LIST\t2\tactivate@7:goods:2:ship:0",
                        "the next tap is not blocked by the refused one");
        }
    }

    void DealOpens()
    {
        // The open deal (store-deal.json: «Ядра», 200 in the hold, 1 840 in the store; the hold 189 / 199, the purse
        // 1 500). Fresh, the game's window holds nothing: the control is at the pivot, nothing to confirm.
        open_deal = Load(models, "store-deal.json");
        InterfaceModel fresh = open_deal;
        for (Widget &widget : fresh.widgets)
            if (widget.node == "QTY_INFO_SHIP_QTY" || widget.node == "QTY_Result")
                widget.label = widget.node == "QTY_Result" ? "" : "200";
        router.SetInterface(fresh);
        Expect(Quantity()["value"] == 200 && Quantity()["origin"] == 200 && Quantity()["min"] == 0 &&
                   Quantity()["max"] == 2040 && surface.props["primary"].is_null(),
               "a deal opens where the game's window is, between all we hold and all they have: " + Quantity().dump());
        Expect(Quantity()["zones"] == true && Quantity()["lessLabel"] == "<trade.sell>" &&
                   Quantity()["moreLabel"] == "<trade.buy>",
               "trade zones: selling left of the pivot, buying right");
        // The game's window holds 100 bought (QTY_INFO_SHIP_QTY 300, «Деньги 25, вес 5 ц», its hold after it 194).
        surface.commands.clear();
        router.OnEvent({"goods", "change", "300"});
        GameAnswers(router, surface, open_deal, 300, "Деньги 25, вес 5 ц", "Трюм:\n194 / 199");
        Expect(Quantity()["value"] == 300 && surface.props["primary"]["label"] == "<trade.buy> 100" &&
                   surface.props["primary"]["sub"] == "25 \xC2\xB7 5 <transfer.cwt>",
               "100 bought: the game's money and weight for it: " + surface.props["primary"].dump());
        Expect(surface.props["stats"][0]["value"] == "189 / 199" &&
                   surface.props["stats"][0]["preview"] == "194 / 199" &&
                   surface.props["stats"][1]["value"] == "1 500" && !surface.props["stats"][1].contains("preview"),
               "the header shows the game's hold after the deal 189 → 194; the purse is the game's 1 500 until the "
               "deal is made: " +
                   surface.props["stats"].dump());
    }

    void DealLimits()
    {
        {
            // The game republishes its window while the amount is chosen: the control follows what it holds.
            InterfaceModel again = open_deal;
            GameAnswers(router, surface, again, 250, "Деньги 13, вес 3 ц");
            Expect(Quantity()["value"] == 250 && surface.props["primary"]["label"] == "<trade.buy> 50",
                   "the control shows what the game's window holds");
        }
        // A sale past what the game lets go (store.c cuts a sale by the store's room and, at sea, by the other ship's
        // money): all 200 offered, the game's window answers 10 sold; the control shows it and says it stopped there.
        surface.commands.clear();
        surface.toasts.clear();
        router.OnEvent({"goods", "change", "0"});
        const std::string edit = "widget:store\tQTY_EDIT\t-1\tguard@7:goods:0:ship:0|";
        Expect(surface.commands == std::vector<std::string>{edit + "0", edit + "-200"},
               "all we hold offered for sale: " + Json(surface.commands).dump());
        GameAnswers(router, surface, open_deal, 190, "Деньги 2, вес 1 ц", "Трюм:\n188 / 199");
        Expect(Quantity()["value"] == 190 && surface.props["primary"]["label"] == "<trade.sell> 10" &&
                   surface.props["primary"]["sub"] == "2 \xC2\xB7 1 <transfer.cwt>" &&
                   surface.toasts == std::vector<std::string>{"<trade.limit>"} &&
                   surface.props["stats"][0]["preview"] == "188 / 199" &&
                   !surface.props["stats"][1].contains("preview"),
               "a sale past the game's limit shows the amount it clamped it to, its money and weight, and says so: " +
                   surface.props["primary"].dump() + Json(surface.toasts).dump());
    }

    void Confirm()
    {
        // The confirmation: zero and the signed amount (zero leaves a previous «give»), then the game's OK.
        router.OnEvent({"goods", "change", "300"});
        GameAnswers(router, surface, open_deal, 300, "Деньги 25, вес 5 ц");
        surface.commands.clear();
        router.OnEvent({"primary", "click", ""});
        router.Tick();
        {
            const std::string edit = "widget:store\tQTY_EDIT\t-1\tguard@7:goods:0:ship:0|";
            Expect(surface.commands.size() == 3 && surface.commands[0] == edit + "0" &&
                       surface.commands[1] == edit + "100" &&
                       surface.commands[2] == "widget:store\tQTY_OK_BUTTON\t-1\tguard@7:goods:0:ship:0|press",
                   "«Купить 100»: the amount and the game's OK in one flush: " + Json(surface.commands).dump());
            // The game closes its window and keeps the row selected: the row's deal is opened again, so the dock keeps
            // its amount and button for the next deal (before: the row's name with nothing to do until a new tap).
            InterfaceModel closed = Load(models, "store.json");
            closed.revision = open_deal.revision + 10;
            surface.commands.clear();
            AnswerAll(router, surface, true);
            router.SetInterface(closed);
            router.Tick();
            ExpectEqual(surface.Last(), "widget:store\tTABLE_LIST\t0\tactivate@7:goods:0:ship:0",
                        "a confirmed deal's row opens its deal again");
            InterfaceModel reopened = open_deal;
            reopened.revision = closed.revision + 1;
            AnswerAll(router, surface, true);
            router.SetInterface(reopened);
            router.OnEvent({"goods", "change", "300"});
            router.OnEvent({"primary", "click", ""}); // confirmed, then left with ← before the game answered
            router.Tick();
            surface.commands.clear();
            closed.revision = reopened.revision + 1;
            router.OnEvent({"header-back", "click", ""});
            AnswerAll(router, surface, true);
            router.SetInterface(closed);
            router.Tick();
            Expect(std::none_of(surface.commands.begin(), surface.commands.end(),
                                [](const std::string &c) { return c.find("activate@") != std::string::npos; }),
                   "a deal closed with ← is not opened again: " + Json(surface.commands).dump());
            reopened.revision = closed.revision + 1;
            router.SetInterface(reopened);
        }
        {
            // The store's «Купить / Продать N» is the trade screens' shared flow.
            const std::string edit = "widget:store\tQTY_EDIT\t-1\tguard@7:goods:0:ship:0|";
            FakeSurface draft_surface;
            Router draft_router(draft_surface);
            Scripted(draft_router);
            draft_router.SetInterface(open_deal);
            OfferFlow("store", draft_router, draft_surface, open_deal, 200, 150, 150, "<trade.sell> 50", edit,
                      "widget:store\tQTY_OK_BUTTON\t-1\tguard@7:goods:0:ship:0|press");
        }
    }

    void Leaving()
    {
        surface.commands.clear();
        router.OnEvent({"goods", "row", "2"});
        Expect(surface.commands.size() == 2 &&
                   surface.commands[0] == "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:0:ship:0|press" &&
                   surface.commands[1] == "widget:store\tTABLE_LIST\t2\tactivate@7:goods:2:ship:0",
               "another row: cancel the open deal, then open the new one");
        // The game's exit only closes an open amount window (store.c ProcessCancelExit) and the dock always has one
        // open: ← sends the game's exit twice, the first closes the window, the second leaves.
        surface.commands.clear();
        router.OnEvent({"header-back", "click", ""});
        const std::vector<std::string> leaves{"back",
                                              "back"}; // this model's EXIT_BTN offers no press: the interface's back
        Expect(surface.commands == leaves, "one ← closes the deal and leaves: " + Json(surface.commands).dump());
        surface.commands.clear();
        router.Back();
        Expect(surface.commands == leaves, "the platform's back does what ← does: " + Json(surface.commands).dump());
        router.OnEvent({"extras-more", "click", ""});
        surface.commands.clear();
        router.Back();
        Expect(surface.commands.empty() && surface.props["extras"]["open"] != true,
               "back closes the «Прочее» sheet before anything else");
    }

    void ShipSwitch()
    {
        {
            // The ship switch stays while a deal is open: the deal is closed with the game's cancel (its ProcessFrame
            // hides the window but leaves bShowChangeWin set), the game's own select switches the ship, and once the
            // game has bound the new ship (the next model) the same goods' deal opens again, else the row the game
            // selected.
            const auto deal_on = [this](int row, int ship, uint64_t revision, bool open) {
                InterfaceModel model = Load(models, "store-deal.json");
                if (!open)
                    std::erase_if(model.widgets,
                                  [](const Widget &widget) { return widget.node.rfind("QTY_", 0) == 0; });
                for (Widget &widget : model.widgets)
                {
                    if (widget.node == "TABLE_LIST")
                    {
                        widget.value = widget.index == row ? "1" : "0";
                        widget.row_id = "goods:" + std::to_string(widget.index) +
                                        ":ship:" + std::to_string(ship); // the engine's row ids name the owner
                    }
                    if (widget.node == "SHIPS_SCROLL")
                        widget.value = widget.index == ship ? "1" : "0";
                }
                model.revision = revision;
                return model;
            };
            FakeSurface ship_surface;
            Router ship_router(ship_surface);
            Scripted(ship_router);
            ship_router.SetInterface(deal_on(2, 0, 20, true));
            const Json subject = ship_surface.props["subject"];
            Expect(!ship_surface.props["deal"].is_null() && subject["prev"] == false && subject["next"] == true &&
                       subject["sub"] == "1 <common.of> 2",
                   "a deal is open and the ship arrows are there: " + subject.dump());
            ship_surface.commands.clear();
            ship_router.OnEvent({"header", "subject-next", ""});
            Expect(ship_surface.commands.size() == 2 &&
                       ship_surface.commands[0] ==
                           "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:2:ship:0|press" &&
                       ship_surface.commands[1] == "widget:store\tSHIPS_SCROLL\t1\tselect@7:ship:1",
                   "› during a deal: the game's cancel of the deal, then the game's select of the next ship: " +
                       Json(ship_surface.commands).dump());
            // The selector moved; the game binds the ship in its next frame (refCharacter, the table, the first row).
            ship_surface.commands.clear();
            AnswerAll(ship_router, ship_surface, true);
            ship_router.SetInterface(deal_on(2, 1, 21, false));
            Expect(ship_surface.commands.empty() && ship_surface.errors.empty(),
                   "nothing is asked for before the game has switched the ship");
            ship_router.SetInterface(deal_on(0, 1, 22, false));
            ExpectEqual(ship_surface.Last(), "widget:store\tTABLE_LIST\t2\tactivate@7:goods:2:ship:1",
                        "the same goods' deal opens for the new ship");
            AnswerAll(ship_router, ship_surface, true);
            ship_router.SetInterface(deal_on(2, 1, 23, true));
            Expect(!ship_surface.props["deal"].is_null() && ship_surface.props["list"]["rows"][2]["selected"] == true &&
                       ship_surface.props["subject"]["prev"] == true &&
                       ship_surface.props["subject"]["next"] == false && ship_surface.errors.empty(),
                   "the deal is the new ship's, with the arrows: " + ship_surface.props["subject"].dump());

            // The new ship's list without those goods: the row the game selected.
            ship_surface.commands.clear();
            ship_router.OnEvent({"header", "subject-prev", ""});
            ExpectEqual(ship_surface.Last(), "widget:store\tSHIPS_SCROLL\t0\tselect@7:ship:0", "‹ switches back");
            AnswerAll(ship_router, ship_surface, true);
            ship_router.SetInterface(deal_on(2, 0, 24, false));
            InterfaceModel without = deal_on(0, 0, 25, false);
            std::erase_if(without.widgets,
                          [](const Widget &widget) { return widget.node == "TABLE_LIST" && widget.index == 2; });
            ship_surface.commands.clear();
            ship_router.SetInterface(without);
            ExpectEqual(ship_surface.Last(), "widget:store\tTABLE_LIST\t0\tactivate@7:goods:0:ship:0",
                        "goods the new ship's list lacks: the game's first row");

            // A switch the game answers with no new model (nothing it shows changed): opened after the wait.
            AnswerAll(ship_router, ship_surface, true);
            ship_router.SetInterface(deal_on(0, 0, 26, true));
            ship_router.OnEvent({"header", "subject-next", ""});
            AnswerAll(ship_router, ship_surface, true);
            ship_router.SetInterface(deal_on(0, 1, 27, false));
            ship_surface.commands.clear();
            ship_surface.now += 0.6;
            ship_router.Tick();
            ExpectEqual(ship_surface.Last(), "widget:store\tTABLE_LIST\t0\tactivate@7:goods:0:ship:1",
                        "no newer model within the wait: opened on these rows");

            // An arrow tapped while a tapped row's window is on its way: the switch waits for that window, then closes
            // it.
            AnswerAll(ship_router, ship_surface, true);
            ship_router.SetInterface(deal_on(0, 1, 28, true));
            ship_router.OnEvent({"goods", "row", "1"});
            ship_surface.commands.clear();
            ship_router.OnEvent({"header", "subject-prev", ""});
            Expect(ship_surface.commands.empty(),
                   "the switch waits for the tapped row's window: " + Json(ship_surface.commands).dump());
            AnswerAll(ship_router, ship_surface, true);
            ship_router.SetInterface(deal_on(1, 1, 29, true));
            Expect(ship_surface.commands.size() == 2 &&
                       ship_surface.commands[0] ==
                           "widget:store\tQTY_CANCEL_BUTTON\t-1\tguard@7:goods:1:ship:1|press" &&
                       ship_surface.commands[1] == "widget:store\tSHIPS_SCROLL\t0\tselect@7:ship:0",
                   "the window came: closed, then the ship switched: " + Json(ship_surface.commands).dump());
            Expect(ship_surface.errors.empty(), "no errors in the ship switch: " + Json(ship_surface.errors).dump());

            // Regression: a row tapped right after a switch, before the game bound the new ship (its rows are still the
            // old ship's under the new ship's identities, so the game refuses that tap): nothing goes out; the tapped
            // goods open from the new ship's list once it is in.
            FakeSurface tap_surface;
            Router tap_router(tap_surface);
            Scripted(tap_router);
            tap_router.SetInterface(deal_on(0, 0, 40, false));
            AnswerAll(tap_router, tap_surface, true);
            tap_router.SetInterface(deal_on(0, 0, 41, true));
            tap_router.OnEvent({"header", "subject-next", ""});
            tap_surface.commands.clear();
            tap_router.OnEvent(
                {"goods", "row", "1"}); // emulator: a tap right after ›, on the rows of the model before the switch
            Expect(tap_surface.commands.empty(),
                   "a row tapped before the switch is in the model is not sent: " + Json(tap_surface.commands).dump());
            AnswerAll(tap_router, tap_surface, true);
            tap_router.SetInterface(deal_on(0, 1, 42, false));
            tap_router.OnEvent(
                {"goods", "row", "3"}); // the selector moved, the rows are still the old ship's: the newer tap counts
            Expect(tap_surface.commands.empty(),
                   "a row tapped before the game bound the new ship is not sent: " + Json(tap_surface.commands).dump());
            InterfaceModel bound = deal_on(0, 1, 43, false);
            // the new ship's list lacks «Картечь»: «Бомбы» (row 3 of the old list) is its row 2
            std::erase_if(bound.widgets,
                          [](const Widget &widget) { return widget.node == "TABLE_LIST" && widget.index == 1; });
            for (Widget &widget : bound.widgets)
                if (widget.node == "TABLE_LIST" && widget.index > 1)
                    widget.row_id = "goods:" + std::to_string(--widget.index) + ":ship:1";
            tap_router.SetInterface(bound);
            ExpectEqual(tap_surface.Last(), "widget:store\tTABLE_LIST\t2\tactivate@7:goods:2:ship:1",
                        "the tapped goods open from the new ship's list");
            Expect(tap_surface.errors.empty(),
                   "no errors for a tap during the switch: " + Json(tap_surface.errors).dump());

            // A switch the game refuses: row taps stop waiting for it.
            tap_router.SetInterface(deal_on(2, 1, 44, false));
            AnswerAll(tap_router, tap_surface, true);
            tap_router.OnEvent({"header", "subject-prev", ""});
            AnswerAll(tap_router, tap_surface, false, "the owner list changed");
            tap_surface.commands.clear();
            tap_router.OnEvent({"goods", "row", "4"});
            ExpectEqual(tap_surface.Last(), "widget:store\tTABLE_LIST\t4\tactivate@7:goods:4:ship:1",
                        "after a refused switch a tap goes out at once");
        }
    }

    void Rejection()
    {
        // Without its required table the store falls back to the generic layout.
        InterfaceModel broken = Load(models, "store.json");
        broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(),
                                            [](const Widget &w) { return w.node == "TABLE_LIST"; }),
                             broken.widgets.end());
        broken.opening = 8;
        surface.now = 10.0;
        router.SetInterface(broken);
        Expect(surface.tag.empty() && surface.errors.empty(),
               "a fresh opening that is not complete yet waits, unreported");
        InterfaceModel filled = Load(models, "store.json");
        filled.opening = 8;
        filled.revision = 2;
        router.SetInterface(filled);
        Expect(router.Using() == "store" && surface.errors.empty(),
               "the ported screen when the game has filled the interface");
        broken.opening = 9;
        const size_t toasts = surface.toasts.size();
        router.SetInterface(broken);
        surface.now += 1.0;
        router.Tick();
        Expect(!router.Active() && surface.tag.empty() && router.Using().empty(),
               "store without TABLE_LIST: nothing stands in for its screen");
        ExpectEqual(router.Rejection(), "TABLE_LIST(choice|table)", "the rejection names the missing required node");
        Expect(surface.errors.size() == 1 && surface.errors[0] ==
                                                 "screen store (interface #9 revision 1): required nodes missing or "
                                                 "mistyped: TABLE_LIST(choice|table); "
                                                 "the interface is closed (the engine's back)",
               "a ported screen that cannot be used is a defect");
        Expect(surface.Last() == "back" && surface.toasts.size() == toasts,
               "the defect closes the interface the game's way; its error line is the only report");
        broken.revision = 99;
        router.SetInterface(broken);
        surface.now += 1.0;
        router.Tick();
        Expect(surface.errors.size() == 1 && surface.toasts.size() == toasts,
               "the same node set again: closed again, reported once");
        broken.widgets.pop_back();
        router.SetInterface(broken);
        surface.now += 1.0;
        router.Tick();
        Expect(surface.errors.size() == 2, "and reported again when the node set changes");
        router.SetInterface(Load(models, "store.json"));
        Expect(router.Using() == "store" && router.Rejection().empty(),
               "the ported screen returns when its nodes are back");
    }
};

static void Store(const std::string &models)
{
    StoreRun run{models};
    run.Opening();
    run.RowTaps();
    run.DealOpens();
    run.DealLimits();
    run.Confirm();
    run.Leaving();
    run.ShipSwitch();
    run.Rejection();
}

// The other commerce interfaces share the store's screen; these check what differs.
// The money and free trades on one router: storage, chests and bodies, the row's facts, the other trade scripts.
struct CommerceRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    CommerceRun(std::string path) : models(std::move(path))
    {
        Scripted(router);
    }

    void Models()
    {
        for (const char *name : {"storage.json", "storage-deal.json", "itemsbox.json", "itemsbox-deal.json",
                                 "itemstrade-deal.json", "cannons-deal.json", "contraband-deal.json"})
        {
            const InterfaceModel model = Load(models, name);
            router.SetInterface(model);
            Expect(router.Using() == model.screen && surface.tag == "s-trade" && router.Rejection().empty(),
                   std::string(name) + " is drawn by the trade screen");
            Expect(surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
                   std::string(name) + " has no extras");
        }
        Expect(surface.errors.empty(), "no errors on the commerce models");
    }

    void Storage()
    {
        // The game's own storage figures: the hold on one line, the load of the storage in a sentence.
        router.SetInterface(Load(models, "storage.json"));
        Expect(surface.props["stats"].size() == 2 && surface.props["stats"][0]["value"] == "189 / 700" &&
                   surface.props["stats"][1]["label"] == "Склад" &&
                   surface.props["stats"][1]["value"] == "0 / 50000 ц.",
               "storage: hold and storage load in the header");

        // Storage: a free transfer is confirmed like a money deal (AmountDraft): «Взять / Отдать N», «Отмена».
        router.SetInterface(Load(models, "storage-deal.json"));
        Expect(surface.props["primary"].is_null() && surface.props["list"]["cols"].size() == 2,
               "storage: two amount columns, nothing to confirm at the pivot");
        ExpectEqual(surface.props["list"]["rows"][0]["image"]["texture"], "atlas:GOODS/Sugar",
                    "rows carry the game's picture reference");
        Expect(surface.props["stats"].size() == 2 && surface.props["stats"][1]["value"] == "2328 / 10000",
               "hold and storage figures in the header");
        {
            const std::string edit = "widget:storage\tQTY_EDIT\t-1\tguard@7:row:0|";
            const std::string ok = "widget:storage\tQTY_OK_BUTTON\t-1\tguard@7:row:0|press";
            OfferFlow("storage: take", router, surface, Load(models, "storage-deal.json"), 60, 90, 90,
                      "<trade.take> 30", edit, ok);
            router.SetInterface(Load(models, "storage.json")); // the game closed its window after the transfer
            Expect(surface.commands.empty() && surface.props["primary"]["label"] != "<trade.take> 30",
                   "the moved row is not opened again by itself");
            router.SetInterface(Load(models, "storage-deal.json"));
            OfferFlow("storage: give", router, surface, Load(models, "storage-deal.json"), 60, 10, 10,
                      "<trade.give> 50", edit, ok);
        }
    }

    void Chest()
    {
        // Chest / body: the game's category tabs, take all, remove body.
        router.SetInterface(Load(models, "itemsbox.json"));
        Expect(surface.props["list"]["filters"].size() == 4 && surface.props["list"]["filters"][0]["selected"] == true,
               "itemsbox: the game's tabs");
        Expect(surface.props["list"]["cols"][0]["label"] == "<trade.yours>" &&
                   surface.props["list"]["cols"][1]["label"] == "<trade.theirs>",
               "itemsbox: the game names both amount columns alike, so the string table names them");
        ExpectEqual(surface.language, "russian", "the router passes the model's language on");
        ExpectEqual(surface.props["primary"]["label"], "Взять всё", "itemsbox: take all is the primary");
        Expect(surface.props["actions"].size() == 1 && surface.props["actions"][0]["label"] == "Убрать тело",
               "itemsbox: remove body beside it");
        router.OnEvent({"goods", "filter", "2"});
        ExpectEqual(surface.Last(), "widget:itemsbox\tTABBTN_3\t-1\tpress", "a tab presses the game's tab button");
        {
            // The chest's four tabs carry our short captions; a fifth (a mod's) keeps the game's.
            const Json &chips = surface.props["list"]["filters"];
            Expect(chips.size() == 4 && chips[0]["label"] == "<common.all>" &&
                       chips[1]["label"] == "<trade.tab_mine>" && chips[2]["label"] == "<trade.tab_here>" &&
                       chips[3]["label"] == "<trade.tab_weapons>",
                   "itemsbox: the game's tabs as chips with our short captions: " + chips.dump());
            InterfaceModel modded = Load(models, "itemsbox.json");
            Widget button, caption;
            for (const Widget &widget : modded.widgets)
            {
                if (widget.node == "TABBTN_4")
                    button = widget;
                if (widget.node == "TABSTR_4")
                    caption = widget;
            }
            button.node = "TABBTN_5";
            caption.node = "TABSTR_5";
            caption.label = "Карты";
            modded.widgets.push_back(button);
            modded.widgets.push_back(caption);
            FakeSurface mod_surface;
            Router mod(mod_surface);
            Scripted(mod);
            mod.SetInterface(modded);
            Expect(mod_surface.props["list"]["filters"].size() == 5 &&
                       mod_surface.props["list"]["filters"][4]["label"] == "Карты",
                   "itemsbox: a tab we do not know is appended with the game's caption");
            // The hand at a row's end: the game opens the row's amount window, then all of the stack is taken.
            const Json &loot = mod_surface.props["list"]["rows"];
            size_t with = loot.size();
            for (size_t i = 0; i < loot.size(); ++i)
                if (loot[i]["quick"] == true && with == loot.size())
                    with = i;
            Expect(mod_surface.props["list"]["quick"] == "hand" && with < loot.size(),
                   "itemsbox: rows with something to take offer the quick take");
            mod_surface.commands.clear();
            mod.OnEvent({"goods", "quick", loot[with]["id"].get<std::string>()});
            Expect(mod_surface.commands.size() == 1 &&
                       mod_surface.commands[0].find("\tTABLE_LIST\t") != std::string::npos &&
                       mod_surface.commands[0].find("activate@") != std::string::npos,
                   "quick take: first the row's amount window: " +
                       (mod_surface.commands.empty() ? std::string() : mod_surface.commands[0]));
        }
    }

    void ChestTakes()
    {
        // Before the routers below: a script screen's state is the program's, one window's at a time.
        router.OnEvent({"primary", "click", ""});
        ExpectEqual(surface.Last(), "widget:itemsbox\tGETALL_BUTTON\t-1\tpress", "take all");
        router.OnEvent({"action-0", "click", ""});
        ExpectEqual(surface.Last(), "widget:itemsbox\tDELBODY_BUTTON\t-1\tpress", "remove body");
        {
            // The amount window opened for the quick take: the whole stack is entered and confirmed, the window is not
            // reopened.
            FakeSurface quick_surface;
            Router quick(quick_surface);
            Scripted(quick);
            quick.SetInterface(Load(models, "itemsbox.json"));
            InterfaceModel open = Load(models, "itemsbox-deal.json");
            std::string row_index;
            int there = 0;
            for (const Widget &widget : open.widgets)
                if (widget.node == "TABLE_LIST" && widget.value == "1")
                    row_index = std::to_string(widget.index), there = std::atoi(widget.CellText(4).c_str());
            quick.OnEvent({"goods", "quick", row_index});
            quick_surface.commands.clear();
            open.revision += 10;
            AnswerAll(quick, quick_surface, true);
            quick.SetInterface(open);
            quick.Tick();
            Expect(quick_surface.commands.size() == 3 &&
                       quick_surface.commands[1].find("QTY_EDIT") != std::string::npos &&
                       quick_surface.commands[1].rfind("|" + std::to_string(there)) ==
                           quick_surface.commands[1].size() - std::to_string(there).size() - 1 &&
                       quick_surface.commands[2].find("QTY_OK_BUTTON") != std::string::npos,
                   "quick take: all of the other side, confirmed: " + Json(quick_surface.commands).dump());
        }
        {
            // A tapped row: the game selects it in one loop and opens its window in the next; the frame between is
            // not shown and the build awaits the window, so the selection and its deal appear together.
            FakeSurface tap_surface;
            Router tap(tap_surface);
            Scripted(tap);
            const auto chosen = [&](const char *model, int row, uint64_t revision) {
                InterfaceModel out = Load(models, model);
                out.revision = revision;
                for (Widget &widget : out.widgets)
                    if (widget.node == "TABLE_LIST")
                        widget.value = widget.index == row ? "1" : "0";
                return out;
            };
            tap.SetInterface(chosen("itemsbox-deal.json", 0, 1));
            const Json before = tap_surface.props;
            tap.OnEvent({"goods", "row", "1"});
            AnswerAll(tap, tap_surface, true);
            tap.SetInterface(chosen("itemsbox.json", 1, 2));
            Expect(tap_surface.props == before && tap.Answering(),
                   "chest: the selection without its window is held back and its window awaited");
            Expect(tap.OnEvent({"goods", "info", ""}) && tap_surface.errors.empty(),
                   "chest: what the held view shows of a deal takes no input, and says nothing");
            tap.SetInterface(chosen("itemsbox-deal.json", 1, 3));
            Expect(tap_surface.props["list"]["rows"][1]["selected"] == true && !tap_surface.props["deal"].is_null() &&
                       !tap.Answering(),
                   "chest: the window arrives: the row selected and its deal in one frame");
            // A tab the game does not take now is not pressed.
            InterfaceModel locked = chosen("itemsbox.json", 0, 4);
            for (Widget &widget : locked.widgets)
                if (widget.node == "TABBTN_2")
                    widget.actions.clear();
            tap.SetInterface(locked);
            tap_surface.commands.clear();
            tap.OnEvent({"goods", "filter", "1"});
            Expect(tap_surface.props["list"]["filters"][1]["disabled"] == true && tap_surface.commands.empty(),
                   "chest: a tab the game does not offer is shown disabled and not pressed: " +
                       Json(tap_surface.commands).dump());
        }
        {
            // A chest (the game hides its take-all there): «Взять всё» asks the engine to run the game's
            // own take-all in one go (ANDROID_TAKE_ALL); what is left after it was too heavy to carry.
            const auto chest = [&](int revision, std::map<std::string, std::string> there, bool offered) {
                InterfaceModel model = Load(models, "itemsbox.json");
                for (Widget &widget : model.widgets)
                    if (widget.node == "GETALL_BUTTON")
                        widget.node = "ANDROID_TAKE_ALL"; // as the engine publishes it for a chest
                std::erase_if(model.widgets, [offered](const Widget &widget) {
                    return widget.node == "DELBODY_BUTTON" || (!offered && widget.node == "ANDROID_TAKE_ALL");
                });
                model.revision = static_cast<uint64_t>(revision);
                for (Widget &widget : model.widgets)
                    if (widget.node == "TABLE_LIST" && there.count(widget.row_id))
                        for (Cell &cell : widget.cells)
                            if (cell.column == 4)
                                cell.text = there[widget.row_id];
                return model;
            };
            const std::map<std::string, std::string> empty{
                {"row:0", "0"}, {"row:1", "0"}, {"row:2", "0"}, {"row:3", "0"}};
            FakeSurface chest_surface;
            Router box(chest_surface);
            Scripted(box);
            box.SetInterface(chest(1, {}, true));
            ExpectEqual(chest_surface.props["primary"]["label"], "<trade.take_all>",
                        "a chest: our take all is the primary");
            chest_surface.commands.clear();
            box.OnEvent({"primary", "click", ""});
            box.Tick();
            Expect(chest_surface.commands.size() == 1 &&
                       chest_surface.commands[0] == "widget:itemsbox\tANDROID_TAKE_ALL\t-1\tpress",
                   "take all: one request for the game's take-all, no stack by stack: " +
                       Json(chest_surface.commands).dump());
            AnswerAll(box, chest_surface, true);
            box.SetInterface(chest(2, empty, true));
            Expect(chest_surface.toasts.empty() && chest_surface.props["primary"].is_null(),
                   "all of it taken: no toast, an empty chest offers no take all");
            box.SetInterface(chest(3, {}, true));
            box.OnEvent({"primary", "click", ""});
            box.Tick();
            AnswerAll(box, chest_surface, true);
            box.SetInterface(chest(4, {{"row:0", "0"}, {"row:1", "0"}, {"row:2", "0"}, {"row:3", "1"}}, true));
            Expect(chest_surface.toasts.size() == 1 && chest_surface.toasts[0] == "<trade.too_heavy>",
                   "a stack left after the game's take-all: the weight limit, said once: " +
                       Json(chest_surface.toasts).dump());
            box.SetInterface(chest(5, {{"row:0", "0"}, {"row:1", "0"}, {"row:2", "0"}, {"row:3", "1"}}, true));
            Expect(chest_surface.toasts.size() == 1, "… only for the answer to the press");
            box.SetInterface(chest(6, {}, false));
            Expect(chest_surface.props["primary"].is_null(), "without the engine's take-all no button");
        }
        router.SetInterface(Load(models, "itemsbox-deal.json"));
        Expect(surface.props["deal"]["steps"].size() == 2 && surface.props["deal"]["max"] == 26,
               "single items: steps 1 and 10, up to both sides");
        {
            // The chest's table has each side's total weight: the dock's weight is one piece's (2.3 for 23 bullets).
            Json facts;
            for (const Json &row : surface.props["list"]["rows"])
                if (row["selected"] == true)
                    facts = row["facts"];
            Expect(facts.size() == 1 && facts[0]["icon"] == "weight" && facts[0]["v"] == "0,1",
                   "itemsbox: the weight of one piece by its icon: " + facts.dump());
        }
        {
            // Gold has no weight: itemsbox.c writes "-" in both weight columns of its row, and the game shows it so.
            InterfaceModel gold = Load(models, "itemsbox-deal.json");
            for (Widget &widget : gold.widgets)
                if (widget.node == "TABLE_LIST" && widget.value == "1")
                    for (Cell &cell : widget.cells)
                        if (cell.column == 1 || cell.column == 3)
                            cell.text = "-";
            FakeSurface gold_surface;
            Router box(gold_surface);
            Scripted(box);
            box.SetInterface(gold);
            Json facts = Json::array();
            for (const Json &row : gold_surface.props["list"]["rows"])
                if (row["selected"] == true)
                    facts = row["facts"];
            Expect(facts.empty() && gold_surface.errors.empty(),
                   "itemsbox: a weightless row has no weight and is no error: " + facts.dump() + " " +
                       Json(gold_surface.errors).dump());
        }
        // A chest, a body, an officer's pockets: picking the amount must not move the items at once (regression).
        {
            OfferFlow("itemsbox", router, surface, Load(models, "itemsbox-deal.json"), 23, 25, 25, "<trade.take> 2",
                      "widget:itemsbox\tQTY_EDIT\t-1\tguard@7:row:0|",
                      "widget:itemsbox\tQTY_OK_BUTTON\t-1\tguard@7:row:0|press");
        }
    }

    void RowFacts()
    {
        // The row's own facts: the trade type by the name's colour; the open row's prices, weight and pack as the
        // dock's facts.
        router.SetInterface(Load(models, "store.json"));
        router.SetInterface(Load(models, "store-deal.json"));
        ExpectEqual(surface.props["list"]["rows"][5]["meta"], "<trade.export>",
                    "the trade type the colour tells is named from the string table");
        ExpectEqual(surface.props["list"]["rows"][5]["sub"], "<trade.export>",
                    "the dock's second line is the trade type; the pack is a fact");
        {
            // «в трюме» / «в магазине», icons beside the prices, the weight with its icon and no word,
            // and the game's «пачка 20 шт.» (it sells by the piece) said as what the figures are for.
            const Json &open = surface.props["list"]["rows"][0];
            const Json facts = open["facts"];
            Expect(facts.size() == 4 && facts[0]["icon"] == "price-buy" && facts[0]["v"] == "5" &&
                       facts[1]["icon"] == "price-sell" && facts[1]["v"] == "3" && facts[2]["icon"] == "weight" &&
                       facts[2]["v"] == "1 <transfer.cwt>" && !facts[2].contains("k") &&
                       facts[3]["v"] == "<trade.per_units>",
                   "store: the open row's facts: both prices, the weight by its icon, per pack: " + facts.dump());
            Expect(open["text"] == "Чугунные ядра для корабельных орудий." && open["sub"] == "",
                   "store: the game's «Вес 1 ц, пачка 20 шт.» line leaves the text: " + open.dump());
            const Json &cols = surface.props["list"]["cols"];
            Expect(cols[0]["label"] == "<trade.in_hold>" && cols[0]["icon"] == "capacity" &&
                       cols[1]["label"] == "<trade.in_store>" && cols[1]["icon"] == "store" &&
                       cols[2]["icon"] == "price-buy" && cols[3]["icon"] == "price-sell",
                   "store: «В трюме», «В магазине» and the price columns with their icons: " + cols.dump());
            Expect(surface.props["list"]["filters"][1]["label"] == "<trade.in_hold>", "the hold chip is «В трюме»");
            Expect(surface.props["stats"][0]["icon"] == "capacity" && !surface.props["stats"][0].contains("label"),
                   "the hold figure is its icon: " + surface.props["stats"].dump());
            // «Выгодно»: export (cheap here), import and high demand (dear here) — the game's own trade types.
            router.OnEvent({"goods", "filter", "2"});
            std::vector<std::string> kinds;
            for (const Json &row : surface.props["list"]["rows"])
                kinds.push_back(row["meta"]);
            Expect(!kinds.empty() &&
                       std::all_of(kinds.begin(), kinds.end(),
                                   [](const std::string &k) {
                                       return k == "<trade.export>" || k == "<trade.import>" || k == "<trade.demand>";
                                   }) &&
                       std::find(kinds.begin(), kinds.end(), "<trade.import>") != kinds.end() &&
                       std::find(kinds.begin(), kinds.end(), "<trade.export>") != kinds.end(),
                   "«Выгодно» lists export, import and high demand: " + Json(kinds).dump());
            router.OnEvent({"goods", "filter", "0"});
        }
        {
            // Buy and sell are columns of their own; a caption's tap orders the rows by it (largest, smallest, the
            // game's order).
            const Json &cols = surface.props["list"]["cols"];
            Expect(cols.size() == 4 && cols[2]["label"] == "<trade.buy>" && cols[3]["label"] == "<trade.sell>" &&
                       cols[2]["icon"] == "price-buy" && cols[3]["icon"] == "price-sell" && cols[2]["sortable"] == true,
                   "store: hold, store, icon-only buy and sell columns");
            const std::string first = surface.props["list"]["rows"][0]["id"];
            router.OnEvent({"goods", "sort", "2"});
            const Json sorted = surface.props["list"]["rows"];
            bool descending = true;
            for (size_t i = 1; i < sorted.size(); ++i)
                descending = descending && std::atoi(sorted[i - 1]["cells"][2]["text"].get<std::string>().c_str()) >=
                                               std::atoi(sorted[i]["cells"][2]["text"].get<std::string>().c_str());
            Expect(descending && surface.props["list"]["cols"][2]["sort"] == "desc",
                   "sort by the buy price, largest first");
            router.OnEvent({"goods", "sort", "2"});
            Expect(surface.props["list"]["cols"][2]["sort"] == "asc", "second tap: smallest first");
            router.OnEvent({"goods", "sort", "2"});
            Expect(surface.props["list"]["cols"][2]["sort"] == "" && surface.props["list"]["rows"][0]["id"] == first,
                   "third tap: the game's order again");
        }
    }

    void OtherTrades()
    {
        // itemstrade.c: single pieces; a doubloon merchant's balance is named. «Лечебное зелье»: 3 ours, 12 theirs; the
        // window holds 5 bought («Деньги 750, вес 2.5»).
        InterfaceModel doubloons = Load(models, "itemstrade-deal.json");
        for (Widget &widget : doubloons.widgets)
            if (widget.node == "OUR_GOLD")
                widget.context["currency"] = "doubloons";
        router.SetInterface(doubloons);
        ExpectEqual(surface.props["subject"]["label"], "Шарль де Мор",
                    "items trade: the character selector is the subject");
        Expect(surface.props["deal"]["value"] == 8 && surface.props["deal"]["max"] == 15 &&
                   surface.props["stats"][1]["label"] == "<trade.doubloons>" &&
                   !surface.props["stats"][1].contains("icon") && !surface.props["stats"][1].contains("preview"),
               "items trade: the window's amount, both stacks reachable, the named balance as the game writes it: " +
                   surface.props.dump());
        OfferFlow("items trade", router, surface, doubloons, 3, 1, 1, "<trade.sell> 2",
                  "widget:itemstrade\tQTY_EDIT\t-1\tguard@7:row:0|",
                  "widget:itemstrade\tQTY_OK_BUTTON\t-1\tguard@7:row:0|press");
        GameAnswers(router, surface, doubloons, 8, "Деньги 750, вес 2.5");
        Expect(surface.props["primary"]["label"] == "<trade.buy> 5" && surface.props["primary"]["sub"] == "750 · 2,5",
               "five pieces: the game's money and weight, a piece's weight without a unit: " +
                   surface.props["primary"].dump());
    }

    void Cannons()
    {
        // cannons.c: pieces of one; the window holds 6 bought («Деньги 1740, вес 36 ц»).
        const InterfaceModel model = Load(models, "cannons-deal.json");
        router.SetInterface(model);
        ExpectEqual(surface.props["list"]["rows"][1]["sub"], "Дист. 450 · Урон x1.5 · Время 11",
                    "cannons: range, damage, reload in the row's second line");
        Expect(surface.props["deal"]["value"] == 6 && surface.props["deal"]["steps"] == Json::array({1, 10}),
               "cannons: the window's amount, steps of 1 and 10: " + surface.props["deal"].dump());
        OfferFlow("cannons", router, surface, model, 0, 4, 3, "<trade.buy> 3",
                  "widget:cannons\tQTY_EDIT\t-1\tguard@7:row:1|",
                  "widget:cannons\tQTY_OK_BUTTON\t-1\tguard@7:row:1|press");
        {
            // cannons.c opens, and switches ships, with a row selected and its amount window closed.
            const auto closed = [this](uint64_t revision, int ship) {
                InterfaceModel model = Load(models, "cannons-deal.json");
                std::erase_if(model.widgets, [](const Widget &widget) { return widget.node.rfind("QTY_", 0) == 0; });
                for (Widget &widget : model.widgets)
                    if (widget.node == "SHIPS_SCROLL")
                        widget.value = widget.index == ship ? "1" : "0";
                model.revision = revision;
                return model;
            };
            FakeSurface open_surface;
            Router open_router(open_surface);
            Scripted(open_router);
            // cannons.c selects its first row in its first frame: the list may come before that selection.
            InterfaceModel unselected = closed(1, 0);
            for (Widget &widget : unselected.widgets)
                if (widget.node == "TABLE_LIST")
                    widget.value = "0";
            open_router.SetInterface(unselected);
            Expect(open_surface.commands.empty(), "cannons: nothing is asked for while no row is selected");
            open_router.SetInterface(closed(2, 0));
            ExpectEqual(open_surface.Last(), "widget:cannons\tTABLE_LIST\t1\tactivate@7:row:1",
                        "cannons: the selected row's deal is asked for on opening");
            AnswerAll(open_router, open_surface, true);
            InterfaceModel opened = Load(models, "cannons-deal.json");
            opened.revision = 3;
            open_router.SetInterface(opened);
            Expect(open_surface.props["list"]["rows"][1]["selected"] == true && !open_surface.props["deal"].is_null() &&
                       open_surface.errors.empty(),
                   "cannons: the selected row comes with its deal");
            open_surface.commands.clear();
            open_router.SetInterface(closed(4, 0));
            Expect(open_surface.commands.empty(), "cannons: a deal closed by the player stays closed");
        }
    }

    void Contraband()
    {
        // contraband.c: a good without a buy price («-») can only be sold: the scale ends at what the hold has. The
        // colours of its names tell what the smugglers buy or sell, not a trade type: no type, no «Выгодно».
        const InterfaceModel model = Load(models, "contraband-deal.json");
        router.SetInterface(model);
        Expect(surface.props["deal"]["max"] == 50 && surface.props["deal"]["min"] == 0 &&
                   surface.props["deal"]["origin"] == 50 && surface.props["list"]["rows"][0]["meta"] == "" &&
                   surface.props["list"]["filters"].size() == 2,
               "contraband: only a sale, the pivot at the right end; no trade types: " + surface.props.dump());
        OfferFlow("contraband", router, surface, model, 50, 20, 20, "<trade.sell> 30",
                  "widget:contraband\tQTY_EDIT\t-1\tguard@7:row:0|",
                  "widget:contraband\tQTY_OK_BUTTON\t-1\tguard@7:row:0|press");
    }
};

static void Commerce(const std::string &models)
{
    CommerceRun run{models};
    run.Models();
    run.Storage();
    run.Chest();
    run.ChestTakes();
    run.RowFacts();
    run.OtherTrades();
    run.Cannons();
    run.Contraband();
}

static void Crew(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "hirecrew.json"));
    Expect(router.Using() == "hirecrew" && surface.tag == "s-crew" && surface.errors.empty(),
           "hirecrew is drawn by the crew screen");
    Json row = surface.props["list"]["rows"][0];
    Expect(row["q"]["value"] == 40 && row["q"]["max"] == 65 && row["cells"][2]["text"] == "23",
           "crew on the ship, the rail over the ship's and the tavern's men, price per man");
    Expect(surface.props["primary"].is_null(), "nothing to confirm yet: no deal button");
    {
        // Both crews as the ship screen draws a crew (hero::CrewQuality): the adapter's numbers, the game's words.
        const Json &ship = surface.props["ship"], &tavern = surface.props["tavern"];
        Expect(ship["groups"].size() == 3 && ship["groups"][0]["label"] == "Матросы" &&
                   ship["groups"][0]["value"] == 71 && ship["groups"][0]["word"] == "опытные" &&
                   ship["groups"][2]["value"] == 18 && ship["morale"] == 62 && ship["moraleWord"] == "хорошая" &&
                   ship["moraleLabel"] == "<ship.morale>",
               "the ship's crew: experience bars and morale with the game's words: " + ship.dump());
        Expect(tavern["groups"].size() == 3 && tavern["groups"][0]["value"] == 22 &&
                   tavern["groups"][0]["word"] == "неопытные" && tavern["groups"][1]["value"] == 30 &&
                   tavern["morale"] == 55,
               "the tavern's men: their own numbers (town_*): " + tavern.dump());
    }
    // hirecrew.json: 40 on the ship, 25 in the tavern, 23 a man; the window started from 40 (the adapter's base).
    // The amount goes to the game (hirecrew.c ChangeQTY_EDIT clamps it and sets it on both crews); the screen shows
    // what the game answered, not a limit or a cost of its own.
    const auto crew_q = [&surface]() { return surface.props["list"]["rows"][0]["q"]; };
    Expect(crew_q()["value"] == 40 && crew_q()["origin"] == 40 && crew_q()["min"] == 0 &&
               !crew_q().contains("moreReason") && surface.errors.empty(),
           "the hire opens on the pivot, with no reason of ours: " + crew_q().dump());
    InterfaceModel model = Load(models, "hirecrew.json");
    surface.commands.clear();
    router.OnEvent({"crew", "commit", "60"});
    router.Tick();
    Expect(surface.commands.size() == 2 && surface.commands[0] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|0" &&
               surface.commands[1] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|20",
           "the amount goes to the game at once, after its reset: " + Json(surface.commands).dump());
    Expect(crew_q()["value"] == 60 && surface.props["primary"].is_null(),
           "until the game answers: the slider where the player put it, no deal of ours");
    // The game's answer: the purse (500 / 23) stops the hire at 21 men, the cost is its own text.
    const auto answer = [&](int crew, int tavern, const char *result) {
        model.revision += 1;
        Find(model, "CREW_QTY", "text")->label = std::to_string(crew);
        Find(model, "CREW_QTY2", "text")->label = std::to_string(tavern);
        Find(model, "QTY_Result", "text")->label = result;
        AnswerAll(router, surface, true);
        router.SetInterface(model);
    };
    answer(61, 4, "Стоимость найма 483");
    Expect(crew_q()["value"] == 61 && crew_q()["origin"] == 40 &&
               surface.props["primary"]["label"] == "<crew.hire> 21" && surface.props["primary"]["sub"] == "483" &&
               surface.props["stats"][0]["value"] == "40" && surface.props["stats"][0]["preview"] == "61",
           "the game's clamp and cost: «Нанять 21» for 483: " + surface.props["primary"].dump() + crew_q().dump());
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(surface.commands.size() == 1 &&
               surface.commands[0] == "widget:hirecrew\tQTY_OK_BUTTON\t-1\tguard@7:ship:0|press",
           "«Нанять 21» is the game's OK, bound to the ship: " + Json(surface.commands).dump());
    surface.commands.clear();
    router.OnEvent({"crew", "commit", "35"});
    surface.now += 0.5;
    router.Tick();
    Expect(surface.commands.size() == 2 && surface.commands[1] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|-5",
           "left of the pivot fires: a negative amount");
    answer(35, 30, "");
    Expect(surface.props["primary"]["label"] == "<crew.fire> 5" && surface.props["primary"]["sub"] == "",
           "the game fired five, at no cost");
    {
        // Without the adapter's facts the screen says so aloud.
        InterfaceModel bare = Load(models, "hirecrew.json");
        bare.widgets.erase(std::remove_if(bare.widgets.begin(), bare.widgets.end(),
                                          [](const Widget &w) { return w.node == "ANDROID_CREWHIRE"; }),
                           bare.widgets.end());
        FakeSurface bare_surface;
        Router bare_router(bare_surface);
        bare_router.SetInterface(bare);
        Expect(bare_surface.errors.size() == 1 && bare_surface.errors[0].find("ANDROID_CREWHIRE") != std::string::npos,
               "missing adapter facts are an error, not a quiet guess");
    }
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), "widget:hirecrew\tSHIPS_SCROLL\t1\tselect@7:ship:1", "the next ship");

    InterfaceModel broken = Load(models, "hirecrew.json");
    broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(),
                                        [](const Widget &w) { return w.node == "QTY_EDIT"; }),
                         broken.widgets.end());
    broken.opening = 9;
    router.SetInterface(broken);
    surface.now += 1.0;
    router.Tick();
    Expect(!router.Active() && router.Rejection() == "QTY_EDIT(edit)" && surface.errors.size() == 1,
           "without its edit box the crew screen is rejected loudly");
}

static void Repair(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    const InterfaceModel model = Load(models, "repair.json");
    router.SetInterface(model);
    Expect(router.Using() == "repair" && surface.tag == "s-repair" && surface.errors.empty(),
           "repair is drawn by the repair screen");
    const Json &parts = surface.props["parts"];
    Expect(
        parts.size() == 3 && parts[0]["q"]["value"] == 75 && parts[0]["q"]["min"] == 0 && parts[0]["q"]["max"] == 100 &&
            parts[0]["q"]["floor"] == 62 && !parts[0]["q"].contains("ceiling"),
        "hull: 75 % on the 0…100 % scale from its state, 62 % (75 shown less the 13 set); the game stops the arrows");
    ExpectEqual(parts[0]["q"]["allLabel"], "<common.all>", "fill to the end is «Все», as everywhere");
    Expect(parts[2]["short"] == true && parts[2]["need"] == "<repair.no_sailcloth>", "sails without sailcloth say so");
    Expect(surface.props["stock"][0]["value"] == "40" && surface.props["stock"][1]["value"] == "0",
           "planks and sailcloth in stock: the adapter's");
    ExpectEqual(surface.props["primary"]["sub"], "6 <save.hours> · <repair.planks> 16",
                "repair button: time and the planks the game says the order takes");
    std::string prefix = "widget:repair\t";

    surface.commands.clear();
    router.OnEvent({"hull", "commit", "78"});
    Expect(surface.commands.size() == 3 && surface.commands[0] == prefix + "REPAIR_RIGHT_H\t-1\tpress",
           "+3 % = three presses of the right arrow");
    Expect(surface.props["parts"][0]["q"]["value"] == 78, "the amount set is shown until the game answers");
    surface.commands.clear();
    router.OnEvent({"hull", "change", "70"});
    Expect(surface.commands.empty(), "a drag waits until it settles");
    surface.now += 0.5;
    router.Tick();
    Expect(surface.commands.size() == 8 && surface.commands[0] == prefix + "REPAIR_LEFT_H\t-1\tpress",
           "then 70 before the game answered: counted from the 78 the presses lead to, eight left");
    surface.commands.clear();
    router.OnEvent({"masts", "commit", "3"});
    Expect(surface.commands.size() == 1 && surface.commands[0] == prefix + "REPAIR_RIGHT_M\t-1\tpress",
           "masts move all at once: one press");

    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_OK\t-1\tpress", "repair");
    router.OnEvent({"all-ships", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_ALL_SQUADRON\t-1\tpress", "all ships");
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), prefix + "SHIPS_SCROLL\t1\tselect@13:ship:1", "the next ship");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_CANCEL\t-1\tpress", "back cancels");
}

// The captain choice of a prize without a captain (transfer-captain.json, recorded on a device after a boarding:
// transfer_main.c OFFICERS_WINDOW over a lugger whose captain fell, three passengers). Every candidate carries the
// game's portrait (PASSENGERSLIST pic<N>.img1 of its FACE128_<FaceId> group, CXI_SCROLLIMAGE::MobileRowImage); the
// selected one's ship skills (TABLE_SMALLSKILL) and facts (TABLE_SMALLOTHER) as SetOfficersSkills shows them; a tap
// selects, «Назначить» is the game's ACCEPT_ADD_OFFICER (AcceptAddOfficer), ✕ / the scrim / back its «Отмена».
static void TransferCaptain(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    const std::string prefix = "widget:transfer_main\t";
    router.SetInterface(Load(models, "transfer-captain.json"));
    const Json &sheet = surface.props["sheet"];
    Expect(router.Using() == "transfer_main" && surface.errors.empty() && sheet.is_object() &&
               sheet["kind"] == "captain" && sheet["closable"] == true &&
               sheet["title"] == "Назначение капитана корабля",
           "the captain window is the shipyard's captain sheet, every widget known: " + sheet.dump().substr(0, 300));
    const Json &candidates = sheet["candidates"];
    Expect(candidates.size() == 3 && candidates[0]["id"] == "candidate-0" &&
               candidates[0]["title"] == "Флоран Габриель" && candidates[0]["selected"] == true &&
               candidates[1]["selected"] == false && candidates[2]["disabled"] == false,
           "the three passengers, the first selected: " + candidates.dump().substr(0, 300));
    bool portraits = candidates.size() == 3;
    for (const Json &candidate : candidates)
        portraits = portraits && candidate["image"].is_object() && candidate["image"]["kind"] == "item" &&
                    candidate["image"]["texture"].get<std::string>().rfind("atlas:FACE128_", 0) == 0;
    Expect(portraits && candidates[1]["image"]["texture"] == "atlas:FACE128_18/face",
           "each row shows the officer's own portrait: " + candidates[1].dump());
    Expect(sheet["name"] == "Флоран Габриель" && !sheet.contains("subtitle"),
           "the selected one's name above his facts, not as the subtitle");
    Expect(sheet["skills"].size() == 7 &&
               sheet["skills"][0] == Json({{"icon", "wheel"}, {"k", "<skill.sailing>"}, {"v", "14"}, {"tone", ""}}) &&
               sheet["skills"][6]["k"] == "<skill.commerce>" && sheet["skills"][6]["v"] == "13",
           "his ship skills from the game's mini table: " + sheet["skills"].dump());
    Expect(sheet["facts"].size() == 5 && sheet["facts"][0] == Json({{"k", "Ранг"}, {"v", "7"}}) &&
               sheet["facts"][1]["v"] == "93 / 93",
           "his facts as the game's table lists them: " + sheet["facts"].dump());
    Expect(sheet["actions"].size() == 1 && sheet["actions"][0]["id"] == "sheet-ok" &&
               sheet["actions"][0]["label"] == "Назначить" && sheet["actions"][0]["disabled"] == false,
           "«Назначить», the game's own button: " + sheet["actions"].dump());

    router.OnEvent({"candidate-2", "click", ""});
    ExpectEqual(surface.Last(), prefix + "PASSENGERSLIST\t2\tselect@4:1:2",
                "a tap selects that passenger in the game's list (guarded by his row of this revision)");
    surface.commands.clear();
    router.OnEvent({"candidate-0", "click", ""});
    Expect(surface.commands.empty(), "a tap on the selected one sends nothing");
    router.OnEvent({"sheet-ok", "click", ""});
    ExpectEqual(surface.Last(), prefix + "ACCEPT_ADD_OFFICER\t-1\tpress",
                "«Назначить» appoints him (AcceptAddOfficer)");
    router.OnEvent({"sheet-close", "click", ""});
    ExpectEqual(surface.Last(), prefix + "CANCEL_ADD_OFFICER\t-1\tpress", "✕ is the game's «Отмена» (ExitOfficerMenu)");
    router.OnEvent({"sheet", "dismiss", ""});
    ExpectEqual(surface.Last(), prefix + "CANCEL_ADD_OFFICER\t-1\tpress", "so is a tap beside it");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "CANCEL_ADD_OFFICER\t-1\tpress", "and back");
}

// The transfer screen on one router: the loot's cargo, a cargo row, the prisoners, the crew and the ships.
struct TransferRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    const std::string prefix = "widget:transfer_main\t";

    void Loot()
    {
        router.SetInterface(Load(models, "transfer-loot.json"));
        Expect(router.Using() == "transfer_main" && surface.tag == "s-transfer" && surface.errors.empty(),
               "loot is drawn by the transfer screen");
        Expect(surface.props["tabs"].size() == 3 && surface.props["tabs"][0]["selected"] == true &&
                   surface.props["tabs"][2]["id"] == "ships" && surface.props["ships"].is_null(),
               "cargo, crew and ships tabs, cargo open");
        {
            // The other ship's card of the dock, and the «Корабли» tab: both ships' figures side by side.
            const Json &prize = surface.props["prize"];
            Expect(prize["title"] == "Корабль 2" && prize["facts"].size() == 3 && prize["facts"][0]["k"] == "Корпус",
                   "the other ship's card: " + prize.dump());
            surface.commands.clear();
            router.OnEvent({"header", "tab", "2"});
            const Json &ships = surface.props["ships"];
            Expect(surface.commands.empty() && surface.props["tabs"][2]["selected"] == true &&
                       ships["ours"]["title"] == "Корабль 1" && ships["rows"].size() >= 8 &&
                       ships["rows"][0]["k"] == "Корпус" && ships["rows"][0]["icon"] == "hull",
                   "the ships tab is ours: nothing is sent, the figures are listed row by row: " +
                       ships["rows"][0].dump());
            bool toned = false;
            for (const Json &row : ships["rows"])
                toned = toned || row.contains("tone");
            Expect(toned, "a figure where the other ship is plainly better or worse is toned");
            router.OnEvent({"header", "tab", "0"});
            Expect(surface.props["ships"].is_null() && surface.props["tabs"][0]["selected"] == true,
                   "back to the cargo");
        }
        Expect(surface.props["stats"][1]["value"] == "<transfer.no_captain>", "the captured ship has no captain");
        {
            // A captain commands the other ship (a companion, or an officer just appointed to the prize): his name
            // heads its card, as the stock screen's portrait of him does (xinterface ANDROID_TRANSFER captain).
            FakeSurface named_surface;
            Router named_router(named_surface);
            InterfaceModel companion = Load(models, "transfer-companion.json");
            for (Widget &widget : companion.widgets)
                if (widget.node == "ANDROID_TRANSFER")
                    widget.context["captain"] = "Ги Дабри";
            named_router.SetInterface(companion);
            const Json &prize = named_surface.props["prize"];
            Expect(prize["facts"].size() == 4 && prize["facts"][0]["k"] == "<transfer.captain>" &&
                       prize["facts"][0]["v"] == "Ги Дабри",
                   "the other ship's card names its captain: " + prize.dump());
        }
        Expect(surface.props["list"]["cols"][0]["label"] == "Корабль 1" && surface.props["list"]["rows"].size() > 5,
               "goods columns named by the ships");
        ExpectEqual(surface.props["primary"]["label"], "<transfer.take_all>", "take all the cargo");
        router.OnEvent({"primary", "click", ""});
        ExpectEqual(surface.Last(), prefix + "TAKE_GOODS\t-1\tpress", "take all = the game's take goods");
        router.OnEvent({"header", "tab", "1"});
        ExpectEqual(surface.Last(), prefix + "CREW_BUTTON\t-1\tpress", "crew tab opens the crew window");
        router.OnEvent({"action-CAPTAN_BUTTON", "click", ""});
        ExpectEqual(surface.Last(), prefix + "CAPTAN_BUTTON\t-1\tpress", "captain button");
        router.OnEvent({"header-back", "click", ""});
        ExpectEqual(surface.Last(), prefix + "EXIT_BTN\t-1\tpress",
                    "back leaves through the game's exit (it asks about the ship)");
    }

    void CargoRow()
    {
        router.OnEvent({"goods", "row", "0"});
        ExpectEqual(surface.Last(), prefix + "TABLE_LIST\t0\tselect",
                    "a tap selects the cargo row (the game's rows carry no identity here)");
        router.SetInterface(Load(models, "transfer-loot-row.json"));
        const Json &shot = surface.props["list"]["rows"][0];
        Expect(shot["q"]["value"] == 100 && shot["q"]["max"] == 595 && shot["q"]["steps"][0] == 20,
               "the selected row: ours of both, in packs of 20");
        {
            const auto arrows = [this](const std::string &command) {
                return command == prefix + "B_LEFT\t-1\tpress" || command == prefix + "B_RIGHT\t-1\tpress";
            };
            const auto presses = [this](const std::string &node, size_t count) {
                return [this, node, count](const std::vector<std::string> &commands) {
                    return commands.size() == count &&
                           std::all_of(commands.begin(), commands.end(),
                                       [&](const std::string &c) { return c == prefix + node + "\t-1\tpress"; });
                };
            };
            // The game moves whole packs of 20: 145 is 140, two presses.
            DraftFlow(
                "transfer cargo: take", router, surface, "goods", 145, "<trade.take> 40", arrows, presses("B_LEFT", 2),
                [] {}, 100);
            Expect(surface.props["list"]["rows"][0]["q"]["value"] == 140,
                   "the control shows what was sent until the game answers");
            InterfaceModel answered = Load(models, "transfer-loot-row.json");
            answered.revision += 5;
            router.SetInterface(answered); // a revision with the same figures: not the answer yet
            Expect(surface.props["list"]["rows"][0]["q"]["value"] == 140 && surface.props["primary"]["busy"] == true,
                   "a revision that does not change the figures is not the answer: the sent amount waits");
            AnswerAll(router, surface, true); // the presses were applied ...
            Expect(surface.props["list"]["rows"][0]["q"]["value"] == 140,
                   "applied answers alone are not the result: the model after them is");
            answered.revision += 1;
            router.SetInterface(answered); // ... and the model after them still has the old figures: the game said no
            Expect(surface.props["list"]["rows"][0]["q"]["value"] == 100 &&
                       surface.props["primary"]["label"] == "<transfer.take_all>",
                   "a refused move opens the amount again at the game's figure; the bar is the place's again");
            DraftFlow(
                "transfer cargo: give", router, surface, "goods", 0, "<trade.give> 100", arrows, presses("B_RIGHT", 5),
                [] {}, 100);
            AnswerAll(router, surface, false,
                      "the button cannot be pressed now"); // refused: open again at once, no model needed
            Expect(surface.props["list"]["rows"][0]["q"]["value"] == 100 &&
                       surface.props["primary"]["label"] == "<transfer.take_all>",
                   "a move the engine refused opens the amount again at once");
            answered.revision += 5;
            router.SetInterface(answered);
            surface.commands.clear();
            router.OnEvent({"goods", "change", "300"});
            router.OnEvent({"header-back", "click", ""});
            Expect(surface.commands.empty() && surface.props["primary"]["label"] == "<transfer.take_all>",
                   "back drops the amount set first, moving nothing");
        }
        {
            // Regression: a good the other ship no longer has could not be given back. The game shows
            // (and exports) only the arrows whose way is possible: without B_LEFT the whole control was disabled.
            InterfaceModel back = Load(models, "transfer-loot-row.json");
            back.widgets.erase(std::remove_if(back.widgets.begin(), back.widgets.end(),
                                              [](const Widget &w) { return w.node == "B_LEFT"; }),
                               back.widgets.end());
            FakeSurface back_surface;
            Router back_router(back_surface);
            back_router.SetInterface(back);
            const Json q = back_surface.props["list"]["rows"][0]["q"];
            Expect(q["disabled"] == false && q["min"] == 0 && q["max"] == 100 && q["origin"] == 100,
                   "without the take arrow the row still gives: ours down to none, nothing to take: " + q.dump());
            back_router.OnEvent({"goods", "commit", "45"});
            Expect(back_surface.props["primary"]["label"] == "<trade.give> 60" &&
                       back_surface.props["primary"]["sub"] == "6 <transfer.cwt>",
                   "the amount steps by packs of 20 (45 is 40 left, 60 given), the hold's change below: " +
                       back_surface.props["primary"].dump());
            back_surface.commands.clear();
            back_router.OnEvent({"primary", "click", ""});
            Expect(back_surface.commands == std::vector<std::string>(3, prefix + "B_RIGHT\t-1\tpress"),
                   "three packs given: " + Json(back_surface.commands).dump());
            {
                // Back from the crew window the game hides both arrows (its focus is off the table): the row is
                // selected again once, and not on every build while they stay hidden.
                InterfaceModel hidden = Load(models, "transfer-loot-row.json");
                hidden.widgets.erase(
                    std::remove_if(hidden.widgets.begin(), hidden.widgets.end(),
                                   [](const Widget &w) { return w.node == "B_LEFT" || w.node == "B_RIGHT"; }),
                    hidden.widgets.end());
                FakeSurface hidden_surface;
                Router hidden_router(hidden_surface);
                hidden_router.SetInterface(hidden);
                hidden.revision += 1;
                hidden_router.SetInterface(hidden);
                const auto selects = std::count(hidden_surface.commands.begin(), hidden_surface.commands.end(),
                                                prefix + "TABLE_LIST\t0\tselect");
                Expect(selects == 1, "arrows hidden: the selected row is selected again once: " +
                                         Json(hidden_surface.commands).dump());
            }
            Expect(back_surface.props["list"]["rows"][0]["facts"][0]["icon"] == "weight" &&
                       back_surface.props["list"]["rows"][0]["facts"][1]["v"] == "<trade.per_units>",
                   "the dock says what a pack weighs and how many pieces it is");
        }
        {
            // The hand at a row's end: the row is selected, then all of the other hold is asked for by the game's
            // arrow.
            FakeSurface quick_surface;
            Router quick(quick_surface);
            quick.SetInterface(Load(models, "transfer-loot.json"));
            Expect(quick_surface.props["list"]["quick"] == "hand" &&
                       quick_surface.props["list"]["rows"][0]["quick"] == true,
                   "cargo: rows the other hold has offer the quick take");
            quick.OnEvent({"goods", "quick", "0"});
            ExpectEqual(quick_surface.Last(), prefix + "TABLE_LIST\t0\tselect",
                        "quick take: the row is selected first");
            quick_surface.commands.clear();
            quick.SetInterface(Load(models, "transfer-loot-row.json"));
            quick.Tick();
            Expect(quick_surface.commands.size() == 25 && quick_surface.commands[0] == prefix + "B_LEFT\t-1\tpress" &&
                       quick_surface.props["dealing"] == true,
                   "quick take: 495 of theirs = 25 packs of 20 taken: " +
                       std::to_string(quick_surface.commands.size()));
        }
    }

    void Prisoners()
    {
        router.SetInterface(Load(models, "transfer-hire.json"));
        const Json &hire = surface.props["modal"];
        Expect(hire["step"] == "1/2" && hire["q"]["value"] == 8 && hire["q"]["max"] == 8,
               "defectors: all 8 willing are taken by default");
        ExpectEqual(hire["actions"][1]["label"], "<transfer.take> 8", "take 8");
        surface.commands.clear();
        router.OnEvent({"hire", "commit", "5"});
        Expect(surface.commands.size() == 3 && surface.commands[0] == prefix + "HIRE_QTY_REMOVE_BUTTON\t-1\tpress",
               "8 → 5 = three presses down");
        router.OnEvent({"hire-ok", "click", ""});
        ExpectEqual(surface.Last(), prefix + "HIRE_QTY_OK_BUTTON\t-1\tpress", "accept the defectors");
        router.OnEvent({"header-back", "click", ""});
        ExpectEqual(surface.Last(), prefix + "HIRE_QTY_CANCEL_BUTTON\t-1\tpress", "back refuses them");

        router.SetInterface(Load(models, "transfer-crew-capture.json"));
        const Json &fate = surface.props["modal"];
        Expect(fate["step"] == "2/2" && fate["actions"].size() == 3 && fate["actions"][0]["hold"] == true &&
                   fate["actions"][0]["variant"] == "danger",
               "crew fate after the defectors: overboard is held, never asked");
        router.OnEvent({"fate-kill", "click", ""});
        ExpectEqual(surface.Last(), prefix + "CAPTURE_CREW_KILL\t-1\tpress", "overboard once the hold completes");

        {
            // The surrendered captain: his portrait beside the question, overboard held, prison the plain answer.
            InterfaceModel capture = Load(models, "transfer-loot.json");
            capture.widgets.push_back(Make("CAPTURE_CAPTION", "text", "Решение по пленным"));
            capture.widgets.push_back(Make("CAPTURE_TEXT", "text", "Выберите участь сдавшегося капитана."));
            capture.widgets.push_back(Make("CAPTURE_CAPTAIN_KILL", "button", "Выкинуть за борт", {"press"}));
            capture.widgets.push_back(Make("CAPTURE_CAPTAIN_PRISON", "button", "Взять пленником", {"press"}));
            capture.widgets.push_back(Make("CAPTURE_CANCEL", "button", "Отмена", {"press"}));
            Widget face = Make("CAPTUR_PICTURE", "image", "");
            ImageRef portrait;
            portrait.kind = "portrait";
            portrait.image = "100";
            face.images.push_back(portrait);
            capture.widgets.push_back(face);
            FakeSurface capture_surface;
            Router capture_router(capture_surface);
            capture_router.SetInterface(capture);
            const Json &asked = capture_surface.props["modal"];
            Expect(capture_surface.errors.empty() && capture_surface.props["extras"]["rows"].empty() &&
                       asked["image"] ==
                           Json({{"kind", "portrait"}, {"texture", "INTERFACES/PORTRAITS/128/face_100"}}) &&
                       asked["actions"][0]["hold"] == true && asked["actions"][0]["variant"] == "danger" &&
                       asked["actions"][1]["variant"] == "primary",
                   "the captain's fate: his portrait, overboard held: " + asked.dump());
            capture_surface.commands.clear();
            capture_router.OnEvent({"modal", "dismiss", ""});
            Expect(capture_surface.errors.empty() && capture_surface.Last() == prefix + "CAPTURE_CANCEL\t-1\tpress",
                   "a tap beside the question is its «Отмена»: the captain stays as he is");
            capture_router.OnEvent({"capture-prison", "click", ""});
            ExpectEqual(capture_surface.Last(), prefix + "CAPTURE_CAPTAIN_PRISON\t-1\tpress",
                        "prison is the game's own button");
        }
        router.SetInterface(Load(models, "transfer-fate.json"));
        const Json &ship = surface.props["modal"];
        Expect(ship["actions"].size() == 2 && ship["actions"][0]["label"] == "<transfer.sink>" &&
                   ship["actions"][0]["hold"] == true,
               "a ship without a captain: sink (held) or keep");
        router.OnEvent({"fate-ship", "click", ""});
        ExpectEqual(surface.Last(), prefix + "REMOVE_ACCEPT_OFFICER\t-1\tpress", "sink = the game's yes");
        router.OnEvent({"fate-keep", "click", ""});
        ExpectEqual(surface.Last(), prefix + "REMOVE_CANCEL_OFFICER\t-1\tpress", "keep = back to appoint a captain");
    }

    void Crew()
    {
        router.SetInterface(Load(models, "transfer-crew.json"));
        {
            // The slider marks the least crew each ship sails with (MinCrew): ours where our crew is
            // 9, theirs where what we leave them is their 21 (96 + 58 - 21 = 133).
            const Json &marks = surface.props["crew"]["rows"][0]["q"]["marks"];
            Expect(marks.size() == 2 && marks[0]["at"] == 9 && marks[1]["at"] == 133 &&
                       marks[0]["label"] == "Корабль 1 <transfer.min_mark>" &&
                       marks[1]["label"] == "Корабль 2 <transfer.min_mark>",
                   "crew: the two MinCrew marks on our crew's scale: " + marks.dump());
            const Json &cells = surface.props["crew"]["rows"][0]["cells"];
            Expect(cells[0]["text"] == "96" && cells[0]["sub"] == "<transfer.min_mark>" && cells[1]["text"] == "58" &&
                       cells[1]["sub"] == "<transfer.min_mark>",
                   "crew: each ship's minimum under its crew: " + cells.dump());
            Expect(surface.errors.empty(), "crew marks read from ANDROID_TRANSFER");
        }
        Expect(surface.props["crew"]["rows"][0]["q"]["value"] == 96 &&
                   surface.props["crew"]["rows"][0]["q"]["max"] == 154 &&
                   surface.props["tabs"][1]["selected"] == true && surface.props["primary"].is_null(),
               "crew window: our 96 of both crews, crew tab open");
        DraftFlow(
            "transfer crew", router, surface, "crew", 100, "<trade.take> 4",
            [this](const std::string &command) {
                return command == prefix + "CREW_LEFT\t-1\tpress" || command == prefix + "CREW_RIGHT\t-1\tpress";
            },
            [this](const std::vector<std::string> &commands) {
                return commands == std::vector<std::string>(4, prefix + "CREW_LEFT\t-1\tpress");
            },
            [] {}, 96);
        router.OnEvent({"header", "tab", "0"});
        ExpectEqual(surface.Last(), prefix + "CREW_EXIT_BTN\t-1\tpress", "cargo tab closes the crew window");
    }

    void ShipsFromCrew()
    {
        // «Корабли» from the crew window (device, scenario fleet: the cargo opened instead): the game closes the crew
        // window first, and the models until it has still show it open.
        {
            FakeSurface tabs_surface;
            Router tabs_router(tabs_surface);
            InterfaceModel crew = Load(models, "transfer-crew.json");
            tabs_router.SetInterface(crew);
            tabs_router.OnEvent({"header", "tab", "2"});
            ExpectEqual(tabs_surface.Last(), prefix + "CREW_EXIT_BTN\t-1\tpress", "ships tab closes the crew window");
            crew.revision += 1;
            tabs_router.SetInterface(crew);
            Expect(tabs_surface.props["tabs"][2]["selected"] == true && tabs_surface.props["ships"].is_null(),
                   "while the crew window is still open: the ships tab is chosen, the crew stays drawn");
            InterfaceModel closed = Load(models, "transfer-loot.json");
            closed.revision = crew.revision + 1;
            tabs_router.SetInterface(closed);
            Expect(tabs_surface.props["tabs"][2]["selected"] == true && !tabs_surface.props["ships"].is_null(),
                   "the crew window closed: the ships tab opens");
        }
    }
};

static void Transfer(const std::string &models)
{
    TransferRun run{models};
    run.Loot();
    run.CargoRow();
    run.Prisoners();
    run.Crew();
    run.ShipsFromCrew();
}

static void Alchemy(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    const std::string prefix = "widget:alchemy\t";
    router.SetInterface(Load(models, "alchemy.json"));
    Expect(router.Using() == "alchemy" && surface.tag == "s-alchemy" && surface.errors.empty(),
           "alchemy is drawn by the alchemy screen");
    const Json &recipes = surface.props["recipes"]["rows"];
    Expect(recipes.size() == 4 && recipes[0]["selected"] == true && recipes[0]["meta"] == "<alchemy.kind_ammo>" &&
               recipes[1]["meta"] == "",
           "the four real recipes, the shown one selected, the item group as its word");
    const Json &parts = surface.props["parts"]["rows"];
    Expect(parts.size() == 2 && parts[0]["cells"][0]["text"] == "<alchemy.none>" && parts[1]["cells"][0] == "28" &&
               parts[1]["cells"][1] == "1",
           "components: none of the bullets, 28 powder, one each per batch");
    Expect(surface.props["q"]["value"] == 0 && surface.props["q"]["disabled"] == true &&
               surface.props["primary"]["disabled"] == true,
           "nothing to craft: no batches, the craft is off");
    router.OnEvent({"recipes", "row", "potion1"});
    ExpectEqual(surface.Last(), prefix + "ANDROID_ALCHEMY_RECIPE\t1\tchoose@11:potion1",
                "another recipe: the scroll's own tap, bound to the opening");
    surface.commands.clear();
    router.OnEvent({"recipes", "row", "cartridge"});
    Expect(surface.commands.empty(), "the shown recipe is not chosen again");
    router.OnEvent({"info", "click", ""});
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "EXIT_BTN\t-1\tpress", "back leaves through the game's exit");

    router.SetInterface(Load(models, "alchemy-craft.json"));
    Expect(surface.props["q"]["value"] == 1 && surface.props["q"]["max"] == 22,
           "bullets bought: one batch set, up to 22");
    ExpectEqual(surface.props["primary"]["label"], "<alchemy.craft> ×3", "one batch makes three cartridges");
    router.OnEvent({"batches", "commit", "2"});
    ExpectEqual(surface.props["primary"]["label"], "<alchemy.craft> ×6", "two batches make six");
    ExpectEqual(surface.props["primary"]["sub"], "20 <alchemy.minutes>", "15 minutes and 5 for the second batch");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), prefix + "ANDROID_ALCHEMY\t-1\tcraft@11:cartridge=2",
                "craft two batches of the shown recipe");
}

// The capability token a widget of the model offers for `verb` ("yard@7:2:press").
static std::string Yard(const InterfaceModel &model, const char *node, int index, const char *type,
                        const std::string &verb)
{
    for (const Widget &widget : model.widgets)
        if (widget.node == node && widget.index == index && widget.type == type)
            for (const std::string &action : widget.actions)
                if (action.rfind("yard@", 0) == 0 && action.size() > verb.size() &&
                    action.substr(action.size() - verb.size() - 1) == ":" + verb)
                    return "widget:shipyard\t" + std::string(node) + "\t" + std::to_string(index) + "\t" + action;
    return "(no token " + std::string(node) + " " + verb + ")";
}

static Widget Control(const char *node, int index, const std::string &token, const char *type = "shipyard_control")
{
    Widget widget = Make(node, type, "", {token});
    widget.index = index;
    widget.enabled = true;
    return widget;
}

// Shipyard: models with the structure recorded on the device (texts replaced), plus repair and
// captain windows built from them.
// The shipyard on one router: its main window, the preview, selling, cannons, help, repair, the captain of a bought
// ship and the screen without the engine's state.
struct ShipyardRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    InterfaceModel main = Load(models, "shipyard-main.json");

    void Opening()
    {
        router.SetInterface(main);
        Expect(router.Using() == "shipyard" && surface.tag == "s-shipyard" && router.Rejection().empty(),
               "shipyard is drawn by its screen");
        Expect(surface.errors.empty() && surface.props["extras"]["rows"].empty() &&
                   surface.props["extras"]["note"] == "",
               "no errors, no extras");
        Expect(surface.props["offers"].size() == 3 && surface.props["offers"][2]["value"] == "23 824" &&
                   surface.props["offers"][0]["title"] == "Шлюпка «Чайка»" &&
                   surface.props["offers"][0]["meta"] == "<yard.class> 6",
               "the yard's offers: type «name», class, grouped price: " + surface.props["offers"][0].dump());
        Expect(surface.props["fleet"].size() == 2 && surface.props["fleet"][0]["title"] == "Кеч «Марта»" &&
                   surface.props["fleet"][0]["meta"] == "<yard.class> 5" &&
                   surface.props["fleet"][0]["value"] == "11\u00a0575" &&
                   surface.props["fleet"][0]["selected"] == true &&
                   surface.props["fleet"][1]["title"] == "<yard.empty_slot>" && surface.props["subject"].is_null(),
               "our ships: each with its trade-in value, one free place");
        {
            const Json &detail = surface.props["detail"];
            Expect(detail["title"] == "Кеч «Марта»" && detail["actions"].size() == 3 &&
                       detail["actions"][0]["id"] == "repair" && detail["actions"][1]["sub"] == "6 фунт / 12" &&
                       detail["actions"][2]["id"] == "sell" && detail["actions"][2]["hold"] == true &&
                       detail["actions"][2]["sub"] == "11\u00a0575" && surface.props["sheet"].is_null(),
                   "our ship on the card: repair, cannons, sell held with its price; no buy button for it");
            Expect(detail["facts"].size() == 8 &&
                       detail["facts"][0] == Json({{"k", "Корпус"}, {"v", "610 / 610"}, {"icon", "hull"}}) &&
                       detail["against"] == "",
                   "the figures with their icons, nothing to compare our ship with");
        }

        router.OnEvent({"offer-1", "click", ""});
        ExpectEqual(surface.Last(), Yard(main, "TABLE_SHIPYARD", 1, "choice", "select"),
                    "a catalogue row selects the offer with its token");
        surface.commands.clear();
        router.OnEvent({"sell", "click", ""});
        Expect(surface.commands.empty(), "one capability per model revision: the second tap waits for the answer");
        main.revision += 1;
        router.SetInterface(main);
        router.OnEvent({"sell", "click", ""});
        ExpectEqual(surface.Last(), Yard(main, "BUTTON_SELL", -1, "button", "press"), "sell presses the game's button");
        main.revision += 1;
        router.SetInterface(main);
        router.OnEvent({"fleet-1", "click", ""});
        ExpectEqual(surface.Last(), Yard(main, "SHIPS_SCROLL", 1, "choice", "select"),
                    "the free place is a fleet slot of the list");
        main.revision += 1;
        router.SetInterface(main);
        router.OnEvent({"cannons", "click", ""});
        ExpectEqual(surface.Last(), Yard(main, "TABLE_OTHER", 8, "choice", "activate"),
                    "the cannons row opens the game's cannons window");
        main.revision += 1;
        router.SetInterface(main);
        router.OnEvent({"header-back", "click", ""});
        ExpectEqual(surface.Last(), Yard(main, "EXIT_BTN", -1, "shipyard_control", "press"),
                    "← leaves through the game's exit control");
    }

    void NotReady()
    {
        // Not ready: drawn, but nothing is sent.
        InterfaceModel settling = main;
        settling.revision += 1;
        Find(settling, "ANDROID_SHIPYARD_STATE", "shipyard_state")->value = "0";
        router.SetInterface(settling);
        surface.commands.clear();
        router.OnEvent({"sell", "click", ""});
        Expect(surface.commands.empty() && surface.props["detail"]["actions"][2]["disabled"] == true,
               "a state that is not ready offers nothing");
        Expect(surface.errors.empty(), "a state settling for a moment is not an error");
        surface.now += 2.5;
        router.Tick();
        Expect(surface.errors.size() == 1, "a state that stays unusable is reported");
    }

    void Preview()
    {
        const InterfaceModel preview = Load(models, "shipyard-preview.json");
        router.SetInterface(preview);
        {
            // A ship in the chosen slot is traded in; the yard pays what the game says (the adapter's
            // ANDROID_YARDDEAL).
            const Json &detail = surface.props["detail"];
            Expect(detail["title"] == "Шлюпка «Чайка»" && detail["actions"].size() == 1 &&
                       detail["actions"][0]["label"] == "<yard.trade_get>" && detail["actions"][0]["sub"] == "9 300" &&
                       detail["actions"][0]["hold"] == true && surface.props["offers"][0]["selected"] == true &&
                       surface.props["fleet"][0]["selected"] == true && detail["against"] == "Марта",
                   "an offer on the card: traded for our chosen ship, the difference on the held button: " +
                       detail["actions"].dump());
            Expect(detail["facts"][0]["own"] == "610 / 610" && detail["facts"][0]["tone"] == "danger" &&
                       detail["facts"][3]["tone"] == "ok" && !detail["facts"][1].contains("tone"),
                   "its figures beside our ship's: a weaker hull red, a better turn rate green, sails plain: " +
                       detail["facts"].dump());
            // Into the free place it is a plain purchase.
            InterfaceModel free_place = preview;
            for (Widget &widget : free_place.widgets)
                if (widget.node == "SHIPS_SCROLL" && widget.type == "choice")
                    widget.value = widget.index == 1 ? "1" : "0";
            FakeSurface free_surface;
            Router free_router(free_surface);
            free_router.SetInterface(free_place);
            Expect(free_surface.props["detail"]["actions"][0]["label"] == "Купить" &&
                       free_surface.props["detail"]["actions"][0]["sub"] == "2 170" &&
                       !free_surface.props["detail"]["facts"][0].contains("own"),
                   "the free place chosen: buy with the full price, nothing to compare with");

            // A captain without a ship still occupies a fleet slot. Selling must invalidate the
            // old ship comparison even when no owned card is drawn before looking at an offer.
            FakeSurface sold_surface;
            Router sold(sold_surface);
            sold.SetInterface(main);
            sold.SetInterface(preview);
            Expect(sold_surface.props["detail"]["facts"][0].contains("own"),
                   "sale regression starts with a cached owned ship");
            const auto without_ship = [](InterfaceModel model) {
                for (Widget &widget : model.widgets)
                    if (widget.node == "SHIPS_SCROLL" && widget.type == "choice" && widget.index == 0)
                    {
                        widget.context["ownerKind"] = "OWNER_NO_SHIP";
                        widget.context["shipName"] = "Нет корабля";
                        widget.context["saleQuote"] = "0";
                    }
                return model;
            };
            sold.SetInterface(without_ship(main));
            Expect(sold_surface.props["detail"].is_null(), "the sold ship has no owned card");
            sold.SetInterface(without_ship(preview));
            const Json &purchase = sold_surface.props["detail"];
            Expect(purchase["actions"][0]["label"] == "Купить" && purchase["actions"][0]["sub"] == "2 170" &&
                       purchase["against"] == "",
                   "a captain without a ship buys at the full price, with no trade or comparison caption");
            Expect(std::none_of(purchase["facts"].begin(), purchase["facts"].end(),
                                [](const Json &fact) { return fact.contains("own") || fact.contains("tone"); }),
                   "an offer after selling has no old ship figures or comparison colors");
            // A subsequent purchase can fill the same slot before its new owned facts arrive.
            sold.SetInterface(preview);
            Expect(!sold_surface.props["detail"]["facts"][0].contains("own"),
                   "selling erased the slot cache instead of temporarily hiding it");
        }
        router.OnEvent({"primary", "click", ""});
        ExpectEqual(surface.Last(), Yard(preview, "BUTTON_BUY", -1, "button", "press"),
                    "buy presses the game's button");
        InterfaceModel looking = preview;
        looking.revision += 1;
        router.SetInterface(looking);
        router.OnEvent({"fleet-0", "click", ""});
        ExpectEqual(surface.Last(), Yard(looking, "SHIPS_SCROLL", 0, "choice", "select"),
                    "our ship's row brings it back to the card");
        {
            // A deal held on its button is not asked about again: the game's question is answered at once.
            FakeSurface held_surface;
            Router held(held_surface);
            held.SetInterface(Load(models, "shipyard-main.json"));
            held.OnEvent({"sell", "click", ""});
            const InterfaceModel asked = Load(models, "shipyard-confirm-sell.json");
            held.SetInterface(asked);
            ExpectEqual(held_surface.Last(), Yard(asked, "MSG_OK", -1, "button", "press"),
                        "held sell: the game's confirmation is answered");
            Expect(held_surface.props["sheet"].is_null(), "and no sheet asks a second time");
        }
    }

    void Sell()
    {
        const InterfaceModel sell = Load(models, "shipyard-confirm-sell.json");
        router.SetInterface(sell);
        const Json sheet = surface.props["sheet"];
        Expect(sheet["kind"] == "confirm" && sheet["text"] == "Продать корабль за 11 575 песо?" &&
                   sheet["actions"].size() == 2 && sheet["actions"][1]["variant"] == "danger" &&
                   sheet["actions"][1]["label"] == "Продать",
               "selling asks with the game's text and a danger button named by the deal");
        router.OnEvent({"sheet-ok", "click", ""});
        ExpectEqual(surface.Last(), Yard(sell, "MSG_OK", -1, "button", "press"), "confirm presses the game's OK");
        InterfaceModel again = sell;
        again.revision += 1;
        router.SetInterface(again);
        router.Back();
        ExpectEqual(surface.Last(), Yard(again, "MSG_CANCEL", -1, "button", "press"),
                    "the platform's back closes the window first, as ← does");
    }

    void Cannons()
    {
        const InterfaceModel cannons = Load(models, "shipyard-cannons.json");
        router.SetInterface(cannons);
        {
            // The stock window's plan: the game's deck picture (bow to the left), bow left, stern right, starboard
            // (cannonr, CANNONS_QTY_R) above, port (cannonl, CANNONS_QTY_L) below. shipyard-cannons.json: a ketch with
            // bow chasers only.
            const Json &sheet = surface.props["sheet"];
            Expect(sheet["kind"] == "cannons" && sheet["bow"]["value"] == "2" &&
                       sheet["bow"]["label"] == "<yard.bow>" && sheet["stern"]["value"] == "0" &&
                       sheet["port"]["value"] == "5" && sheet["port"]["label"] == "<yard.port>" &&
                       sheet["starboard"]["value"] == "5" && sheet["starboard"]["label"] == "<yard.starboard>",
                   "cannons window: each side with the game's count: " + sheet.dump());
            Expect(sheet["plan"]["texture"] == "interfaces/gundeck" && sheet["plan"]["uv"] == "0,0.31,1,0.75" &&
                       std::none_of(surface.errors.begin(), surface.errors.end(),
                                    [](const std::string &e) { return e.find("CANNONS_SHIP") != std::string::npos; }),
                   "the game's deck plan, cut to the ship's band: " + sheet["plan"].dump());
        }
        router.OnEvent({"sheet-close", "click", ""});
        ExpectEqual(surface.Last(), Yard(cannons, "CANNONS_CANCEL", -1, "button", "press"),
                    "closing it presses the game's cancel");
    }

    void Help()
    {
        const InterfaceModel help = Load(models, "shipyard-help.json");
        router.SetInterface(help);
        Expect(surface.props["sheet"]["kind"] == "help" && surface.props["sheet"]["lines"].size() == 1,
               "help window with the game's text");
        router.OnEvent({"sheet", "dismiss", ""});
        ExpectEqual(surface.Last(), Yard(help, "ANDROID_SHIPYARD_CLOSE", -1, "shipyard_control", "close"),
                    "the scrim closes help through the engine's close control");

        {
            // The hero's sheet over a hero-only ship (shipyard-rpg.json, RPG_WINDOW): SetSPECIALMiniTable names the
            // ship skills only by their pictures (row tr5, ICONS_SPEC "<skill> skill icon") and puts the values in tr6,
            // green where the skill is raised and red where it is lowered.
            FakeSurface rpg_surface;
            Router rpg_router(rpg_surface);
            const InterfaceModel rpg = Load(models, "shipyard-rpg.json");
            rpg_router.SetInterface(rpg);
            const Json &sheet = rpg_surface.props["sheet"];
            Expect(rpg_surface.errors.empty() && rpg_surface.props["extras"]["rows"].empty() &&
                       sheet["kind"] == "rpg" && sheet["name"] == "Жан Тестов" && sheet["facts"].size() == 5 &&
                       sheet["facts"][0] == Json({{"k", "Ранг"}, {"v", "1"}}),
                   "the hero's sheet: name and the game's facts, every widget known");
            Expect(sheet["skills"].size() == 7 &&
                       sheet["skills"][0] ==
                           Json({{"icon", "wheel"}, {"k", "<skill.sailing>"}, {"v", "41"}, {"tone", "ok"}}) &&
                       sheet["skills"][5] ==
                           Json({{"icon", "repair"}, {"k", "<skill.repair>"}, {"v", "11"}, {"tone", "danger"}}) &&
                       sheet["skills"][6] ==
                           Json({{"icon", "commerce"}, {"k", "<skill.commerce>"}, {"v", "8"}, {"tone", ""}}),
                   "the ship skills: named from the game's pictures, with tr6's values");
            rpg_router.OnEvent({"sheet-close", "click", ""});
            ExpectEqual(rpg_surface.Last(), Yard(rpg, "ANDROID_SHIPYARD_CLOSE", -1, "shipyard_control", "close"),
                        "the sheet closes through the engine's close control");
        }
    }

    void RepairWithTheGame()
    {
        {
            // shipyard-repair.json: the hull at 50 %, the sails at 70 %. The game decides how far its arrows go and
            // what the order costs; the screen shows where they stopped.
            FakeSurface yard_surface;
            Router yard(yard_surface);
            InterfaceModel model = Load(models, "shipyard-repair.json");
            yard.SetInterface(model);
            const auto q = [&yard_surface](int part) { return yard_surface.props["sheet"]["parts"][part]["q"]; };
            Expect(yard_surface.errors.empty() && q(0)["value"] == 50 && q(0)["min"] == 0 && q(0)["max"] == 100 &&
                       q(0)["floor"] == 50 && !q(0).contains("ceiling") && q(1)["value"] == 70 && q(1)["floor"] == 70 &&
                       !q(1).contains("ceiling"),
                   "the sliders run 0…100 % from the ship's state, no ceiling of ours: " + q(0).dump() + q(1).dump());
            yard_surface.commands.clear();
            yard.OnEvent({"hull", "commit", "30"});
            Expect(q(0)["value"] == 50 && yard_surface.commands.empty(),
                   "below the ship's state: the slider stays at 50 %, nothing is sent");

            // «Все» is the game's own button (shipyard.c RepairAll: the sails first, then the hull).
            yard.OnEvent({"sheet-all", "click", ""});
            Expect(yard_surface.commands.size() == 1 &&
                       yard_surface.commands[0] == "widget:shipyard\tREPAIR_ALL\t-1\tyard@7:30:press",
                   "«Все» presses the game's REPAIR_ALL: " + Json(yard_surface.commands).dump());
            InterfaceModel all = model;
            all.revision += 1;
            Find(all, "REPAIR_QTY_H", "text")->label = "94%";
            Find(all, "REPAIR_QTY_S", "text")->label = "100%";
            Find(all, "REPAIR_WINDOW_TEXT", "text")->label = "2495";
            yard.SetInterface(all);
            Expect(q(0)["value"] == 94 && q(1)["value"] == 100 &&
                       yard_surface.props["sheet"]["actions"][1]["sub"] == "2\xC2\xA0"
                                                                           "495" &&
                       yard_surface.commands.size() == 1,
                   "the sliders and the price show what the game set: " + yard_surface.props["sheet"].dump());
            Expect(yard_surface.props["sheet"]["actions"][0]["label"] == "<common.all>" &&
                       yard_surface.props["sheet"]["actions"][1]["label"] == "<repair.do>",
                   "the port's words: «Все» fills, «Починить» repairs (as the self repair)");
        }
        {
            // A slider pushed past the purse: the game's arrows stop short, the slider shows where; OK waits for the
            // steps, then repairs once.
            FakeSurface yard_surface;
            Router yard(yard_surface);
            InterfaceModel model = Load(models, "shipyard-repair.json");
            yard.SetInterface(model);
            yard_surface.commands.clear();
            yard.OnEvent({"hull", "commit", "100"});
            yard.OnEvent({"sheet-ok", "click", ""});
            int hull = 50;
            const int purse_stop = 62; // where the game's arrows stop in this run
            for (int round = 0; round < 40; ++round)
            {
                const std::string last = yard_surface.commands.back();
                if (last.find("REPAIR_OK") != std::string::npos)
                    break;
                Expect(last.find("REPAIR_RIGHT_H") != std::string::npos, "only the hull's arrows: " + last);
                hull = std::min(purse_stop, hull + (last.find("step5") != std::string::npos ? 5 : 1));
                model.revision += 1;
                Find(model, "REPAIR_QTY_H", "text")->label = std::to_string(hull) + "%";
                Find(model, "REPAIR_WINDOW_TEXT", "text")->label = std::to_string(hull * 10);
                yard.SetInterface(model);
            }
            Expect(hull == purse_stop &&
                       yard_surface.commands.back() == "widget:shipyard\tREPAIR_OK\t-1\tyard@7:30:press" &&
                       yard_surface.props["sheet"]["parts"][0]["q"]["value"] == purse_stop,
                   "the game stopped at 62 %: so does the slider, then OK: " + Json(yard_surface.commands).dump());
            size_t oks = 0;
            for (const std::string &command : yard_surface.commands)
                oks += command.find("REPAIR_OK") != std::string::npos;
            Expect(oks == 1, "OK went to the game once, after the last step");
        }
    }

    void Repair()
    {
        // Repair: the quantity asks for a percentage; one step per revision toward it.
        InterfaceModel repair = Load(models, "shipyard-main.json");
        repair.revision = 100;
        Find(repair, "ANDROID_SHIPYARD_STATE", "shipyard_state")->context["mode"] = "repair";
        const auto label = [](const char *node, const char *text) {
            Widget widget = Make(node, "text", text);
            widget.index = 0;
            return widget;
        };
        repair.widgets.push_back(label("REPAIR_WINDOW_CAPTION", "Ремонт"));
        repair.widgets.push_back(label("REPAIR_WINDOW_TEXT", "0"));
        repair.widgets.push_back(label("REPAIR_QTY_H", "88%"));
        repair.widgets.push_back(label("REPAIR_QTY_S", "95%"));
        repair.widgets.push_back(Control("REPAIR_RIGHT_H", -1, "yard@7:9:press"));
        repair.widgets.push_back(Control("REPAIR_RIGHT_H", -5, "yard@7:9:step5"));
        repair.widgets.push_back(Control("REPAIR_LEFT_H", -1, "yard@7:9:press"));
        repair.widgets.push_back(Control("REPAIR_LEFT_H", -5, "yard@7:9:step5"));
        repair.widgets.push_back(Control("REPAIR_OK", -1, "yard@7:9:press", "button"));
        repair.widgets.push_back(Control("REPAIR_ALL", -1, "yard@7:9:press", "button"));
        repair.widgets.push_back(Control("REPAIR_CANCEL", -1, "yard@7:9:press", "button"));
        for (Widget &widget : repair.widgets)
            if (widget.node == "REPAIR_OK" || widget.node == "REPAIR_ALL" || widget.node == "REPAIR_CANCEL")
                widget.label = widget.node == "REPAIR_OK"    ? "Починить"
                               : widget.node == "REPAIR_ALL" ? "Чинить всё"
                                                             : "Отмена";
        router.SetInterface(repair);
        const Json parts = surface.props["sheet"]["parts"];
        Expect(surface.props["sheet"]["kind"] == "repair" && parts.size() == 2 && parts[0]["q"]["value"] == 88 &&
                   parts[0]["q"]["min"] == 0 && parts[0]["q"]["floor"] == 88 && parts[0]["q"]["max"] == 100 &&
                   !parts[0]["q"].contains("ceiling") && parts[0]["q"]["label"] == "Корпус",
               "repair: 0…100 % from the ship's state: " + parts.dump());
        surface.commands.clear();
        router.OnEvent({"hull", "commit", "100"});
        ExpectEqual(surface.Last(), "widget:shipyard\tREPAIR_RIGHT_H\t-5\tyard@7:9:step5",
                    "far from the target: a step of 5");
        Expect(surface.commands.size() == 1, "one step per revision");
        repair.revision += 1;
        for (Widget &widget : repair.widgets)
            if (widget.node == "REPAIR_QTY_H")
                widget.label = "93%";
        router.SetInterface(repair);
        Expect(surface.commands.size() == 2 && surface.Last() == "widget:shipyard\tREPAIR_RIGHT_H\t-5\tyard@7:9:step5",
               "the next revision takes the next step");
        repair.revision += 1;
        for (Widget &widget : repair.widgets)
            if (widget.node == "REPAIR_QTY_H")
                widget.label = "98%";
        router.SetInterface(repair);
        Expect(surface.commands.size() == 3 && surface.Last() == "widget:shipyard\tREPAIR_RIGHT_H\t-1\tyard@7:9:press",
               "close to it: steps of 1");
        repair.revision += 1;
        router.SetInterface(repair); // the game did not move (money)
        Expect(surface.commands.size() == 3, "when the game stops moving, so does the screen");
        repair.revision += 1;
        router.SetInterface(repair);
        router.OnEvent({"sheet-ok", "click", ""});
        ExpectEqual(surface.Last(), "widget:shipyard\tREPAIR_OK\t-1\tyard@7:9:press",
                    "repair confirms with the game's button");
    }

    void Captain()
    {
        // Captain: choose who takes the bought ship.
        InterfaceModel captain = Load(models, "shipyard-main.json");
        captain.revision = 200;
        Find(captain, "ANDROID_SHIPYARD_STATE", "shipyard_state")->context["mode"] = "captain";
        Widget first = Control("PASSENGERSLIST", 0, "yard@7:11:select", "choice");
        first.label = "Пьер Тест";
        first.value = "1";
        Widget second = Control("PASSENGERSLIST", 1, "yard@7:11:select", "choice");
        second.label = "Анна Тест";
        captain.widgets.push_back(first);
        captain.widgets.push_back(second);
        captain.widgets.push_back(Control("ACCEPT_ADD_OFFICER", -1, "yard@7:11:press", "button"));
        captain.widgets.push_back(Control("CANCEL_ADD_OFFICER", -1, "yard@7:11:press", "button"));
        router.SetInterface(captain);
        Expect(surface.props["sheet"]["kind"] == "captain" && surface.props["sheet"]["candidates"].size() == 2 &&
                   surface.props["sheet"]["candidates"][0]["selected"] == true,
               "captain: the candidates");
        router.OnEvent({"candidate-1", "click", ""});
        ExpectEqual(surface.Last(), "widget:shipyard\tPASSENGERSLIST\t1\tyard@7:11:select",
                    "a candidate is selected with its token");
        captain.revision += 1;
        router.SetInterface(captain);
        router.OnEvent({"sheet-close", "click", ""});
        ExpectEqual(surface.Last(), "widget:shipyard\tCANCEL_ADD_OFFICER\t-1\tyard@7:11:press",
                    "closing presses the game's cancel");
    }

    void WithoutState()
    {
        // Without the engine's shipyard state the screen cannot work: generic layout, loudly.
        InterfaceModel broken = Load(models, "shipyard-main.json");
        broken.opening = 99;
        broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(),
                                            [](const Widget &w) { return w.type == "shipyard_state"; }),
                             broken.widgets.end());
        router.SetInterface(broken);
        surface.now += 1.0;
        router.Tick();
        Expect(!router.Active() && router.Rejection() == "ANDROID_SHIPYARD_STATE(shipyard_state)" &&
                   !surface.errors.empty(),
               "without its state widget the shipyard is rejected loudly");
    }
};

static void Shipyard(const std::string &models)
{
    ShipyardRun run{models};
    run.Opening();
    run.NotReady();
    run.Preview();
    run.Sell();
    run.Cannons();
    run.Help();
    run.RepairWithTheGame();
    run.Repair();
    run.Captain();
    run.WithoutState();
}

static void Atlas()
{
    PictureAtlas atlas;
    const size_t count = atlas.Load(
        "; comment\n[GOODS]\nsTextureName\t= goods.tga\nwTextureWidth = 1024\nwTextureHeight = 512\n"
        "picture = Balls,128,256,256,384\npicture = Rum,1,2,x,4\n\n[icons]\nsTextureName = "
        "..\\BATTLE_INTERFACE\\land_icon.tga\n"
        "wTextureWidth = 256\nwTextureHeight = 256\npicture = talk, 0,0,64,64 ; first\npicture = broken,1\n");
    Expect(count == 2, "two well-formed pictures");
    const AtlasPicture *balls = atlas.Find("goods", "BALLS");
    Expect(balls && balls->texture == "INTERFACES/goods" && balls->uv[0] == 0.125f && balls->uv[1] == 0.5f &&
               balls->uv[2] == 0.25f && balls->uv[3] == 0.75f,
           "group and picture are case-insensitive; the rectangle becomes texture coordinates");
    const AtlasPicture *talk = atlas.Find("ICONS", "talk");
    Expect(talk && talk->texture == "BATTLE_INTERFACE/land_icon" && talk->uv[2] == 0.25f,
           "a texture outside INTERFACES is resolved");
    Expect(!atlas.Find("GOODS", "Rum") && !atlas.Find("ICONS", "broken"),
           "unknown and malformed pictures are not found");
}

static Json LoadJson(const std::string &directory, const char *name)
{
    std::ifstream file(directory + "/" + name, std::ios::binary);
    return Json::parse(file, nullptr, false);
}

namespace storm::ui::screens
{
std::string DialogueAnswerError(const std::string &text, int max_code_points);
}

static void DialogueChannel(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("dialogue", LoadJson(models, "dialogue.json"));
    ExpectEqual(surface.tag, "s-dialogue", "dialogue channel mounts the dialogue");
    ExpectEqual(surface.language, "russian", "the dialogue speaks the language the engine sends with it");
    Expect(surface.props["replies"].size() == 3 && !surface.props["replies"][0].contains("hold"),
           "replies listed; their long-press text is the panel's matter, not the screen's");
    Expect(surface.props["page"] == 0 && surface.props["edit"].is_null(),
           "the speech starts on its first page, no editor");
    router.OnEvent({"reply-1", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:4:1", "reply goes back with its generation");
    Expect(surface.props["replies"][1]["pressed"] == true, "the reply sent looks busy until the engine answers");
    router.OnEvent({"dialogue-next", "click", ""});
    Expect(surface.props["page"] == 1, "«next» turns the page (the pager clamps at the last one)");
    Expect(!router.WantsBack(), "without an editor the back key stays the host's");
    router.SetState("dialogue", Json{{"active", false}});
    Expect(surface.tag.empty(), "inactive dialogue unmounts");
    router.SetState("weather", Json::object());
    Expect(surface.tag.empty() && surface.errors.size() == 1 &&
               surface.errors[0] == "state channel weather has no consumer",
           "a channel without a consumer is an error");

    // Texts with the game's escaped line breaks, and an empty reply.
    router.SetState("dialogue",
                    Json{{"active", true},
                         {"generation", 5},
                         {"speaker", "A"},
                         {"text", "one\\ntwo\\r"},
                         {"choices", Json::array({Json{{"index", 0}, {"text", " "}}, Json{{"index", 2}, {"text", "x"}},
                                                  Json{{"text", "no index"}}})}});
    ExpectEqual(surface.props["text"].get<std::string>(), "one\ntwo", "escaped line breaks become lines");
    ExpectEqual(surface.props["replies"][0]["title"].get<std::string>(), "<dialogue.continue>",
                "an empty reply says what it does");
    Expect(surface.props["replies"].size() == 2 && surface.errors.size() == 2,
           "a choice without an index is reported, not shown");

    // The engine hands over the struct: the same dialogue as its fixture, the same screen.
    const Json shown = surface.props;
    storm::ui::Dialogue typed;
    typed.active = true;
    typed.generation = 5;
    typed.speaker = "A";
    typed.text = "one\\ntwo\\r";
    typed.choices = {{0, " "}, {2, "x"}};
    router.SetDialogue(typed);
    Expect(surface.props == shown && surface.errors.size() == 2, "the engine's struct shows as its fixture does");
    router.SetDialogue(storm::ui::Dialogue());
    Expect(surface.tag.empty(), "an inactive struct unmounts");
}

// Caught stealing (TEHO itemsbox.c, LAi_owner.c): the chest's interface does not pause the game
// and the owner who saw the hero in it starts his dialogue the moment it closes. The game's
// close of the chest reaches the UI after a grace (engine_ui.cpp), when the dialogue panel may
// already be up: the close must not take it, or the game waits in a dialogue nobody sees.
static void DialogueOverClosingInterface(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "itemsbox.json"));
    ExpectEqual(router.Model().screen, "itemsbox", "the chest is open");
    router.SetState("dialogue", LoadJson(models, "dialogue.json"));
    ExpectEqual(surface.tag, "s-dialogue", "the owner's dialogue comes while the chest is still closing");
    router.CloseGameInterface("itemsbox");
    Expect(surface.tag == "s-dialogue" && router.Model().screen == "dialogue",
           "the chest's late close leaves the dialogue up");
    Expect(!surface.logs.empty() &&
               surface.logs.back().find("interface itemsbox closed under the dialogue screen") != std::string::npos,
           "and says so in the log");
    router.OnEvent({"reply-0", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:4:0", "its reply still answers the dialogue");

    // A game interface the dialogue's script opens over it: when it closes, the dialogue the
    // engine still holds comes back (the engine sends a node once).
    router.SetInterface(Load(models, "itemsbox.json"));
    Expect(surface.tag != "s-dialogue", "an interface opened from the dialogue covers it");
    router.CloseGameInterface("itemsbox");
    Expect(surface.tag == "s-dialogue" && router.Model().opening == 4, "its close brings the same dialogue back");

    // A dialogue that ended under the interface does not come back.
    router.SetInterface(Load(models, "itemsbox.json"));
    router.SetState("dialogue", Json{{"active", false}});
    ExpectEqual(router.Model().screen, "itemsbox", "the end of a covered dialogue leaves the interface");
    router.CloseGameInterface("itemsbox");
    Expect(surface.tag.empty() && !router.Active(), "and nothing is left when the interface closes");
    Expect(surface.errors.empty(), "no errors on the way");
}

static void DialogueText(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("dialogue", LoadJson(models, "dialogue-edit.json"));
    router.OnEvent({"reply-0", "click", ""});
    Expect(surface.commands.empty(), "a reply the player types opens the editor instead of answering");
    Expect(surface.props["edit"]["kind"] == "text" && surface.props["edit"]["value"] == "Аделина" &&
               surface.props["edit"]["error"] == "",
           "the text editor proposes the reply's text");
    Expect(router.WantsBack(), "the back key closes the editor");
    router.OnEvent({"edit-field", "focus", ""});
    Expect(surface.text.text == "Аделина" && !surface.text.numeric && !surface.text.token.empty(),
           "the field opens the keyboard with the draft");
    router.SubmitText(surface.text.token, "Мария", true);
    ExpectEqual(surface.Last(), "dialogue:9:0:Мария", "the keyboard's «Done» sends the typed answer");
    Expect(surface.props["edit"]["actions"][1]["disabled"] == true, "«Done» waits for the engine's answer");
    router.OnEvent({"edit-done", "click", ""});
    Expect(surface.commands.size() == 1, "nothing goes twice while the engine decides");

    Json refused = LoadJson(models, "dialogue-edit.json");
    refused["editIndex"] = 0;
    refused["editError"] = "too_long";
    router.SetState("dialogue", refused);
    Expect(surface.props["edit"]["error"] == "<dialogue.too_long>" && surface.props["edit"]["value"] == "Мария" &&
               surface.props["edit"]["actions"][1]["disabled"] == false,
           "an answer the engine refuses keeps the draft and says why");
    router.OnEvent({"edit-done", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:9:0:Мария", "the same answer can be sent again");
    router.SetState("dialogue", refused);
    Expect(surface.props["edit"]["error"] == "<dialogue.too_long>", "a repeated refusal is shown again");

    router.OnEvent({"edit-field", "click", ""});
    const size_t sent = surface.commands.size();
    router.SubmitText(surface.text.token,
                      std::string("a\x01"
                                  "b"),
                      true);
    Expect(surface.commands.size() == sent && surface.props["edit"]["error"] == "<dialogue.invalid>",
           "a control character is refused before sending");
    router.OnEvent({"edit-field", "click", ""});
    router.SubmitText(surface.text.token, "ignored", false);
    Expect(surface.props["edit"]["value"] == "a\x01"
                                             "b",
           "a cancelled keyboard keeps the draft");
    router.OnEvent({"edit-cancel", "click", ""});
    Expect(surface.props["edit"].is_null() && !router.WantsBack(), "«Cancel» closes the editor");
    router.OnEvent({"reply-0", "click", ""});
    router.Back();
    Expect(surface.props["edit"].is_null(), "the back key closes the editor");

    Json unknown = LoadJson(models, "dialogue-edit.json");
    unknown["editIndex"] = 0;
    unknown["editError"] = "strange";
    const size_t errors = surface.errors.size();
    router.SetState("dialogue", unknown);
    Expect(surface.errors.size() == errors + 1, "an unknown refusal code is reported");

    Json next = LoadJson(models, "dialogue-edit.json");
    router.OnEvent({"reply-0", "click", ""});
    next["generation"] = 10;
    router.SetState("dialogue", next);
    Expect(surface.props["edit"].is_null(), "a new utterance closes the editor");

    ExpectEqual(DialogueAnswerError("  Привет: \"да\" \xF0\x9F\x98\x80  ", 256), "",
                "multilingual input with an emoji passes");
    ExpectEqual(DialogueAnswerError("", 256), "", "an empty answer passes");
    std::string emoji;
    for (int i = 0; i < 256; ++i)
        emoji += "\xF0\x9F\x98\x80";
    ExpectEqual(DialogueAnswerError(emoji, 256), "", "256 code points pass (code points, not bytes or UTF-16 units)");
    ExpectEqual(DialogueAnswerError(emoji + "a", 256), "too_long", "the code point limit");
    ExpectEqual(DialogueAnswerError("abcd", 3), "too_long", "the reply's own limit");
    ExpectEqual(DialogueAnswerError("\xE2\x80\xA8", 256), "invalid_text", "a line separator is refused");
    ExpectEqual(DialogueAnswerError("\xC2\x85", 256), "invalid_text", "a C1 control is refused");
    ExpectEqual(DialogueAnswerError("a\xED\xA0\x80"
                                    "b",
                                    256),
                "invalid_text", "a lone surrogate is refused");
    ExpectEqual(DialogueAnswerError(" \xC2\xA0", 256), "", "spaces pass");
}

static void DialogueNumber(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetState("dialogue", LoadJson(models, "dialogue-bet.json"));
    router.OnEvent({"reply-0", "click", ""});
    const Json edit = surface.props["edit"];
    Expect(edit["kind"] == "number" && edit["value"] == 100 && edit["max"] == 12500 && edit["money"] == 12500,
           "a whole-number proposal opens the number panel up to the hero's money");
    router.OnEvent({"edit-amount", "change", "3000"});
    Expect(surface.props["edit"]["value"] == 3000 && surface.props["edit"]["actions"][1]["sub"] == "3000",
           "the amount follows the control");
    router.OnEvent({"edit-amount", "commit", "99999"});
    Expect(surface.props["edit"]["value"] == 12500, "no more than the hero has");
    router.OnEvent({"edit-done", "click", ""});
    ExpectEqual(surface.Last(), "dialogue:11:0:12500", "the amount goes back as the typed answer");

    Json poor = LoadJson(models, "dialogue-bet.json");
    poor.erase("money");
    poor["generation"] = 12;
    router.SetState("dialogue", poor);
    router.OnEvent({"reply-0", "click", ""});
    Expect(surface.props["edit"]["max"] == 100 && surface.props["edit"]["money"] == -1,
           "without the hero's money the proposal is the limit");
}

// The nations screen's script over the fixture game's nations (engine/tests/ui_scripts/game): England, France,
// Spain, Holland and the pirates, at war with the pirates, France allied with Holland.
static void Nations(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    InterfaceModel model = Load(models, "nationrelation.json");
    router.SetInterface(model);
    ExpectEqual(router.Using(), "nationrelation", "the relations draw on the nations screen");
    ExpectEqual(surface.tag, "s-nations", "s-nations mounted");
    const Json &view = surface.props;
    Expect(view["columns"].size() == 5 && view["rows"].size() == 5 && view["reputation"].size() == 4,
           "five nations, four reputations (the window's)");
    Expect(view["rows"][0]["cells"][0]["self"] == true && view["rows"][0]["cells"][1]["icon"] == "rel-neutral" &&
               view["rows"][0]["cells"][4]["icon"] == "rel-war" && view["rows"][1]["cells"][3]["icon"] == "rel-ally",
           "the game's relations become the legend glyphs");
    Expect(view["columns"][4]["icon"] == "nation-pirates" && view["rows"][1]["icon"] == "nation-france",
           "our flags for the nations");
    Expect(view["flag"]["label"] == "France" && view["apply"]["disabled"] == true && view["flagsDisabled"] == false &&
               view["flags"].size() == 5 && view["flags"][1]["selected"] == true && view["flags"][1]["id"] == "1" &&
               view["flags"][4]["icon"] == "nation-pirates" && view["flags"][4]["disabled"] == false,
           "five flags to pick (the game says which it allows), the candidate selected, «change flag» not offered");
    Expect(view["reputation"][1]["bar"] == 67 && view["reputation"][1]["tone"] == "ok" &&
               view["reputation"][2]["bar"] == 30 && view["reputation"][2]["tone"] == "danger" &&
               view["reputation"][0]["tone"] == "" && view["reputation"][0]["value"] == "Neutral",
           "the game's numbers become bars (-100 … 100), the tone of the game's word, the window's word");
    Expect(view["helpFlag"] == true && view["helpReputation"] == true, "help offered");
    Expect(view["extras"]["rows"].empty() && view["extras"]["note"] == "", "every widget is known: no extras");
    ExpectEqual(view["title"].get<std::string>(), "Relations", "the game's title, trimmed");

    const auto answer = [&](const char *flag) {
        AnswerAll(router, surface, true);
        model.revision += 1;
        for (Widget &widget : model.widgets)
            if (widget.node == "FlagPic")
                widget.images[0].image = flag;
        router.SetInterface(model);
    };
    router.OnEvent({"flags", "pick", "1"});
    Expect(surface.commands.empty(), "picking the candidate again sends nothing");
    router.OnEvent({"flags", "pick", "4"});
    ExpectEqual(surface.Last(), "widget:nationrelation\tRIGHTCHANGE_NATION\t-1\tpress", "a pick presses ›");
    answer("Spain");
    Expect(surface.commands.size() == 2, "the game's next candidate is not the flag asked for: › again");
    answer("France");
    Expect(surface.commands.size() == 2 && surface.props["flags"][4]["disabled"] == true &&
               surface.props["flags"][1]["selected"] == true && surface.props["flags"][2]["disabled"] == false,
           "back where it started: the game does not offer that flag, off for this opening");
    router.OnEvent({"flags", "pick", "4"});
    Expect(surface.commands.size() == 2, "and it is not asked again");
    router.OnEvent({"flags", "pick", "2"});
    answer("Spain");
    Expect(surface.commands.size() == 3 && surface.props["flags"][2]["selected"] == true,
           "a flag the game reached is the candidate");

    router.OnEvent({"help-flag", "click", ""});
    Expect(surface.props["extras"]["readingTitle"] == "Nation" &&
               surface.props["extras"]["readingText"] == "Nation_hint text\n\nmore",
           "the flag's (i): the game's help, its tilde lines a paragraph break");
    router.Back();
    router.OnEvent({"flag-apply", "click", ""});
    Expect(surface.commands.size() == 3, "«change flag» not offered: nothing sent");
    for (Widget &widget : model.widgets)
        if (widget.node == "FLAGS")
            widget.enabled = true, widget.actions = {"press"};
    model.revision += 1;
    router.SetInterface(model);
    router.OnEvent({"flag-apply", "click", ""});
    ExpectEqual(surface.Last(), "widget:nationrelation\tFLAGS\t-1\tpress", "«change flag» when offered");
    AnswerAll(router, surface, true);
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:nationrelation\tI_SHIP\t-1\tpress", "a header section opens that section");
    router.Tick();
    Expect(surface.props["headerSection"] == "ship", "drawn chosen at the tap");
    AnswerAll(router, surface, true);
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "back", "← without an exit button: the engine's back");

    // Without the flag candidate the screen is not used, loudly.
    InterfaceModel broken = Load(models, "nationrelation.json");
    broken.opening = 100;
    std::erase_if(broken.widgets, [](const Widget &w) { return w.node == "FlagPic"; });
    router.SetInterface(broken);
    surface.now += 2.0;
    router.Tick();
    Expect(!router.Active() && router.Rejection() == "FlagPic(image)", "without its candidate it is rejected loudly");
}

static Widget *Row(InterfaceModel &model, const char *node, int index)
{
    for (Widget &widget : model.widgets)
        if (widget.node == node && widget.index == index)
            return &widget;
    return nullptr;
}

// The hero screen on one router: the skill tables and their help, the subjects, perks, officers and sections.
struct HeroRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    InterfaceModel model = Load(models, "hero.json");
    Json view; // what the screen showed last

    void Opening()
    {
        router.SetInterface(model);
        ExpectEqual(router.Using(), "lscchar_all", "the character draws on the hero screen");
        ExpectEqual(surface.tag, "s-hero", "s-hero mounted");
        view = surface.props;
        Expect(view["headerSection"] == "hero" && view["tabs"].size() == 4 && view["tabs"][0]["badge"] == 2 &&
                   view["tabs"][1]["id"] == "perks1" && view["tabs"][1]["badge"] == 1 &&
                   view["tabs"][2]["id"] == "perks2" && view["tabs"][3]["id"] == "officers",
               "skills, the two perk groups and officers; free skill and perk points as badges: " +
                   view["tabs"].dump());
        Expect(view["card"]["title"] == "Anne Bonny" && view["card"]["portrait"] == "INTERFACES/PORTRAITS/128/face_7" &&
                   view["card"]["health"]["value"] == 75 && view["card"]["health"]["text"] == "90" &&
                   view["card"]["free"] == "2" && view["card"]["lines"][0]["v"] == "excellent" &&
                   view["card"]["lines"][1]["v"] == "63 / 100",
               "the identity rail from TABLE_OTHER and the adapter");
        Expect(view["special"]["rows"].size() == 2 && view["special"]["rows"][0]["cells"][1]["text"] == "5" &&
                   !view["special"]["rows"][0]["cells"][1].contains("tone") &&
                   view["special"]["rows"][1]["cells"][1]["text"] == "8" &&
                   view["special"]["rows"][1]["cells"][1]["tone"] == "ok",
               "the read-only PIRATES column: the value the game uses (Agility base 7 (+1) = 8, green): " +
                   view["special"].dump());
        {
            // Without points to spend every table shows the value the game uses, base + the stock bonus column, and the
            // breakdown of a row ends on that same figure.
            InterfaceModel spent = model;
            for (Widget &widget : spent.widgets)
                if (widget.node == "FREE_SKILL")
                    widget.label = "";
            std::erase_if(spent.widgets, [](const Widget &widget) { return widget.node == "ANDROID_SKILLHELP"; });
            Widget fact = Make("ANDROID_SKILLHELP", "fact", "");
            fact.row_id = "TABLE_SKILL_1_tr2";
            fact.context = {{"node", "TABLE_SKILL_1"},
                            {"skill", Row(spent, "TABLE_SKILL_1", 1)->row_id},
                            {"text", "Blades."},
                            {"base", "3"},
                            {"steps", "1"}};
            spent.widgets.push_back(std::move(fact));
            Widget step = Make("ANDROID_SKILLSTEPS", "fact", "");
            step.row_id = "TABLE_SKILL_1_tr2_0";
            step.context = {{"node", "TABLE_SKILL_1"},
                            {"skill", Row(spent, "TABLE_SKILL_1", 1)->row_id},
                            {"kind", "navy"},
                            {"a1", "3"},
                            {"a2", "63"},
                            {"a3", "65"},
                            {"value", "-2"}};
            spent.widgets.push_back(std::move(step));
            FakeSurface totals_surface;
            Router totals(totals_surface);
            totals.SetInterface(spent);
            const Json personal = totals_surface.props["personal"]["rows"];
            Expect(totals_surface.props["points"] == false && personal[0]["cells"][2]["text"] == "12" &&
                       personal[0]["cells"][2]["tone"] == "gold" && personal[1]["cells"][2]["text"] == "1" &&
                       personal[1]["cells"][2]["tone"] == "danger" &&
                       totals_surface.props["ship"]["rows"][0]["cells"][2]["text"] == "11" &&
                       totals_surface.errors.empty(),
                   "the skills show base + bonus (Rapiers 3 (-2) = 1, red; no bonus: the base, gold): " +
                       totals_surface.props["personal"].dump());
            totals.OnEvent({"personal", "row", "1"});
            const Json steps = totals_surface.props["dialog"]["steps"];
            Expect(steps.size() == 3 && steps[2]["k"] == "<hero.step_result>" &&
                       steps[2]["v"] == personal[1]["cells"][2]["text"] && totals_surface.errors.empty(),
                   "the breakdown's result is the row's figure: " + steps.dump());

            // A bonus cell of another shape is not guessed at: the row shows its base, untinted, and it is an error.
            for (const char *odd : {"+1", "(+0)", "(1)", "(+1", "(+x)"})
            {
                InterfaceModel malformed = spent;
                Row(malformed, "TABLE_SPECIAL", 1)->cells[2].text = odd;
                FakeSurface odd_surface;
                Router odd_router(odd_surface);
                odd_router.SetInterface(malformed);
                const Json &cell = odd_surface.props["special"]["rows"][1]["cells"][1];
                Expect(cell["text"] == "7" && !cell.contains("tone") && odd_surface.errors.size() == 1 &&
                           odd_surface.errors[0].find("not \"(+N)\"") != std::string::npos,
                       std::string("a malformed bonus '") + odd + "' shows the base and is reported");
            }
        }
        Expect(view["points"] == true && view["personal"]["rows"][0]["cells"][1]["arrows"] == true &&
                   view["personal"]["rows"][0]["cells"][1]["noLeft"] == true,
               "free points: ‹ › on the skills, ‹ only for points added in this visit");
        Expect(view["subject"]["sub"] == "1 <common.of> 2" && view["subject"]["prev"] == false &&
                   view["subject"]["next"] == true,
               "the captain and one officer in the switch");
    }

    void SkillHelp()
    {
        {
            // The stock game has RPGDescribe help for all three tables. Opening it is presentation-only: tapping a
            // skill must not select a different engine row, spend a free point or leak through to an underlying action.
            FakeSurface help_surface;
            Router help(help_surface);
            InterfaceModel with_help = model;
            // The recorded model's own descriptions are replaced by one fact per table, on its first row only.
            std::erase_if(with_help.widgets, [](const Widget &widget) { return widget.node == "ANDROID_SKILLHELP"; });
            const char *tables[] = {"TABLE_SPECIAL", "TABLE_SKILL_1", "TABLE_SKILL_2"};
            const char *ids[] = {"special", "personal", "ship"};
            for (const char *table : tables)
            {
                const Widget *row = Row(with_help, table, 0);
                Widget fact = Make("ANDROID_SKILLHELP", "fact", "");
                fact.row_id = std::string(table) + "_tr1";
                fact.context = {{"node", table},
                                {"skill", row->row_id},
                                {"text", "Game description.\r\n~\r\nEffects from RPGDescribe."}};
                with_help.widgets.push_back(std::move(fact));
            }
            help.SetInterface(with_help);
            for (int i = 0; i < 3; ++i)
            {
                help.OnEvent({ids[i], "row", "0"});
                Expect(help_surface.props["dialog"]["kind"] == "skill" &&
                           help_surface.props["dialog"]["title"] == Row(with_help, tables[i], 0)->CellText(1) &&
                           help_surface.props["dialog"]["text"] == "Game description.\n\nEffects from RPGDescribe." &&
                           help_surface.props["dialog"]["steps"].empty(),
                       std::string("a tap shows stock description for ") + tables[i] +
                           ", no steps without the adapter's");
                Expect(help.WantsBack(), "Back belongs to the skill help modal");
                help.Back();
                Expect(help_surface.props["dialog"].is_null(),
                       "Back closes skill help and retains the character screen");
            }
            help.OnEvent({"personal", "row", "1"}); // no game-provided text for this fixture's second row
            Expect(help_surface.props["dialog"].is_null() && help_surface.commands.empty() &&
                       help_surface.errors.size() == 1 &&
                       help_surface.errors[0].find("no description of TABLE_SKILL_1") != std::string::npos,
                   "missing skill text is reported, not invented; row taps send nothing");
            help.OnEvent({"personal", "row", "0"});
            help.OnEvent({"skill-help-close", "click", ""});
            Expect(help_surface.props["dialog"].is_null(), "the explicit close action closes skill help");

            // What a value is made of (android_hero.c AndroidSkillSteps): the base, the game's steps, the result. The
            // lines must add up to the row's own value and bonus columns; when they do not, none is shown and it is an
            // error.
            for (int i = 0; i < 3; ++i)
            {
                const bool special = i == 0;
                const Widget *row = Row(with_help, tables[i], 0);
                const int base = ParseAmount(row->CellText(special ? 3 : 4)).value();
                const int bonus = ParseAmount(row->CellText(special ? 2 : 3)).value_or(0);
                for (const bool broken : {false, true})
                {
                    FakeSurface steps_surface;
                    Router steps(steps_surface);
                    InterfaceModel with_steps = with_help;
                    for (Widget &fact : with_steps.widgets)
                        if (fact.node == "ANDROID_SKILLHELP" && fact.context["node"] == tables[i])
                            fact.context["base"] = std::to_string(base), fact.context["steps"] = "3";
                    // an amulet +5, a ship of too high a class (what is left of the bonus), the health line -5
                    const std::vector<std::map<std::string, std::string>> lines = {
                        {{"kind", "item"}, {"label", "Amulet"}, {"value", "5"}},
                        {{"kind", "navy"},
                         {"a1", "3"},
                         {"a2", "63"},
                         {"a3", "65"},
                         {"value", std::to_string(bonus + (broken ? 1 : 0))}},
                        {{"kind", "health"}, {"a1", "3"}, {"value", "-5"}}};
                    for (size_t n = 0; n < lines.size(); ++n)
                    {
                        Widget fact = Make("ANDROID_SKILLSTEPS", "fact", "");
                        fact.row_id = std::string(tables[i]) + "_tr1_" + std::to_string(n);
                        fact.context = {{"node", tables[i]}, {"skill", row->row_id}};
                        for (const auto &[key, value] : lines[n])
                            fact.context[key] = value;
                        with_steps.widgets.push_back(std::move(fact));
                    }
                    steps.SetInterface(with_steps);
                    steps.OnEvent({ids[i], "row", "0"});
                    const Json shown = steps_surface.props["dialog"]["steps"];
                    if (broken)
                    {
                        Expect(shown.empty() && steps_surface.errors.size() == 1 &&
                                   steps_surface.errors[0].find("do not add up") != std::string::npos,
                               std::string("steps that do not add up to the row are not shown and are an error: ") +
                                   tables[i]);
                        continue;
                    }
                    const std::string minus = "−";
                    Expect(shown.size() == 5 && shown[0]["k"] == "<hero.step_base>" &&
                               shown[0]["v"] == std::to_string(base) && shown[1]["k"] == "Amulet" &&
                               shown[1]["v"] == "+5" && shown[1]["tone"] == "ok" &&
                               shown[2]["k"] == "<hero.step_navy>" && shown[3]["k"] == "<hero.step_health>" &&
                               shown[3]["v"] == minus + "5" && shown[3]["tone"] == "danger" &&
                               shown[4]["k"] == "<hero.step_result>" && shown[4]["v"] == std::to_string(base + bonus) &&
                               steps_surface.errors.empty(),
                           std::string("base, the steps with their signs and the result: ") + tables[i] + " " +
                               shown.dump());
                }
            }
        }
    }

    void SubjectList()
    {
        // The subject list: a tap on the name lists the captain and the officers; a picked row is the same select as ›.
        Expect(view["subject"]["list"] == true && !view["subject"].contains("open"),
               "two subjects offer the list, closed");
        router.OnEvent({"header", "subject-list", ""});
        view = surface.props;
        Expect(view["subject"]["open"] == true && view["subject"]["title"] == "<hero.subjects>" &&
                   view["subject"]["rows"].size() == 2 && view["subject"]["rows"][0]["label"] == "Anne Bonny" &&
                   view["subject"]["rows"][0]["selected"] == true && view["subject"]["rows"][1]["selected"] == false,
               "the list of the captain and the officer on a post, the current one selected");
        Expect(router.WantsBack(), "back closes the subject list");
        router.Back();
        Expect(!surface.props["subject"].contains("open"), "back closed the list");
        router.OnEvent({"header", "subject-list", ""});
        const size_t sent = surface.commands.size();
        router.OnEvent({"header-subject-0", "", ""});
        Expect(surface.commands.size() == sent && !surface.props["subject"].contains("open"),
               "picking the current subject only closes the list");
        router.OnEvent({"header", "subject-list", ""});
        router.OnEvent({"header-subject-1", "", ""});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tCHARACTERS_SCROLL\t1\tselect@4:1:1",
                    "a picked officer is selected as with ›");
        Expect(!surface.props["subject"].contains("open"), "the pick closes the list");
        Expect(view["extras"]["rows"].empty(), "every widget is known: no extras");
    }

    void SkillArrow()
    {
        // A skill arrow: select the row, then the game's arrow once the engine shows the row selected.
        router.OnEvent({"personal", "inc", "0"});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_SKILL_1\t0\tselect@4:Leadership",
                    "› selects the skill row first");
        model.revision += 1;
        Row(model, "TABLE_SKILL_1", 0)->value = "1";
        router.SetInterface(model);
        ExpectEqual(surface.Last(), "widget:lscchar_all\tB_SKILLUP\t-1\tpress", "then presses the game's arrow");
        Expect(surface.props["personal"]["rows"][0]["cells"][1]["noLeft"] == false,
               "a point added in this visit may be taken back");
    }

    void Perks()
    {
        // Learning a perk: select, open its window, accept.
        surface.commands.clear();
        router.OnEvent({"header-tabs", "select", "perks1"});
        view = surface.props;
        const auto perk_nodes = [](const Json &v) { return v["tree"]["nodes"]; };
        Expect(surface.commands.empty() && view["tab"] == "perks" && view["tabs"][1]["selected"] == true &&
                   perk_nodes(view).size() == 2 && view["detail"]["title"] == "Basic defense" &&
                   view["detail"]["learn"]["disabled"] == false && view["detail"]["learn"]["label"] == "<hero.learn>" &&
                   view["perkInfo"] == "<hero.free_points> 1 · <hero.next_point> 340 / 1\xC2\xA0"
                                       "000" &&
                   view["detail"]["text"] == "Damage taken is 10% lower.\n\nStacks with armour.",
               "the game's open perk group: tree, detail, points, progress");
        Expect(perk_nodes(view)[0]["state"] == "open" && perk_nodes(view)[1]["state"] == "locked" &&
                   perk_nodes(view)[1]["image"]["enamel"] == "iron" && view["detail"]["opens"].size() == 1 &&
                   view["detail"]["opens"][0]["perk"] == "criticalhit",
               "a perk with a missing requirement is locked; the open one names what it opens");
        router.OnEvent({"perks", "row", "criticalhit"});
        Expect(surface.commands.empty() && surface.props["detail"]["learn"]["disabled"] == false &&
                   surface.props["detail"]["requires"].size() == 1 &&
                   surface.props["detail"]["requires"][0]["name"] == "Basic defense" &&
                   !surface.props["detail"].contains("blocked"),
               "the locked perk's detail names what it needs, in words; learning it is the game's to refuse");
        router.OnEvent({"perk-learn", "click", ""});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_PERKS\t1\tselect@4:criticalhit",
                    "the hold asks the game: the perk's row first");
        {
            auto refused = model;
            refused.revision += 1;
            Row(refused, "TABLE_PERKS", 0)->value = "0";
            Row(refused, "TABLE_PERKS", 1)->value = "1";
            Widget accept = Make("PERK_ACCEPT", "button", "Learn");
            accept.enabled = false; // ChoosePerk: SetSelectable("PERK_ACCEPT", false)
            refused.widgets.push_back(accept);
            refused.widgets.push_back(Make("PERK_CANCEL", "button", "Cancel", {"press"}));
            router.SetInterface(refused);
            ExpectEqual(surface.Last(), "widget:lscchar_all\tPERK_CANCEL\t-1\tpress",
                        "the game's window refuses it: closed, never accepted");
            Expect(surface.props["dialog"].is_null() && surface.props["detail"]["learn"]["disabled"] == true,
                   "a perk the game refused is not offered again");
            model.revision = refused.revision + 1;
            router.SetInterface(model);
            surface.commands.clear();
        }
        router.OnEvent({"perk-detail", "go", "basicdefense"});
        Expect(surface.commands.empty() && surface.props["detail"]["title"] == "Basic defense",
               "«Требует» selects that perk");
        Expect(surface.props["detail"]["learn"]["label"] == "<hero.learn>", "the button says just «Изучить»");
        router.OnEvent({"perk-learn", "click", ""}); // the completed hold
        ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_PERKS\t0\tactivate@4:basicdefense",
                    "the hold opens the selected perk's stock window at once");
        Expect(surface.props["dialog"].is_null(), "no card while the game learns");
        model.revision += 1;
        model.widgets.push_back(Make("PERK_ACCEPT", "button", "Learn", {"press"}));
        model.widgets.push_back(Make("PERK_CANCEL", "button", "Cancel", {"press"}));
        router.SetInterface(model);
        ExpectEqual(surface.Last(), "widget:lscchar_all\tPERK_ACCEPT\t-1\tpress", "and accepted in the game's window");
        Expect(router.WantsBack(), "while the game's window is open the back key closes it");
        model.widgets.resize(model.widgets.size() - 2);
        // The other perk group is its own tab: the game's group button is pressed, the tab shows chosen at once.
        model.revision += 1;
        router.SetInterface(model);
        router.OnEvent({"header-tabs", "select", "perks2"});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tTABBTN_2\t-1\tpress",
                    "the ship perks: the game's second group");
        Expect(surface.props["tabs"][1]["selected"] == true && surface.props["tabs"][2]["selected"] == false,
               "the tab marker follows the game's group, with the tree");
    }

    void Officers()
    {
        // Officers: a post opens the game's remove window; the dialog removes or closes.
        model.revision += 1;
        router.SetInterface(model);
        router.OnEvent({"header-tabs", "select", "officers"});
        view = surface.props;
        Expect(view["posts"].size() == 1 && view["freePosts"].size() == 1 && view["passengers"].size() == 1 &&
                   view["postsCount"] == "1 <common.of> 2",
               "posts, free posts, passengers");
        router.OnEvent({"post-1", "click", ""});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tCHARACTERS_SCROLL\t1\tselect@4:1:1",
                    "a post selects its slot");
        model.revision += 1;
        Row(model, "CHARACTERS_SCROLL", 0)->value = "0";
        Row(model, "CHARACTERS_SCROLL", 1)->value = "1";
        Row(model, "CHARACTERS_SCROLL", 1)->actions = {"select", "activate"};
        router.SetInterface(model);
        ExpectEqual(surface.Last(), "widget:lscchar_all\tCHARACTERS_SCROLL\t1\tactivate@4:1:1", "then activates it");
        model.revision += 1;
        model.widgets.push_back(Make("REMOVE_ACCEPT_OFFICER", "button", "Yes", {"press"}));
        model.widgets.push_back(Make("REMOVE_CANCEL_OFFICER", "button", "No", {"press"}));
        router.SetInterface(model);
        view = surface.props;
        Expect(view["dialog"]["kind"] == "officer" && view["dialog"]["title"] == "Jack Rackham" &&
                   view["dialog"]["facts"][0]["v"] == "34" &&
                   view["dialog"]["facts"][2]["v"] == "1\xC2\xA0"
                                                      "500 <hero.per_month>",
               "the officer dialog with the post's skill, loyalty and salary");
        router.OnEvent({"officer-remove", "click", ""});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tREMOVE_ACCEPT_OFFICER\t-1\tpress",
                    "«relieve» is the game's accept");
        model.revision += 1;
        router.SetInterface(model);
        router.Back();
        ExpectEqual(surface.Last(), "widget:lscchar_all\tREMOVE_CANCEL_OFFICER\t-1\tpress",
                    "back closes the game's window");
        model.widgets.resize(model.widgets.size() - 2);

        // The post chooser: a candidate is selected, then accepted.
        model.revision += 1;
        model.widgets.push_back(Make("ACCEPT_ADD_OFFICER", "button", "Accept", {"press"}));
        model.widgets.push_back(Make("CANCEL_ADD_OFFICER", "button", "Cancel", {"press"}));
        for (int i = 0; i < 2; ++i)
        {
            Widget row = Make("PASSENGERSLIST", "choice", i ? "Mary Read" : "Ned Low", {"select"});
            row.index = i;
            row.value = i ? "0" : "1";
            row.row_id = "3:" + std::to_string(i);
            model.widgets.push_back(row);
            Widget fact = Make("ANDROID_CANDIDATES", "fact", i ? "Mary Read" : "Ned Low");
            fact.index = i;
            fact.context = {{"index", std::to_string(i)}, {"character", i ? "12" : "11"}, {"rank", "6"},
                            {"skill", "Grappling"},       {"skillvalue", "41"},           {"face", "30"}};
            model.widgets.push_back(fact);
        }
        router.SetInterface(model);
        Expect(surface.props["dialog"]["kind"] == "assign" && surface.props["dialog"]["rows"].size() == 2 &&
                   surface.props["dialog"]["rows"][1]["value"] == "Grappling 41",
               "the post chooser lists the game's candidates with the post's skill");
        router.OnEvent({"candidate-1", "click", ""});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tPASSENGERSLIST\t1\tselect@4:3:1",
                    "a candidate is selected in the game's list");
        model.revision += 1;
        Row(model, "PASSENGERSLIST", 0)->value = "0";
        Row(model, "PASSENGERSLIST", 1)->value = "1";
        router.SetInterface(model);
        ExpectEqual(surface.Last(), "widget:lscchar_all\tACCEPT_ADD_OFFICER\t-1\tpress", "and accepted");
    }

    void Sections()
    {
        // Sections wait for the game's window to close.
        model.revision += 1;
        router.SetInterface(model);
        router.OnEvent({"header", "section", "ship"});
        ExpectEqual(surface.Last(), "widget:lscchar_all\tCANCEL_ADD_OFFICER\t-1\tpress",
                    "a section first closes the open window");
        model.widgets.erase(std::remove_if(model.widgets.begin(), model.widgets.end(),
                                           [](const Widget &w) {
                                               return w.node == "ACCEPT_ADD_OFFICER" ||
                                                      w.node == "CANCEL_ADD_OFFICER" || w.node == "PASSENGERSLIST" ||
                                                      w.node == "ANDROID_CANDIDATES";
                                           }),
                            model.widgets.end());
        model.revision += 1;
        router.SetInterface(model);
        ExpectEqual(surface.Last(), "widget:lscchar_all\tI_SHIP\t-1\tpress", "then opens the section");
    }

    void WithoutFacts()
    {
        // Without the adapter's facts the screen is not used, loudly.
        InterfaceModel bare = Load(models, "hero.json");
        bare.opening = 50;
        bare.widgets.erase(
            std::remove_if(bare.widgets.begin(), bare.widgets.end(), [](const Widget &w) { return w.type == "fact"; }),
            bare.widgets.end());
        const size_t errors = surface.errors.size();
        router.SetInterface(bare);
        Expect(router.Using() == "lscchar_all" && surface.errors.size() == errors + 1,
               "without the interface adapter's facts: drawn, and reported");
        bare.revision += 1;
        router.SetInterface(bare);
        Expect(surface.errors.size() == errors + 1, "reported once per opening (a closing interface loses its facts)");
    }
};

static void Hero(const std::string &models)
{
    HeroRun run{models};
    run.Opening();
    run.SkillHelp();
    run.SubjectList();
    run.SkillArrow();
    run.Perks();
    run.Officers();
    run.Sections();
    run.WithoutFacts();
}

// The header's switch lists the people of the game's character list (hero-people.json: an officer on two posts,
// a fighter, a companion, a passenger and a prisoner beside free posts), each once, by name, with every role.
static void HeroPeople(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "hero-people.json");
    router.SetInterface(model);
    Expect(surface.props["subject"]["label"] == "Anne Bonny" && surface.props["subject"]["sub"] == "1 <common.of> 7",
           "the captain first, seven people: " + surface.props["subject"].dump());
    router.OnEvent({"header", "subject-list", ""});
    const Json rows = surface.props["subject"]["rows"];
    std::vector<std::string> names, metas;
    for (const Json &row : rows)
    {
        names.push_back(row["label"]);
        metas.push_back(row["meta"]);
    }
    Expect(names == std::vector<std::string>{"Anne Bonny", "Jack Rackham", "Mary Read", "Israel Hands", "Edward Teach",
                                             "Bartholomew de la Fontaine-Beaumarchais", "Charles Vane"},
           "every person once, in the game's order, by name");
    Expect(metas == std::vector<std::string>{"Captain", "Navigator", "Cannoner\xC2\xA0· doctor", "Fighter", "Companion",
                                             "Passenger", "Prisoner"},
           "with every role they hold, in sentence case");
    Expect(rows[2]["image"]["texture"] == "INTERFACES/PORTRAITS/128/face_30", "with the person's portrait");
    const auto shows = [&](int slot, const char *character) {
        for (Widget &widget : model.widgets)
            if (widget.node == "CHARACTERS_SCROLL")
                widget.value = widget.index == slot ? "1" : "0";
            else if (widget.node == "ANDROID_SUBJECT" && character)
                widget.context["character"] = character;
        model.revision += 1;
        router.SetInterface(model);
    };
    router.OnEvent({"header-subject-2", "click", ""});
    Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t3\tselect@", 0) == 0,
           "an officer on two posts is selected by the first: " + surface.Last());
    Expect(surface.props["subject"]["label"] == "Mary Read" && surface.props["subject"]["sub"] == "3 <common.of> 7" &&
               !surface.props["subject"].contains("open"),
           "the pick is drawn in the tap's own frame: " + surface.props["subject"].dump());
    // The game answers in two frames: its list selects her slot at once, the person follows a frame later.
    AnswerAll(router, surface, true);
    shows(3, nullptr);
    Expect(surface.props["subject"]["label"] == "Mary Read" && surface.props["card"]["roles"] == "Captain",
           "the step keeps the pick, the card the person the game still shows: " + surface.props["subject"].dump());
    shows(3, "11");
    Expect(surface.props["subject"]["label"] == "Mary Read" && surface.props["subject"]["sub"] == "3 <common.of> 7",
           "the game shows her: " + surface.props["subject"].dump());
    Expect(surface.props["card"]["sub"] == "Rank 4" && surface.props["card"]["roles"] == "Cannoner\xC2\xA0· doctor",
           "her card names both posts on a line of their own under the rank: " + surface.props["card"].dump());
    Expect(surface.errors.empty(), "the game confirmed the pick: " + (surface.errors.empty() ? "" : surface.errors[0]));
    router.OnEvent({"header", "subject-next", ""});
    Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t7\tselect@", 0) == 0,
           "› goes to the next person, not to her other post: " + surface.Last());
    Expect(surface.props["subject"]["label"] == "Israel Hands", "› is drawn in the tap's own frame");
    AnswerAll(router, surface, true);
    shows(7, "12");
    router.OnEvent({"header", "subject-list", ""});
    router.OnEvent({"header-subject-6", "click", ""});
    Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t12\tselect@", 0) == 0,
           "a prisoner is selected by his slot: " + surface.Last());
    AnswerAll(router, surface, true);
    shows(12, "22");
    router.OnEvent({"header", "subject-prev", ""});
    Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t11\tselect@", 0) == 0,
           "‹ goes to the person before him: " + surface.Last());
    AnswerAll(router, surface, true);
    shows(11, "21");
    // The game shows her by her second post (a HUD portrait opened the screen on it): the switch shows her.
    shows(4, "11");
    Expect(surface.props["subject"]["label"] == "Mary Read" && surface.props["subject"]["sub"] == "3 <common.of> 7",
           "her second post shows her: " + surface.props["subject"].dump());
    Expect(surface.errors.empty(),
           "the game confirmed every pick: " + (surface.errors.empty() ? "" : surface.errors[0]));
    // A selection the player did not make (the HUD's subject, the game's own): the header waits for the person with the
    // card, as nothing was drawn at a tap.
    shows(7, nullptr);
    Expect(surface.props["subject"]["label"] == "Mary Read" &&
               surface.props["card"]["roles"] == "Cannoner\xC2\xA0· doctor",
           "the game's step does not move the header: " + surface.props["subject"].dump());
    shows(7, "12");
    Expect(surface.props["subject"]["label"] == "Israel Hands", "the game shows him");

    // A free post selected in the game's list: the game shows the captain.
    shows(2, "1");
    Expect(surface.props["subject"]["label"] == "Anne Bonny", "a free post shows the captain");
    // A subject the list does not hold is the adapter's fault: said, never shown as somebody else in silence.
    shows(2, "99");
    Expect(!surface.errors.empty() && surface.errors.back().find("character 99") != std::string::npos,
           "a subject outside the list is an error");
    shows(2, "1");
    surface.errors.clear();

    // The officers tab stays about posts: both of her posts, no companion, passenger or prisoner among them.
    router.OnEvent({"header-tabs", "select", "officers"});
    Expect(surface.props["posts"].size() == 4 && surface.props["postsCount"] == "4 <common.of> 9" &&
               surface.props["freePosts"].size() == 5 && surface.props["passengers"].size() == 1,
           "posts by post: " + surface.props["postsCount"].dump());
    ExpectEqual(surface.props["posts"][0]["meta"], "Navigator", "a post's role on a line of its own, in sentence case");
    Expect(surface.errors.empty(), "no errors: " + (surface.errors.empty() ? "" : surface.errors.front()));

    // German writes its nouns capitalised, in the list, on the card and on the posts as anywhere.
    FakeSurface german_surface;
    Router german(german_surface);
    german.SetInterface(Load(models, "hero-people-de.json"));
    german.OnEvent({"header", "subject-list", ""});
    ExpectEqual(german_surface.props["subject"]["rows"][2]["meta"],
                "Geschützoffizier\xC2\xA0· Proviantmeister\xC2\xA0· Schiffszimmermann", "German roles in the list");
    ExpectEqual(german_surface.props["card"]["roles"],
                "Geschützoffizier\xC2\xA0· Proviantmeister\xC2\xA0· Schiffszimmermann", "German roles on the card");
    german.OnEvent({"header-tabs", "select", "officers"});
    ExpectEqual(german_surface.props["posts"][0]["meta"], "Navigationsoffizier", "a German post");
}

// The items screen on one router: the list and its details, the actions, slots, an unavailable item, the map.
struct ItemsRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    InterfaceModel model = Load(models, "items.json");
    Json view; // what the screen showed last

    void Opening()
    {
        router.SetInterface(model);
        ExpectEqual(router.Using(), "items", "the items draw on the items screen");
        ExpectEqual(surface.tag, "s-items", "s-items mounted");
        view = surface.props;
        Expect(view["slots"].size() == 10 && view["slots"][0]["label"] == "<items.slot_blade>" &&
                   view["slots"][0]["name"] == "Rapier" &&
                   view["slots"][0]["image"]["texture"] == "atlas:ITEMS_1/itm1" && view["slots"][1]["empty"] == true,
               "ten slots: the slot's caption under what the hero wears, the empty ones dashed");
        {
            FakeSurface two_surface;
            Router two(two_surface);
            auto fewer = model;
            std::erase_if(fewer.widgets,
                          [](const Widget &w) { return w.node == "ANDROID_SLOTS" && w.row_id == "slot3"; });
            two.SetInterface(fewer);
            Expect(two_surface.props["slots"].size() == 9 && two_surface.props["slots"][8]["id"] == "slot2",
                   "the artefact slots are the character's own, as the adapter lists them");
        }
        Expect(view["rows"].size() == 3 && !view.contains("columns") &&
                   view["rows"][0]["cells"][0]["badge"] == "check" &&
                   view["rows"][2]["cells"][1] == "\xC3\x97"
                                                  "3",
               "the list: worn mark, a count only above one");
        Expect(view["stats"][0]["value"] == "4.6 / 140" && view["subject"]["label"] == "Anne Bonny" &&
                   view["tabs"].size() == 4,
               "weight in the header, the officer switch, the game's tabs");
        Expect(view["detail"]["title"] == "Rapier" && view["detail"]["text"] == "A light blade." &&
                   view["detail"]["action"]["label"] == "Take off" && view["detail"]["action"]["disabled"] == false,
               "the selected item with the game's own action");
        {
            // UI31-01: the real Android model exports itemid (the shared script codec's spelling), not itemId as in
            // this fixture. Selecting the occupied blade slot must retain its selected worn item's details and action.
            for (const std::string spelling : {"itemid", "ITEMID"})
            {
                FakeSurface occupied_surface;
                Router occupied(occupied_surface);
                InterfaceModel live_spelling = model;
                for (Widget &widget : live_spelling.widgets)
                    if (auto identity = widget.context.extract("itemId"); !identity.empty())
                    {
                        identity.key() = spelling;
                        widget.context.insert(std::move(identity));
                    }
                occupied.SetInterface(live_spelling);
                occupied.OnEvent({"slot-blade", "click", ""});
                const Json &shown = occupied_surface.props;
                Expect(shown["slots"][0]["selected"] == true && !shown["detail"].is_null() &&
                           shown["detail"]["title"] == "Rapier" && shown["detail"]["text"] == "A light blade." &&
                           shown["detail"]["action"]["label"] == "Take off" && !shown["detail"]["action"]["disabled"],
                       "UI31-01: occupied slot preserves details/actions with script attribute casing " + spelling);
            }
        }
        {
            // The description in parts: the worn rapier's own figures (nothing to compare with), its kind, then the
            // text.
            const Json &facts = view["detail"]["facts"];
            Expect(facts.size() == 4 && facts[0] == Json({{"k", "<items.attack>"}, {"v", "60.0"}}) &&
                       facts[2]["k"] == "<items.weight>" && facts[3] == Json({{"k", "<items.kind>"}, {"v", "Rapiers"}}),
                   "the worn weapon: attack, balance, weight, kind: " + facts.dump());
            // The dagger against the worn rapier: weaker (red), lighter (green); the balance has no better side.
            FakeSurface dagger_surface;
            Router dagger(dagger_surface);
            InterfaceModel selected = model;
            for (Widget &widget : selected.widgets)
            {
                if (widget.node == "TABLE_ITEMS")
                    widget.value = widget.index == 1 ? "1" : "0";
                if (widget.node == "ANDROID_INFO")
                    widget.context = {
                        {"row", "tr2"}, {"text", "A short blade."}, {"kind", "Rapiers"}, {"weight", "1.5"}};
            }
            dagger.SetInterface(selected);
            const Json &against = dagger_surface.props["detail"]["facts"];
            Expect(against[0] == Json({{"k", "<items.attack>"},
                                       {"v", "34.5"},
                                       {"d", "\xE2\x88\x92"
                                             "25.5"},
                                       {"dTone", "danger"}}) &&
                       against[1] == Json({{"k", "<items.balance>"}, {"v", "2.0"}, {"d", "+0.6"}}) &&
                       against[2] == Json({{"k", "<items.weight>"},
                                           {"v", "1.5"},
                                           {"d", "\xE2\x88\x92"
                                                 "0.9"},
                                           {"dTone", "ok"}}) &&
                       dagger_surface.props["detail"]["text"] == "A short blade.",
                   "an item against the worn one of its slot: " + against.dump());
            // A row the adapter did not describe keeps the game's own text.
            for (Widget &widget : selected.widgets)
                if (widget.node == "ANDROID_INFO")
                    widget.context["row"] = "tr9";
            selected.revision += 1;
            dagger.SetInterface(selected);
            Expect(dagger_surface.props["detail"]["text"] == "A light blade.",
                   "no description from the adapter: the game's INFO_TEXT");
        }
        Expect(view["extras"]["rows"].empty(), "every widget is known: no extras");
        {
            // An item worn nowhere (the potion: no slot) has no slot line and is no error.
            FakeSurface potion_surface;
            Router potion(potion_surface);
            InterfaceModel selected = model;
            for (Widget &widget : selected.widgets)
                if (widget.node == "TABLE_ITEMS")
                    widget.value = widget.index == 2 ? "1" : "0";
            potion.SetInterface(selected);
            Expect(potion_surface.props["detail"]["title"] == "Potion" && potion_surface.props["detail"]["sub"] == "" &&
                       potion_surface.errors.empty(),
                   "an item without a slot: no slot line, no missing-string error");
        }
    }

    void Actions()
    {
        router.OnEvent({"item-equip", "click", ""});
        ExpectEqual(surface.Last(), "widget:items\tEQUIP_BUTTON\t-1\tpress@5:inventory:7:character:1:role:0",
                    "the action is the game's guarded press");
        router.OnEvent({"items", "row", "2"});
        ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t2\tselect@5:inventory:30:character:1:role:0",
                    "a row is selected");
    }

    void Slots()
    {
        // A slot filters the list; selecting its row shows details without putting it on.
        router.OnEvent({"slot-blade", "click", ""});
        Expect(surface.props["rows"].size() == 2 && surface.props["slots"][0]["selected"] == true,
               "the blade slot lists the blades");
        router.OnEvent({"items", "row", "1"});
        ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t1\tselect@5:inventory:9:character:1:role:0",
                    "the dagger is selected");
        const size_t before_selected_reply = surface.commands.size();
        model.revision += 1;
        for (Widget &widget : model.widgets)
            if (widget.node == "TABLE_ITEMS")
                widget.value = widget.index == 1 ? "1" : "0";
            else if (widget.node == "EQUIP_BUTTON")
                widget.row_id = "inventory:9:character:1:role:0", widget.label = "Equip";
        router.SetInterface(model);
        Expect(surface.commands.size() == before_selected_reply && surface.props["detail"]["title"] == "Dagger",
               "filtered selection shows details and never implicitly equips");
        router.OnEvent({"items", "row", "1"});
        Expect(surface.commands.size() == before_selected_reply, "reselecting the row does not equip either");
        router.OnEvent({"item-equip", "click", ""});
        ExpectEqual(surface.Last(), "widget:items\tEQUIP_BUTTON\t-1\tpress@5:inventory:9:character:1:role:0",
                    "only the explicit action puts it on");

        router.OnEvent({"item-tabs", "select", "3"});
        ExpectEqual(surface.Last(), "widget:items\tTABBTN_3\t-1\tpress", "a game tab");
        Expect(surface.props["slots"][0]["selected"] == false && surface.props["rows"].size() == 3,
               "a game category clears the previous blade-slot filter while its response is pending");
        router.OnEvent({"slot-blade", "click", ""});
        Expect(surface.props["slots"][0]["selected"] == true && surface.props["rows"].size() == 2, "the blades");
        router.OnEvent({"header", "subject-next", ""});
        ExpectEqual(surface.Last(), "widget:items\tCHARACTERS_SCROLL\t1\tselect@5:1:1", "the next officer's items");
        Expect(surface.props["subject"]["label"] == "Jack Rackham", "his name in the tap's own frame");
        // The game selects his slot at once and shows his items a game frame later: the step keeps his name and the
        // captain's items, the slot filter goes with the captain's items.
        AnswerAll(router, surface, true);
        model.revision += 1;
        for (Widget &widget : model.widgets)
            if (widget.node == "CHARACTERS_SCROLL")
                widget.value = widget.index == 1 ? "1" : "0";
        router.SetInterface(model);
        Expect(surface.props["subject"]["label"] == "Jack Rackham" && surface.props["rows"].size() == 2 &&
                   surface.props["slots"][0]["selected"] == true,
               "the step: his name, the captain's blades");
        model.revision += 1;
        for (Widget &widget : model.widgets)
            if (widget.node == "ANDROID_SUBJECT")
                widget.context["character"] = "9", widget.label = "Jack Rackham";
        router.SetInterface(model);
        // (Earlier taps of this run whose answers the run never modelled are corrected here too: only the pick counts.)
        Expect(surface.props["subject"]["label"] == "Jack Rackham" && surface.props["slots"][0]["selected"] == false &&
                   surface.props["rows"].size() == 3 &&
                   std::none_of(surface.errors.begin(), surface.errors.end(),
                                [](const std::string &e) { return e.find(" header ") != std::string::npos; }),
               "the game shows him, as predicted");
        router.OnEvent({"header", "subject-list", ""});
        Expect(surface.props["subject"]["open"] == true && surface.props["subject"]["rows"].size() == 2,
               "the items screen lists the subjects too");
        router.OnEvent({"header-subjects", "dismiss", ""});
        Expect(!surface.props["subject"].contains("open"), "a tap beside the list closes it");
    }

    void Unavailable()
    {
        // A selected but unavailable item is an answer from ThisItemCanBeEquip, not an unanswered step.
        {
            FakeSurface refused_surface;
            Router refused(refused_surface);
            InterfaceModel denied = Load(models, "items.json");
            refused.SetInterface(denied);
            refused.OnEvent({"slot-blade", "click", ""});
            refused.OnEvent({"items", "row", "1"});
            const size_t sent = refused_surface.commands.size();
            for (Widget &widget : denied.widgets)
                if (widget.node == "TABLE_ITEMS")
                    widget.value = widget.index == 1 ? "1" : "0";
                else if (widget.node == "EQUIP_BUTTON")
                {
                    widget.enabled = false;
                    widget.actions.clear();
                    widget.row_id.clear(); // InventoryEquipCapability publishes no token for a disabled button.
                    widget.unavailable_reason = "Needs a weapon perk";
                }
            denied.revision += 1;
            refused.SetInterface(denied);
            Expect(refused_surface.commands.size() == sent &&
                       refused_surface.props["detail"]["action"]["disabled"] == true,
                   "a refused equip leaves the selected item and its unavailable action visible");
            Expect(refused_surface.toasts.empty() &&
                       refused_surface.props["detail"]["action"]["reason"] == "Needs a weapon perk",
                   "a description-only selection keeps the game's refusal");
            refused_surface.now = 4.0;
            denied.revision += 1; // another publication (for example the description or weight).
            refused.SetInterface(denied);
            refused.Tick();
            Expect(refused_surface.errors.empty() && refused_surface.commands.size() == sent,
                   "a disabled equip answer completes the intent");
        }
        {
            FakeSurface waiting_surface;
            Router waiting(waiting_surface);
            waiting.SetInterface(Load(models, "items.json"));
            waiting.OnEvent({"slot-blade", "click", ""});
            waiting.OnEvent({"items", "row", "1"});
            waiting_surface.now = 4.0;
            waiting.Tick();
            Expect(waiting_surface.errors.size() == 1 &&
                       waiting_surface.errors[0].find("the engine did not answer a step") != std::string::npos,
                   "an actual missing selection response still reports the timeout");
        }
    }

    void MapWindow()
    {
        // The game's map window.
        model.revision += 1;
        model.widgets.push_back(Make("MAP_CAPTION", "text", "Treasure map"));
        model.widgets.push_back(Make("MAP_TEXT", "text", "A cross on the south shore."));
        model.widgets.push_back(Make("MAP_EXIT_BTN", "button", "", {"press"}));
        Widget picture = Make("MAP_PICTURE", "image");
        picture.images.push_back(ImageRef{});
        picture.images.back().kind = "texture";
        picture.images.back().image = "interfaces/maps/treasure";
        model.widgets.push_back(picture);
        router.SetInterface(model);
        Expect(surface.props["map"]["title"] == "Treasure map" &&
                   surface.props["map"]["image"]["texture"] == "interfaces/maps/treasure",
               "the map window: the game's picture by its texture name");
        Expect(router.WantsBack(), "back closes the map first");
        router.Back();
        ExpectEqual(surface.Last(), "widget:items\tMAP_EXIT_BTN\t-1\tpress", "back is the map's close");
    }
};

static void Items(const std::string &models)
{
    ItemsRun run{models};
    run.Opening();
    run.Actions();
    run.Slots();
    run.Unavailable();
    run.MapWindow();
}

// Every kind of item gets the action the game's EQUIP_BUTTON has for it (android_hero.c AndroidItemAction, items.c
// EquipPress), and the one that destroys an item (taking off a timed artefact) is never a plain tap.
static void ItemActionCases(const std::string &models)
{
    struct Case
    {
        const char *what;
        std::map<std::string, std::string> fact; // the selected row's adapter fact (tr1)
        const char *game_label;                  // EQUIP_BUTTON's label
        bool ready;                              // the game lets the button be pressed
        Json action;                             // the expected detail action fields (null: no action)
        bool error = false;                      // the screen reports the row
        const char *reason = "";                 // an explicit reason from the game has priority
    };
    const Json none = nullptr;
    const std::vector<Case> cases = {
        {"a worn weapon: the game's own words",
         {{"slot", "blade"}, {"worn", "1"}, {"action", "equip"}},
         "Take off",
         true,
         {{"label", "Take off"},
          {"variant", "primary"},
          {"hold", false},
          {"disabled", false},
          {"reason", "<items.unavailable>"}}},
        {"the stock game forbids removing a worn blade: its button off, no reason of ours",
         {{"slot", "blade"}, {"worn", "1"}, {"action", "equip"}},
         "Take off",
         false,
         {{"label", "Take off"}, {"disabled", true}, {"reason", "<items.unavailable>"}}},
        {"a supplied blade refusal takes precedence",
         {{"slot", "blade"}, {"worn", "1"}, {"action", "equip"}},
         "Take off",
         false,
         {{"disabled", true}, {"reason", "The character keeps this blade"}},
         false,
         "The character keeps this blade"},
        {"an unworn blade refusal is not a removal restriction",
         {{"slot", "blade"}, {"worn", "0"}, {"action", "equip"}},
         "Equip",
         false,
         {{"disabled", true}, {"reason", "<items.unavailable>"}}},
        {"another worn weapon has its own restrictions",
         {{"slot", "gun"}, {"worn", "1"}, {"action", "equip"}},
         "Take off",
         false,
         {{"disabled", true}, {"reason", "<items.unavailable>"}}},
        {"a weapon in the bag",
         {{"slot", "blade"}, {"worn", "0"}, {"action", "equip"}},
         "Equip",
         true,
         {{"label", "Equip"}, {"variant", "primary"}, {"hold", false}, {"disabled", false}}},
        {"a worn timed artefact: taking it off uses it up, held to confirm",
         {{"slot", "amulet"}, {"worn", "1"}, {"action", "amulet"}, {"days", "70"}, {"remain", "64"}},
         "Delete",
         true,
         {{"label", "<items.take_off>"}, {"variant", "danger"}, {"hold", true}, {"disabled", false}}},
        {"a worn artefact without a term stays in the bag: taken off",
         {{"slot", "amulet"}, {"worn", "1"}, {"action", "amulet"}},
         "Delete",
         true,
         {{"label", "<items.take_off>"}, {"variant", "primary"}, {"hold", false}, {"disabled", false}}},
        {"an artefact in the bag goes on",
         {{"slot", "amulet"}, {"worn", "0"}, {"action", "amulet"}, {"days", "70"}},
         "Equip",
         true,
         {{"label", "Equip"}, {"variant", "primary"}, {"hold", false}, {"disabled", false}}},
        {"an artefact the game will not put on",
         {{"slot", "amulet"}, {"worn", "0"}, {"action", "amulet"}, {"days", "70"}},
         "Equip",
         false,
         {{"label", "Equip"}, {"disabled", true}, {"reason", "<items.unavailable>"}}},
        {"a potion, a jewel: the game has no action, none is shown",
         {{"slot", ""}, {"worn", "0"}, {"action", ""}},
         "Equip",
         false,
         none},
        {"a quest letter is read (quest items keep the game's action)",
         {{"slot", ""}, {"worn", "0"}, {"quest", "1"}, {"action", "use"}},
         "Equip",
         true,
         {{"label", "<items.use>"}, {"variant", "primary"}, {"hold", false}, {"disabled", false}}},
        {"a quest item without a use has no action at all",
         {{"slot", ""}, {"worn", "0"}, {"quest", "1"}, {"action", ""}},
         "Equip",
         false,
         none},
        {"a map opens",
         {{"slot", ""}, {"worn", "0"}, {"map", "1"}, {"action", "map"}},
         "Equip",
         true,
         {{"label", "<items.open_map>"}, {"variant", "primary"}}},
        {"ammunition is loaded",
         {{"slot", ""}, {"worn", "0"}, {"action", "ammo"}},
         "Equip",
         true,
         {{"label", "<items.load>"}, {"variant", "primary"}}},
        {"an action the screen does not know is reported, not guessed",
         {{"slot", ""}, {"worn", "0"}, {"action", "drop"}},
         "Equip",
         true,
         none,
         true},
    };
    for (const Case &c : cases)
    {
        FakeSurface surface;
        Router router(surface);
        InterfaceModel model = Load(models, "items.json");
        for (Widget &widget : model.widgets)
        {
            if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr1")
            {
                widget.context.clear();
                for (const auto &[key, value] : c.fact)
                    widget.context[key] = value;
            }
            if (widget.node == "EQUIP_BUTTON")
            {
                widget.label = c.game_label;
                widget.unavailable_reason = c.reason;
                widget.enabled = c.ready;
                if (!c.ready)
                    widget.actions.clear(), widget.row_id.clear();
            }
        }
        router.SetInterface(model);
        const Json &action = surface.props["detail"]["action"];
        Expect(c.error == !surface.errors.empty(),
               std::string(c.what) + ": errors " + std::to_string(surface.errors.size()));
        if (c.action.is_null())
        {
            Expect(action.is_null(), std::string(c.what) + ": no action, got " + action.dump());
            continue;
        }
        bool fields = action.is_object();
        for (const auto &[key, value] : c.action.items())
            fields = fields && action.contains(key) && action[key] == value;
        Expect(fields, std::string(c.what) + ": " + action.dump());
        if (c.fact.count("action") && c.fact.at("action") == "amulet")
            Expect(!action.contains("sub"), "amulet action has no extra consumption caption");
        if (c.fact.count("action") && c.fact.at("action") == "amulet" && !c.fact.count("days"))
        {
            bool has_term = false;
            for (const Json &fact : surface.props["detail"]["facts"])
                has_term = has_term || fact.value("k", "") == "<items.term>";
            Expect(!has_term && !action.contains("sub"),
                   "infinite amulet has no invented countdown or consumption warning");
        }
        // Nothing that destroys an item is a plain tap.
        Expect(action.value("variant", "") != "danger" || action.value("hold", false) == true,
               std::string(c.what) + ": a destroying action is held");
    }
}

// A selected row the adapter did not describe: reported, and no guessed action.
static void UndescribedRow(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    std::erase_if(model.widgets,
                  [](const Widget &widget) { return widget.node == "ANDROID_ITEMS" && widget.row_id == "tr1"; });
    router.SetInterface(model);
    Expect(surface.props["detail"].is_object() && surface.props["detail"]["action"].is_null(),
           "a row without its adapter fact shows no action: " + surface.props["detail"].dump());
    Expect(surface.errors.size() == 1 && surface.errors[0].find("items: no adapter row for '") != std::string::npos,
           "a row without its adapter fact is an error, not a guessed equip: " + Json(surface.errors).dump());
}

// The worn timed artefact: its term in the facts, the days it has left as its slot's caption.
static void TimedArtefact(const std::string &models)
{
    FakeSurface surface;
    surface.texts = {{"items.term", "Срок"}, {"items.days", "{1} дн."}, {"items.days_left", "{1} из {2} дн."}};
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    for (Widget &widget : model.widgets)
        if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr1")
            widget.context = {
                {"slot", "amulet"}, {"worn", "1"}, {"action", "amulet"}, {"days", "70"}, {"remain", "64"}};
    for (Widget &widget : model.widgets)
        if (widget.node == "ANDROID_SLOTS" && widget.row_id == "slot1")
            widget.context = {{"artefact", "1"}, {"label", "Voodoo doll"}, {"group", "ITEMS_3"},      {"image", "itm1"},
                              {"remain", "64"},  {"time", "70"},           {"itemId", "fixture-doll"}};
    for (const char *node : {"SLOT1_TEXT", "SLOT2_TEXT", "SLOT3_TEXT"})
        model.widgets.push_back(Make(node, "text", "64/70"));
    router.SetInterface(model);
    Expect(surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
           "stock counters never become an extras note");
    bool term = false;
    for (const Json &fact : surface.props["detail"]["facts"])
        term = term || fact == Json({{"k", "Срок"}, {"v", "64 из 70 дн."}});
    Expect(term, "the worn artefact's term: " + surface.props["detail"]["facts"].dump());
    const Json &tile = surface.props["slots"][7];
    Expect(tile["id"] == "slot1" && tile["label"] == "64 дн." && !tile.contains("sub"),
           "the artefact's slot: its days as the caption: " + tile.dump());
}

// A chosen slot owns the parchment: an empty slot does not show (and offer the action of) the item
// selected before; a slot that holds an item selects that item's row.
static void SlotOwnsParchment(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json"); // the worn rapier is selected
    router.SetInterface(model);
    router.OnEvent({"slot-gun", "click", ""});
    Expect(surface.props["slots"][1]["selected"] == true && surface.props["detail"].is_null(),
           "the empty gun slot: no rapier on the parchment: " + surface.props["detail"].dump());
    router.OnEvent({"slot-gun", "click", ""});
    Expect(surface.props["detail"]["title"] == "Rapier", "the slot let go: the game's selection again");
    for (Widget &widget : model.widgets)
        if (widget.node == "TABLE_ITEMS")
            widget.value = widget.index == 2 ? "1" : "0"; // the potion
    model.revision += 1;
    router.SetInterface(model);
    router.OnEvent({"slot-blade", "click", ""});
    ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t0\tselect@5:inventory:7:character:1:role:0",
                "the blade slot selects the rapier it holds");
    Expect(surface.props["detail"].is_null(), "until selection returns no stale potion action is exposed");
}

// Three amulet slots share the list category, but an empty slot must not expose another slot's
// worn timed amulet and its destructive action. An absent detail also removes its event binding.
static void AmuletSlots(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    for (Widget &widget : model.widgets)
        if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr1")
            widget.context = {{"slot", "amulet"},
                              {"worn", "1"},
                              {"action", "amulet"},
                              {"days", "70"},
                              {"itemId", "fixture-item-tr1"}};
    Widget slot = Make("ANDROID_SLOTS", "fact", "Rapier");
    slot.row_id = "slot1";
    slot.context["itemId"] = "fixture-item-tr1";
    model.widgets.push_back(slot);
    router.SetInterface(model);
    Expect(surface.props["detail"]["action"]["hold"] == true, "timed amulet starts held to confirm");
    router.OnEvent({"slot-slot3", "click", ""});
    Expect(surface.props["slots"][9]["selected"] == true && surface.props["detail"].is_null(),
           "empty third amulet slot hides the first slot's destructive action");
    Expect(surface.props["rows"].empty(), "another slot's worn amulet is not offered for an empty slot");
    const auto before = surface.Last();
    router.OnEvent({"item-equip", "click", ""});
    ExpectEqual(surface.Last(), before, "an old action after the empty-slot tap sends no command");
    router.OnEvent({"slot-slot1", "click", ""});
    Expect(surface.props["detail"]["title"] == "Rapier" && surface.props["detail"]["action"]["hold"] == true,
           "the occupied amulet slot exposes only its own held action");
}

// Slot names are script attribute names too: their first interned case is not a different equipment slot.
// All three occupied slots must retain only their own item and its guarded removal, never a neighbour's.
static void SlotNamesFoldCase(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    const char *slot_names[] = {"SLOT1", "Slot2", "slot3"};
    for (Widget &widget : model.widgets)
    {
        if (widget.node == "ANDROID_ITEMS")
            widget.context = {{"slot", "amulet"},
                              {"worn", "1"},
                              {"action", "amulet"},
                              {"days", "70"},
                              {"itemid", "amulet-" + std::to_string(widget.index)}};
        if (widget.node == "TABLE_ITEMS")
            widget.value = widget.index == 1 ? "1" : "0";
        if (widget.node == "EQUIP_BUTTON")
            widget.row_id = "inventory:9:character:1:role:0";
    }
    for (int i = 0; i < 3; ++i)
    {
        Widget slot = Make("ANDROID_SLOTS", "fact");
        slot.label = "Amulet " + std::to_string(i);
        slot.row_id = slot_names[i];
        slot.context["itemid"] = "amulet-" + std::to_string(i);
        model.widgets.push_back(std::move(slot));
    }
    router.SetInterface(model);
    router.OnEvent({"slot-slot2", "click", ""});
    Expect(surface.props["slots"][7]["name"] == "Amulet 0" && surface.props["slots"][8]["name"] == "Amulet 1" &&
               surface.props["slots"][9]["name"] == "Amulet 2" && surface.props["rows"].size() == 1 &&
               !surface.props["detail"].is_null() &&
               surface.props["detail"]["title"] == Row(model, "TABLE_ITEMS", 1)->CellText(0) &&
               surface.props["detail"]["action"]["hold"] == true,
           "mixed-case occupied amulet slots retain the correct selected removal action");
    router.OnEvent({"item-equip", "click", ""});
    ExpectEqual(surface.Last(), "widget:items\tEQUIP_BUTTON\t-1\tpress@5:inventory:9:character:1:role:0",
                "the second amulet slot removes only the second slot's selected item");
}

static void SameDisplayNames(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    for (Widget &widget : model.widgets)
    {
        if (widget.node == "TABLE_ITEMS")
        {
            widget.cells[0].text = "Same display name";
            widget.value = widget.index == 2 ? "1" : "0";
        }
        if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr2")
            widget.context["worn"] = "1";
        if (widget.node == "ANDROID_SLOTS")
            widget.label = "Same display name", widget.context["itemId"] = "fixture-item-tr2";
    }
    router.SetInterface(model);
    router.OnEvent({"slot-blade", "click", ""});
    ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t1\tselect@5:inventory:9:character:1:role:0",
                "identical display names select the slot's stable item ID, not the first worn row");
}

static void OtherRowsAction(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    for (Widget &widget : model.widgets)
        if (widget.node == "EQUIP_BUTTON")
            widget.row_id = "inventory:9:character:1:role:0";
    router.SetInterface(model);
    Expect(surface.props["detail"]["action"]["disabled"] == true, "another row's game action is disabled");
    const auto before = surface.Last();
    router.OnEvent({"item-equip", "click", ""});
    ExpectEqual(surface.Last(), before, "even a queued click cannot execute another row's game action");
}

static void SlotFilters(const std::string &models)
{
    struct Selection
    {
        const char *slot;
        const char *action;
        bool filtered;
    };
    for (const Selection &test : {Selection{"blade", "equip", true},
                                  {"cirass", "equip", true},
                                  {"gun", "equip", true},
                                  {"", "use", false},
                                  {"", "map", false}})
    {
        FakeSurface surface;
        Router router(surface);
        InterfaceModel model = Load(models, "items.json");
        for (Widget &widget : model.widgets)
            if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr2")
                widget.context = {{"slot", test.slot}, {"action", test.action}, {"worn", "0"}, {"quest", "1"}};
        router.SetInterface(model);
        if (test.filtered)
            router.OnEvent({std::string("slot-") + test.slot, "click", ""});
        size_t index = 0;
        for (const Json &row : surface.props["rows"])
        {
            if (row["id"] == "inventory:9:character:1:role:0")
                break;
            ++index;
        }
        Expect(index < surface.props["rows"].size(), "selection control item is visible");
        router.OnEvent({"items", "row", std::to_string(index)});
        const size_t sent = surface.commands.size();
        ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t1\tselect@5:inventory:9:character:1:role:0",
                    "all inventory kinds only select on tap");
        for (Widget &widget : model.widgets)
            if (widget.node == "TABLE_ITEMS")
                widget.value = widget.index == 1 ? "1" : "0";
            else if (widget.node == "EQUIP_BUTTON")
                widget.row_id = "inventory:9:character:1:role:0";
        ++model.revision;
        router.SetInterface(model);
        Expect(surface.commands.size() == sent && surface.props["detail"]["title"] == "Dagger",
               std::string(test.action) + ": selection response shows details without gameplay action");
        router.OnEvent({"item-equip", "click", ""});
        Expect(surface.commands.size() == sent + 1,
               std::string(test.action) + ": explicit button alone runs the game action");
        ExpectEqual(surface.Last(), "widget:items\tEQUIP_BUTTON\t-1\tpress@5:inventory:9:character:1:role:0",
                    "explicit action retains exact selected-row capability");
    }
}

// Repeated real router actions and authoritative model replies, not a local worn toggle.
// No game VM is run here: actual inventory consumption/persistence remains a runtime check.
static void RepeatedActions(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "items.json");
    for (Widget &widget : model.widgets)
        if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr2")
            widget.context = {{"slot", "amulet"}, {"action", "amulet"}, {"worn", "0"}, {"itemId", "test-amulet"}};
    router.SetInterface(model);
    router.OnEvent({"slot-slot1", "click", ""});
    router.OnEvent({"items", "row", "0"}); // only the unworn amulet fits this filter
    ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t1\tselect@5:inventory:9:character:1:role:0",
                "amulet tap selects its description");
    const size_t selected = surface.commands.size();
    for (Widget &widget : model.widgets)
        if (widget.node == "TABLE_ITEMS")
            widget.value = widget.index == 1 ? "1" : "0";
        else if (widget.node == "EQUIP_BUTTON")
            widget.row_id = "inventory:9:character:1:role:0", widget.label = "Equip";
    ++model.revision;
    router.SetInterface(model);
    Expect(surface.commands.size() == selected && surface.props["detail"]["action"]["label"] == "Equip",
           "amulet selection acknowledgement never equips");
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        size_t sent = surface.commands.size();
        router.OnEvent({"item-equip", "click", ""});
        Expect(surface.commands.size() == sent + 1, "explicit amulet equip sends one guarded game action");
        for (Widget &widget : model.widgets)
            if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr2")
                widget.context["worn"] = "1";
        for (Widget &widget : model.widgets)
            if (widget.node == "ANDROID_SLOTS" && widget.row_id == "slot1")
            {
                widget.label = "Amulet";
                widget.context = {{"artefact", "1"}, {"label", "Amulet"}, {"itemId", "test-amulet"}};
            }
        ++model.revision;
        router.SetInterface(model);
        Expect(surface.props["slots"][7].value("name", "") == "Amulet" &&
                   surface.props["detail"]["action"]["label"] == "<items.take_off>" &&
                   !surface.props["detail"]["action"]["hold"],
               "worn reply synchronizes slot and infinite amulet removal action");
        sent = surface.commands.size();
        router.OnEvent({"item-equip", "click", ""});
        Expect(surface.commands.size() == sent + 1, "explicit amulet unequip works again on the repeated cycle");
        for (Widget &widget : model.widgets)
            if (widget.node == "ANDROID_ITEMS" && widget.row_id == "tr2")
                widget.context["worn"] = "0";
        for (Widget &widget : model.widgets)
            if (widget.node == "ANDROID_SLOTS" && widget.row_id == "slot1")
            {
                widget.label.clear();
                widget.context = {{"artefact", "1"}};
            }
        ++model.revision;
        router.SetInterface(model);
        Expect(surface.props["slots"][7]["empty"] == true && surface.props["detail"]["action"]["label"] == "Equip",
               "unequipped reply clears worn slot and restores equip without a local equipment mutation");
    }
}

static void ItemCategories(const std::string &models)
{
    ItemActionCases(models);
    UndescribedRow(models);
    TimedArtefact(models);
    SlotOwnsParchment(models);
    AmuletSlots(models);
    SlotNamesFoldCase(models);
    SameDisplayNames(models);
    OtherRowsAction(models);
    SlotFilters(models);
    RepeatedActions(models);
}

// A HUD tap opens the character or the ship screen on its subject (hud::Hud::RequestSubject):
// the screen selects it with the header's switch, as the player would, once.
struct SubjectMount final : storm::ui::hud::Mount
{
    double now = 0.0;
    bool Measure(const std::string &, const std::string &, storm::ui::hud::Placement &) const override
    {
        return false;
    }
    bool ShowHud(const storm::ui::hud::View &) override
    {
        return true;
    }
    void HideHud() override
    {
    }
    void Send(const Action &) override
    {
    }
    void SetLanguage(const std::string &) override
    {
    }
    std::string Text(const std::string &key) override
    {
        return key;
    }
    void Log(const std::string &) override
    {
    }
    void Error(const std::string &) override
    {
    }
    double Now() const override
    {
        return now;
    }
    bool InterfaceActive() const override
    {
        return true;
    }
    bool ScreenMounted() const override
    {
        return true;
    }
    void Alert(const std::string &) override
    {
    }
};

static void OpenOnSubject(const std::string &models)
{
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"character", 9, -1});
        router.SetInterface(Load(models, "hero.json"));
        Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t1\tselect@", 0) == 0,
               "a tapped officer (character 9, the navigator's post) is selected in the officer switch: " +
                   surface.Last());
        const size_t sent = surface.commands.size();
        InterfaceModel again = Load(models, "hero.json");
        again.revision += 1;
        router.SetInterface(again);
        Expect(surface.commands.size() == sent, "the request is taken once");
    }
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"character", 1, -1});
        router.SetInterface(Load(models, "hero.json"));
        Expect(surface.commands.empty() && !hud.TakeSubject("character"),
               "the hero's portrait: the captain is already the subject, nothing to select");
        hud.RequestSubject({"character", 77, -1});
        InterfaceModel again = Load(models, "hero.json");
        again.revision += 1;
        router.SetInterface(again);
        Expect(surface.commands.empty() && !surface.errors.empty(),
               "a person not in the character list: the captain stays, and that is said");
    }
    for (const auto &[character, slot] : {std::pair{11, 3}, std::pair{20, 10}})
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"character", character, -1});
        router.SetInterface(Load(models, "hero-people.json"));
        Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t" + std::to_string(slot) + "\tselect@",
                                    0) == 0,
               "a tapped officer on two posts or a companion is selected by the first slot: " + surface.Last());
    }
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"ship", -1, 0});
        router.SetInterface(Load(models, "ship.json"));
        Expect(surface.Last().rfind("widget:ship\tSHIPS_SCROLL\t0\tselect@", 0) == 0,
               "a tapped flagship is selected in the squadron switch: " + surface.Last());
    }
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        hud.RequestSubject({"ship", -1, 1});
        router.SetInterface(Load(models, "ship.json"));
        Expect(surface.commands.empty() && surface.errors.empty(),
               "the tapped ship is the one shown already: nothing to select");
        hud.RequestSubject({"ship", -1, 4});
        InterfaceModel again = Load(models, "ship.json");
        again.revision += 1;
        router.SetInterface(again);
        Expect(surface.commands.empty() && !surface.errors.empty(), "a ship the squadron does not have: said aloud");
    }
}

// The ship screen (hero_ship.cpp): the game's figures in three tabs, its windows as our dialogs.
// The ship screen on one router: the figures, «Обмен», «Команда», «Трюм», the cannons and their calibre.
struct ShipRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    InterfaceModel model = Load(models, "ship.json");
    Json view; // what the screen showed last

    void Revise()
    {
        model.revision += 1;
        router.SetInterface(model);
    }
    void Remove(const char *prefix)
    {
        std::erase_if(model.widgets, [prefix](const Widget &w) { return w.node.rfind(prefix, 0) == 0; });
    }

    void Opening()
    {
        router.SetInterface(model);
        ExpectEqual(router.Using(), "ship", "the ship draws on the ship screen");
        ExpectEqual(surface.tag, "s-ship", "s-ship mounted");
        view = surface.props;
        Expect(view["extras"]["rows"].empty() && surface.errors.empty(), "every widget is known: no extras, no errors");
        Expect(view["card"]["title"] == "Swallow" && view["card"]["sub"] == "Sloop \xC2\xB7 <yard.class> 6" &&
                   view["card"]["captain"] == "<ship.captain> Peter Marsh \xC2\xB7 <ship.companion>" &&
                   view["card"]["rename"] == true && view["card"]["image"]["texture"] == "interfaces/ships/Sloop",
               "the card: name, type and class, the companion captain, the picture, the quill");
        Expect(view["card"]["hull"]["value"] == 50 && view["card"]["crew"]["mark"] == 22 &&
                   view["card"]["sails"]["text"] == "100%",
               "hull, crew (minimum marked) and sails bars");
        Expect(view["left"][0]["cells"][1]["text"] == "13 <common.knots_short> <common.of> 15" &&
                   view["left"][2]["cells"][1]["text"] == "65\xC2\xB0" &&
                   view["right"][0]["cells"][1]["text"] == "12 \xC3\x97 6 lb  \xE2\x80\xBA" &&
                   view["right"][2]["cells"][1]["text"] == "1\xC2\xA0"
                                                           "240",
               "the figures rounded, the guns row opens the cannons window");
        Expect(view["subject"]["label"] == "Swallow" && view["subject"]["sub"] == "2 <common.of> 2",
               "the squadron's ships are the header subject");

        Expect(view["subject"]["list"] == true, "two ships offer the list");
        router.OnEvent({"header", "subject-list", ""});
        Expect(surface.props["subject"]["open"] == true && surface.props["subject"]["title"] == "<ship.squadron>" &&
                   surface.props["subject"]["rows"].size() == 2 &&
                   surface.props["subject"]["rows"][1]["selected"] == true &&
                   surface.props["subject"]["rows"][0]["icon"] == "ship",
               "the squadron's ships listed, the current one selected");
        router.OnEvent({"header-subject-0", "", ""});
        ExpectEqual(surface.Last(), "widget:ship\tSHIPS_SCROLL\t0\tselect@7:1:0",
                    "a picked ship is selected as with ‹");
        Expect(!surface.props["subject"].contains("open"), "the pick closes the list");
        router.OnEvent({"header", "subject-prev", ""});
        ExpectEqual(surface.Last(), "widget:ship\tSHIPS_SCROLL\t0\tselect@7:1:0", "the previous ship");
        router.OnEvent({"ship-rename", "click", ""});
        ExpectEqual(surface.text.text, "Swallow", "the quill asks for the new name");
        router.SubmitText(surface.text.token, "Kestrel", true);
        ExpectEqual(surface.Last(), "widget:ship\tEDIT_BOX\t-1\tKestrel", "the name goes to the game's field");
        router.OnEvent({"stats-right", "row", "0"});
        ExpectEqual(surface.Last(), "widget:ship\tTABLE_OTHER\t8\tactivate@7:CannonType",
                    "the guns row opens the cannons window");
    }

    void Exchange()
    {
        // «Обмен»: the game asks, the intent answers.
        surface.now += 1.0;
        Revise();
        router.OnEvent({"ship-exchange", "click", ""});
        ExpectEqual(surface.Last(), "widget:ship\tSHIPS_SCROLL\t1\tactivate@7:1:1",
                    "the exchange activates the current ship");
        model.widgets.push_back(Make("REMOVE_WINDOW_CAPTION", "text", "Companion ship"));
        model.widgets.push_back(Make("REMOVE_WINDOW_TEXT", "text", "Switch to the exchange?"));
        model.widgets.push_back(Make("REMOVE_ACCEPT_OFFICER", "button", "Ok"));
        model.widgets.push_back(Make("REMOVE_CANCEL_OFFICER", "button", "Cancel"));
        Revise();
        ExpectEqual(surface.Last(), "widget:ship\tREMOVE_ACCEPT_OFFICER\t-1\tpress", "and the question is answered");
        Expect(surface.props["dialog"].is_null(), "the exchange question is not shown");
        Remove("REMOVE_");
        Revise();
    }

    void Crew()
    {
        // «Команда»: bars, morale price, the morale question and the shares.
        router.OnEvent({"header-tabs", "select", "crew"});
        view = surface.props;
        Expect(view["crew"]["groups"].size() == 3 && view["crew"]["groups"][0]["value"] == 62 &&
                   view["crew"]["groups"][1]["word"] == "green" && view["crew"]["morale"] == 45 &&
                   view["crew"]["moraleWord"] == "normal",
               "crew experience and morale from the adapter, the game's words");
        Expect(view["facts"][1]["title"] == "1 240 pesos" && view["facts"][1]["meta"] == "Ship upkeep" &&
                   view["crewActions"][1]["sub"] == "340 <ship.money>",
               "food and upkeep lines, the morale price");
        router.OnEvent({"crew-morale", "click", ""});
        ExpectEqual(surface.Last(), "widget:ship\tCREW_MORALE_BUTTON\t-1\tpress", "raise morale asks the game");
        model.widgets.push_back(Make("REMOVE_WINDOW_CAPTION", "text", "Raise morale"));
        model.widgets.push_back(Make("REMOVE_ACCEPT_OFFICER", "button", "Ok"));
        model.widgets.push_back(Make("REMOVE_CANCEL_OFFICER", "button", "Cancel"));
        Revise();
        Expect(surface.props["dialog"]["kind"] == "question" &&
                   surface.props["dialog"]["title"] == "<ship.raise_morale_for> 340 <ship.money>?",
               "the morale question with its price");
        router.OnEvent({"dialog-accept", "click", ""});
        ExpectEqual(surface.Last(), "widget:ship\tREMOVE_ACCEPT_OFFICER\t-1\tpress", "accepted");
        Expect(router.WantsBack(), "back closes the question first");
        router.Back();
        ExpectEqual(surface.Last(), "widget:ship\tREMOVE_CANCEL_OFFICER\t-1\tpress", "back cancels it");
        Remove("REMOVE_");
        model.widgets.push_back(Make("PARTITION_WINDOW_CAPTION", "text", "Crew shares"));
        model.widgets.push_back(Make("PARTITION_OK", "button", "Pay"));
        model.widgets.push_back(Make("PARTITION_CANCEL", "button", "Cancel"));
        Revise();
        view = surface.props["dialog"];
        Expect(view["kind"] == "shares" && view["shares"][2]["v"] == "500" &&
                   view["debts"][0]["v"] == "1\xC2\xA0"
                                            "200" &&
                   view["debts"][1]["v"] == "\xE2\x80\x94" && view["actions"][0]["label"] == "<ship.pay>" &&
                   view["subtitle"] == "<ship.last_payment> 1 September 1654",
               "the shares window as figures, the game's pay button with no sum of ours");
        router.OnEvent({"shares-pay", "click", ""});
        ExpectEqual(surface.Last(), "widget:ship\tPARTITION_OK\t-1\tpress", "paid by the game's button");
        Remove("PARTITION_");
        Revise();
    }

    void Hold()
    {
        // «Трюм»: a row opens the drop window; the control keeps what stays; «За борт N» held.
        router.OnEvent({"header-tabs", "select", "cargo"});
        view = surface.props;
        Expect(view["list"]["rows"].size() == 3 &&
                   view["list"]["rows"][1]["meta"] == "Pack 20 \xC2\xB7 Pack weight 1" &&
                   view["list"]["rows"][0]["image"]["texture"] == "atlas:GOODS/Balls" &&
                   view["load"]["text"] == "189 <common.of> 700 <transfer.cwt>",
               "the goods with their packs and pictures, the deadweight");
        router.OnEvent({"goods", "row", "1"});
        ExpectEqual(surface.Last(), "widget:ship\tTABLE_LIST\t1\tactivate@7:ship:good:5:owner:31",
                    "the row opens its drop window by the good's identity");
        model.widgets.push_back(Make("QTY_CAPTION", "text", "Gunpowder"));
        model.widgets.push_back(Make("QTY_INFO_SHIP_QTY", "text", "220"));
        Widget edit = Make("QTY_EDIT", "edit");
        edit.value = "0";
        model.widgets.push_back(edit);
        model.widgets.push_back(Make("QTY_OK_BUTTON", "button", "Ok"));
        model.widgets.push_back(Make("QTY_CANCEL_BUTTON", "button", "Cancel"));
        model.widgets.push_back(Make("QTY_BOMB_BUTTON", "button", "Mine"));
        Revise();
        view = surface.props;
        Expect(view["list"]["rows"][1]["q"]["value"] == 220 && view["list"]["rows"][1]["q"]["max"] == 220 &&
                   view["cargoActions"].size() == 2 && view["cargoActions"][1]["disabled"] == true &&
                   view["cargoActions"][1]["hold"] == true,
               "the open row: everything stays, nothing to drop yet, the mine button for powder");
        router.OnEvent({"goods", "change", "200"});
        surface.now += 1.0;
        router.Tick();
        ExpectEqual(surface.Last(), "widget:ship\tQTY_EDIT\t-1\t20", "the control sets the amount to drop");
        // The game takes the amount out of the hold while it is set: the row and the deadweight follow.
        for (Widget &widget : model.widgets)
            if (widget.node == "QTY_EDIT")
                widget.value = "220";
            else if (widget.node == "QTY_INFO_SHIP_QTY")
                widget.label = "0";
            else if (widget.node == "TABLE_OTHER" && widget.row_id == "Capacity")
                widget.cells[2].text = "178 / 700";
        std::erase_if(model.widgets, [](const Widget &w) { return w.node == "TABLE_LIST" && w.index == 1; });
        surface.now += 2.0;
        Revise();
        view = surface.props;
        Expect(view["list"]["rows"].size() == 3 && view["list"]["rows"][1]["name"] == "Gunpowder" &&
                   view["list"]["rows"][1]["q"]["value"] == 0 && view["list"]["rows"][1]["cells"][0] == "220" &&
                   view["load"]["text"] == "189 \xE2\x80\xBA 178 <common.of> 700 <transfer.cwt>" &&
                   view["cargoActions"][1]["label"] == "<ship.overboard> 220",
               "all of it set to go: the row stays in place, the deadweight shows the drop");
        router.OnEvent({"goods-drop", "click", ""});
        ExpectEqual(surface.Last(), "widget:ship\tQTY_OK_BUTTON\t-1\tpress", "«За борт» is the game's OK");
        router.OnEvent({"goods", "row", "0"});
        ExpectEqual(surface.Last(), "widget:ship\tQTY_CANCEL_BUTTON\t-1\tpress",
                    "another good first closes the open window");
        Expect(router.WantsBack(), "back closes the drop window first");
        router.Back();
        ExpectEqual(surface.Last(), "widget:ship\tQTY_CANCEL_BUTTON\t-1\tpress", "back cancels the drop");
        Remove("QTY_");
        Revise();
        ExpectEqual(surface.Last(), "widget:ship\tTABLE_LIST\t0\tactivate@7:ship:good:0:owner:31",
                    "then the other good opens");
    }

    void Cannons()
    {
        // The cannons window: guns per side walked one arrow press per revision.
        router.OnEvent({"header-tabs", "select", "overview"});
        model.widgets.push_back(Make("CANNONS_CAPTION", "text", "Cannons"));
        model.widgets.push_back(Make("CANNONS_TEXT", "text", "Cannon 6 lb"));
        for (const char *side : {"F", "B", "R", "L"})
        {
            model.widgets.push_back(Make(("CANNONS_QTY_" + std::string(side)).c_str(), "text", "1"));
            model.widgets.push_back(Make(("CANNONS_LEFT_" + std::string(side)).c_str(), "button"));
            model.widgets.push_back(Make(("CANNONS_RIGHT_" + std::string(side)).c_str(), "button"));
        }
        model.widgets.push_back(Make("CANNONS_OK", "button", "Ok"));
        model.widgets.push_back(Make("CANNONS_CANCEL", "button", "Cancel"));
        Revise();
        view = surface.props["sheet"];
        Expect(view["title"] == "Cannons" && view["sides"].size() == 4 && view["sides"][0]["q"]["max"] == 2 &&
                   view["sides"][0]["q"]["ceiling"] == 1 &&
                   view["sides"][0]["q"]["moreReason"] == "<ship.no_guns_in_hold>" && view["actions"].size() == 1,
               "the cannons sheet: four sides, each up to its ports");
        Expect(view["calibres"]["rows"].empty() && view["calibres"]["empty"] == "<ship.no_spare_guns>" &&
                   view["subtitle"] == "<ship.guns_fitted> Cannon 6 lb \xC2\xB7 <ship.caliber> <ship.up_to> 6 lb",
               "no guns in the hold for this ship: the calibre list says so, with the ship's calibre");
        router.OnEvent({"guns-port", "change", "3"});
        ExpectEqual(surface.Last(), "widget:ship\tCANNONS_RIGHT_L\t-1\tpress", "one more gun on the port side");
        for (Widget &widget : model.widgets)
            if (widget.node == "CANNONS_QTY_L")
                widget.label = "2";
        Revise();
        ExpectEqual(surface.Last(), "widget:ship\tCANNONS_RIGHT_L\t-1\tpress", "and another after the engine answered");
        const size_t sent = surface.commands.size();
        Revise(); // the game took no more (2 again): the intent ends
        Expect(surface.commands.size() == sent, "no press without progress");
        router.Back();
        ExpectEqual(surface.Last(), "widget:ship\tCANNONS_CANCEL\t-1\tpress", "back cancels the cannons window");
    }

    void Calibre()
    {
        // The calibre: the guns in the hold the game lists for this ship (ship.c FillCannonsTable); a picked row is
        // selected in the game's table and «Fit all» (CanonsSetAll) fits that type on every side.
        FakeSurface guns_surface;
        Router guns_router(guns_surface);
        InterfaceModel guns = Load(models, "ship-cannons.json");
        guns_router.SetInterface(guns);
        view = guns_surface.props["sheet"];
        Expect(guns_surface.errors.empty() && guns_surface.props["extras"]["rows"].empty(),
               "the cannons window: every widget known, no errors");
        Expect(view["subtitle"] == "<ship.guns_fitted> Cannon 6 lb \xC2\xB7 <ship.caliber> <ship.up_to> 6 lb" &&
                   view["calibres"]["rows"].size() == 2 && view["calibres"]["label"] == "Cannons in the hold" &&
                   view["calibres"]["count"] == "Qty" &&
                   view["calibres"]["rows"][0]["cells"][0]["text"] == "Cannon 3 lb" &&
                   view["calibres"]["rows"][0]["cells"][0]["sub"] ==
                       "Range 350 \xC2\xB7 Damage x1.0 \xC2\xB7 Time 10" &&
                   view["calibres"]["rows"][0]["cells"][0]["image"]["texture"] == "atlas:GOODS/Cannon_3" &&
                   view["calibres"]["rows"][0]["cells"][1] == "8" && view["calibres"]["rows"][0]["selected"] == false,
               "the guns in the hold with their figures under the game's headings: " + view["calibres"].dump());
        Expect(view["actions"].size() == 3 && view["actions"][0]["id"] == "cannons-fit" &&
                   view["actions"][0]["label"] == "Fit all" && view["actions"][0]["sub"] == "Cannon 6 lb" &&
                   view["actions"][1]["id"] == "cannons-remove" && view["actions"][2]["id"] == "cannons-ok",
               "nothing picked: «Fit all» refits the fitted type; «Remove all» and OK beside it");
        Expect(view["sides"][3]["id"] == "guns-stern" && view["sides"][1]["q"]["value"] == 3 &&
                   view["sides"][1]["q"]["max"] == 4 && view["sides"][1]["q"]["ceiling"] == 4 &&
                   view["sides"][1]["q"]["moreReason"] == "" && view["sides"][0]["q"]["ceiling"] == 2,
               "one spare 6 lb gun: the port side may take its fourth, the bow its second");
        guns_router.OnEvent({"cannons-stock", "row", "0"});
        ExpectEqual(guns_surface.Last(), "widget:ship\tCANNONS_TABLE\t0\tselect",
                    "a picked calibre is selected in the game's table");
        for (Widget &widget : guns.widgets)
            if (widget.node == "CANNONS_TABLE")
                widget.value = widget.index == 0 ? "1" : "0";
        guns.revision += 1;
        guns_router.SetInterface(guns);
        view = guns_surface.props["sheet"];
        Expect(view["calibres"]["rows"][0]["selected"] == true && view["actions"][0]["sub"] == "Cannon 3 lb",
               "the picked row is marked and «Fit all» names it");
        guns_router.OnEvent({"cannons-fit", "click", ""});
        ExpectEqual(guns_surface.Last(), "widget:ship\tCANNONS_SET_ALL\t-1\tpress", "«Fit all» is the game's button");
        guns_router.OnEvent({"cannons-ok", "click", ""});
        ExpectEqual(guns_surface.Last(), "widget:ship\tCANNONS_OK\t-1\tpress", "OK applies");
    }
};

static void Ship(const std::string &models)
{
    ShipRun run{models};
    run.Opening();
    run.Exchange();
    run.Crew();
    run.Hold();
    run.Cannons();
    run.Calibre();
}

static void Journal(const std::string &)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel model;
    model.screen = "questbook";
    model.language = "english";
    model.opening = 8;
    model.revision = 30;
    model.widgets.push_back(Make("TITLE", "heading", " Log "));
    for (const char *tab : {"TABBTN_QUEST", "TABBTN_QUESTARJ", "TABBTN_INFO", "TABBTN_CASHBOOK", "TABBTN_SHIP_PLACE",
                            "TABBTN_STOREBOOK", "TABBTN_STATISTIC"})
    {
        Widget button = Make(tab, "image", "", {"press"});
        button.value = std::string(tab) == "TABBTN_QUEST" ? "1" : "0";
        model.widgets.push_back(button);
        model.widgets.push_back(Make((std::string("TABSTR_") + (tab + 7)).c_str(), "text", tab + 7));
    }
    for (int i = 0; i < 2; ++i)
    {
        Widget title = Make("QUEST_TITLE", "choice", i ? "Debts" : "Burden", {"open"});
        title.index = i;
        title.row_id = i ? "debts" : "burden";
        model.widgets.push_back(title);
    }
    router.SetInterface(model);
    ExpectEqual(router.Using(), "questbook", "the journal draws on the journal screen");
    Json view = surface.props;
    Expect(view["section"] == "quests" && view["list"].size() == 2 && view["subtabs"].size() == 2 &&
               view["entry"].is_null() && view["sections"][0]["count"] == "2",
           "quests: active and completed, the titles, nothing open");
    Expect(view["extras"]["rows"].empty(), "every widget is known: no extras");

    router.OnEvent({"entries", "row", "1"});
    ExpectEqual(surface.Last(), "widget:questbook\tQUEST_TITLE\t1\topen@8:debts", "a title opens its text");
    InterfaceModel text = model;
    text.revision += 1;
    text.widgets.erase(std::remove_if(text.widgets.begin(), text.widgets.end(),
                                      [](const Widget &w) { return w.node == "QUEST_TITLE"; }),
                       text.widgets.end());
    text.widgets.push_back(
        Make("QUEST_TEXT", "text", "1 May 1654 10:02\nArrived.\nMet the brother.\n2 May 1654 09:00\nLeft."));
    router.SetInterface(text);
    view = surface.props;
    Expect(view["list"].size() == 2 && view["list"][1]["selected"] == true && view["entry"]["title"] == "Debts" &&
               view["entry"]["entries"].size() == 2 &&
               view["entry"]["entries"][0]["text"] == "Arrived.\nMet the brother.",
           "the titles stay while the text is open; the text split into dated entries");
    const size_t sent = surface.commands.size();
    router.OnEvent({"entries", "row", "0"});
    ExpectEqual(surface.Last(), "back", "another entry: back to the titles first");
    model.revision = text.revision + 1;
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:questbook\tQUEST_TITLE\t0\topen@8:burden", "then open it");
    Expect(surface.commands.size() == sent + 2, "two steps");
    Expect(surface.props["entry"].is_object() && surface.props["entry"]["title"] == "Debts",
           "meanwhile the open text stays on the screen, not the titles of the step between: " +
               surface.props["entry"].dump());

    text.revision = model.revision + 1;
    router.SetInterface(text);
    Expect(router.WantsBack(), "an open text: back is the screen's");
    router.Back();
    ExpectEqual(surface.Last(), "back", "back closes the text");
    model.revision = text.revision + 1;
    router.SetInterface(model);
    Expect(surface.commands.back() == "back" && surface.commands[surface.commands.size() - 2] == "back",
           "and goes on to leave the journal");

    router.OnEvent({"section-stats", "click", ""});
    ExpectEqual(surface.Last(), "widget:questbook\tTABBTN_STATISTIC\t-1\tpress", "a section presses its game tab");

    // A «Logbook Update» line of the record that closed its quest (AddQuestRecord + CloseQuestHeader, as the
    // FalseTrace quest does): the journal opens on the active tab, the quest is on the completed one.
    {
        FakeSurface toast_surface;
        Router toast_router(toast_surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        toast_router.AttachHud(&hud);
        hud.RequestSubject({"journal", -1, -1, "",
                            "Closed"}); // as the script names it; the title's id is the lower-case attribute name
        InterfaceModel active = model;
        active.revision = 100;
        toast_router.SetInterface(active);
        ExpectEqual(toast_surface.Last(), "widget:questbook\tTABBTN_QUESTARJ\t-1\tpress",
                    "an entry not on the active tab: the completed tab is looked at");
        InterfaceModel completed = active;
        completed.revision += 1;
        completed.widgets.erase(std::remove_if(completed.widgets.begin(), completed.widgets.end(),
                                               [](const Widget &w) { return w.node == "QUEST_TITLE"; }),
                                completed.widgets.end());
        for (Widget &w : completed.widgets)
            if (w.node.rfind("TABBTN_", 0) == 0)
                w.value = w.node == "TABBTN_QUESTARJ" ? "1" : "0";
        Widget closed = Make("QUEST_TITLE", "choice", "Closed", {"open"});
        closed.index = 0;
        closed.row_id = "closed";
        completed.widgets.push_back(closed);
        toast_router.SetInterface(completed);
        ExpectEqual(toast_surface.Last(), "widget:questbook\tQUEST_TITLE\t0\topen@8:closed", "… and opened there");
        Expect(toast_surface.props["list"].size() == 2,
               "the walk keeps the first view of the journal until the entry is open");
    }
}

// The application's own screens on the "launcher" channel: views, texts by key, verbs back.
// The launcher, the download library and the about screen on one router, in the order the player meets them.
struct LauncherRun
{
    FakeSurface surface;
    Router router{surface};

    std::string Verb() const
    {
        return surface.actions.empty() ? std::string()
                                       : surface.actions.back().node + " " + surface.actions.back().verb + " " +
                                             surface.actions.back().value;
    }
    static Json LauncherState(Json data)
    {
        return Json{{"screen", "launcher"}, {"language", "english"}, {"launcher", std::move(data)}};
    }
    std::ptrdiff_t ShownCount(const std::string &line) const
    {
        return std::count(surface.shown.begin(), surface.shown.end(), line);
    }

    void SetupView()
    {
        router.SetState("launcher", LauncherState({{"phase", "setup"}, {"hasGame", false}, {"problem", "no_game"}}));
        ExpectEqual(surface.tag, "s-launcher", "launcher channel mounts the launcher");
        ExpectEqual(surface.language, "english", "launcher language follows the host");
        ExpectEqual(surface.props["view"].get<std::string>(), "setup", "no game: setup");
        Expect(surface.props["notices"].empty(), "no game is not an error notice");
        Expect(!surface.props["setup"]["library"].get<std::string>().empty(), "the downloads link is offered");
        router.OnEvent({"folder", "click", ""});
        ExpectEqual(Verb(), "APP folder ", "folder row asks the host for the picker");
        router.OnEvent({"archive", "click", ""});
        ExpectEqual(Verb(), "APP archive ", "archive row asks for the documents picker");
        router.OnEvent({"library", "click", ""});
        ExpectEqual(Verb(), "APP library ", "the downloads link opens the download library");
        router.OnEvent({"help", "click", ""});
        Expect(surface.props["extras"]["reading"].get<bool>(), "help opens the reading sheet");
        router.Back();
        Expect(!surface.props["extras"]["reading"].get<bool>(), "back closes the reading sheet first");
    }

    void ImportResults()
    {
        // The reader's detail goes where the text puts {1}, after the rest.
        surface.texts["launcher.result.import_archive_volumes"] = "<volumes.>\n{1}";
        const size_t errorsBefore = surface.errors.size();
        router.SetState("launcher",
                        LauncherState({{"phase", "setup"},
                                       {"hasGame", true},
                                       {"files", true},
                                       {"result", {{"key", "import_archive_volumes"}, {"args", {"setup-2.bin"}}}}}));
        Expect(surface.errors.size() == errorsBefore, "a detail placed by {1} is no error");
        const Json notice = surface.props["notices"][0];
        ExpectEqual(notice["tone"].get<std::string>(), "danger", "failed import is a danger notice");
        ExpectEqual(notice["text"].get<std::string>(), "<volumes.> <launcher.kept>\nsetup-2.bin",
                    "failure names the files and keeps the old game");
        {
            FakeSurface results;
            Router shown(results);
            const auto show = [&](const std::string &key, const Json &args) {
                shown.SetState("launcher", LauncherState({{"phase", "setup"},
                                                          {"hasGame", false},
                                                          {"result", {{"key", key}, {"args", args}}}}));
                return results.props["notices"][0]["text"].get<std::string>();
            };
            results.texts["launcher.result.import_archive_damaged"] = "<damaged.>\n{1}";
            ExpectEqual(show("import_archive_damaged", Json::array({""})), "<damaged.>",
                        "an empty detail leaves no line");
            Expect(results.errors.empty(), "an empty detail is no error");
            results.texts["launcher.result.import_archive_damaged"] = "<damaged.>";
            show("import_archive_damaged", Json::array({"a.zip: bad name"}));
            Expect(results.errors.size() == 1 && results.errors[0].find("has no {1}") != std::string::npos,
                   "a result with a detail whose text has no {1} is an error");
            results.texts["launcher.result.import_archive_memory"] = "<memory.> {1}";
            show("import_archive_memory", Json::array({""}));
            Expect(results.errors.size() == 2 && results.errors[1].find("has {1}") != std::string::npos,
                   "a result without a detail whose text has {1} is an error");
        }
        Expect(surface.props["back"].get<bool>(), "changing files of a ready game has ←");
        router.Back();
        ExpectEqual(Verb(), "APP files-close ", "← returns to the ready launcher");
        ExpectEqual(surface.props["setup"]["prompt"].get<std::string>(), "<launcher.replace>",
                    "a ready game's files: another copy replaces it");
        ExpectEqual(surface.props["installed"]["title"].get<std::string>(), "<launcher.installed>",
                    "the installed game before it was read");
        router.SetState("launcher", LauncherState({{"phase", "setup"},
                                                   {"hasGame", true},
                                                   {"files", true},
                                                   {"installed",
                                                    {{"name", "Sea Dogs"},
                                                     {"version", "ver. 1.7.3"},
                                                     {"languages", {"russian"}},
                                                     {"defaultLanguage", "russian"},
                                                     {"bytes", 1073741824.0},
                                                     {"patches", Json::array()}}}}));
        ExpectEqual(surface.props["installed"]["title"].get<std::string>(), "Sea Dogs 1.7.3",
                    "the installed game by its name and version");
        ExpectEqual(surface.props["installed"]["meta"].get<std::string>(),
                    "<launcher.language.russian> · <launcher.gb>", "its languages and size");
        ExpectEqual(surface.props["installed"]["untested"].get<std::string>(), "",
                    "a copy not read for its version says nothing about it");
        surface.texts["launcher.root_untested"] = "<not {1}>";
        const Json tested = Json::array({"ver. 1.7.3 (26.11.21) x64"});
        const auto installedCopy = [&](Json patchSet) {
            return LauncherState({{"phase", "setup"},
                                  {"hasGame", true},
                                  {"files", true},
                                  {"installed",
                                   {{"name", "Sea Dogs"},
                                    {"version", "ver. 1.8"},
                                    {"identified", true},
                                    {"patchSet", std::move(patchSet)},
                                    {"testedVersions", tested}}}});
        };
        router.SetState("launcher", installedCopy(nullptr));
        ExpectEqual(surface.props["installed"]["untested"].get<std::string>(), "<not ver. 1.7.3 (26.11.21) x64>",
                    "a copy no script patch set identifies: not the tested version, named from the patch layer");
        router.SetState("launcher",
                        installedCopy({{"id", "teho-1.7.3-x64"}, {"version", "ver. 1.7.3 (26.11.21) x64"}}));
        ExpectEqual(surface.props["installed"]["untested"].get<std::string>(), "", "the tested version says nothing");
        router.OnEvent({"delete-game", "click", ""});
        ExpectEqual(Verb(), "APP delete-game ", "the held «Удалить» deletes the game");
        router.SetState("launcher",
                        LauncherState({{"phase", "ready"},
                                       {"hasGame", true},
                                       {"result", {{"key", "import_interrupted"}, {"args", Json::array()}}}}));
        Expect(surface.props["notices"][0]["tone"].get<std::string>() == "warn" &&
                   surface.props["notices"][0]["text"].get<std::string>() ==
                       "<launcher.result.import_interrupted> <launcher.kept>",
               "an interrupted install found at start is said once, with the game that is left");
    }

    void Notices()
    {
        // The game did not start on a full storage: the megabytes to free, in place of {1}.
        surface.texts["launcher.result.start_no_space"] = "<free {1} MB>";
        router.SetState("launcher", LauncherState({{"phase", "ready"},
                                                   {"hasGame", true},
                                                   {"result", {{"key", "start_no_space"}, {"args", {41.2}}}}}));
        ExpectEqual(surface.props["notices"][0]["text"].get<std::string>(), "<free 41 MB>",
                    "a start on a full storage says what to free");
        ExpectEqual(surface.props["notices"][0]["tone"].get<std::string>(), "danger", "and is a danger notice");

        // Nothing reaches the screen unlogged, the launcher's notices included: each is logged when it appears,
        // once while it stays, and again when it comes back.
        const std::string volumes = "danger (notice on screen app:launcher): <launcher.failed> "
                                    "<volumes.> <launcher.kept>\nsetup-2.bin";
        Expect(ShownCount(volumes) == 1, "a failed import's notice is logged");
        for (const char *key : {"picker_unavailable", "exit_game_first", "picker_unavailable"})
        {
            router.SetState("launcher", LauncherState({{"phase", "setup"},
                                                       {"hasGame", true},
                                                       {"files", true},
                                                       {"result", {{"key", key}, {"args", Json::array()}}}}));
            router.SetState("launcher", LauncherState({{"phase", "setup"},
                                                       {"hasGame", true},
                                                       {"files", true},
                                                       {"result", {{"key", key}, {"args", Json::array()}}}}));
        }
        Expect(ShownCount("info (notice on screen app:launcher): <launcher.result.picker_unavailable>") == 2,
               "picker_unavailable is logged at each appearance, not at each build");
        Expect(ShownCount("info (notice on screen app:launcher): <launcher.result.exit_game_first>") == 1,
               "exit_game_first is logged");
        router.SetState(
            "launcher",
            LauncherState({{"phase", "ready"}, {"hasGame", true}, {"exit", {{"reason", "anr"}, {"when", "today"}}}}));
        Expect(ShownCount("warn (notice on screen app:launcher): <launcher.exit_previous>") == 1,
               "the previous exit's notice is logged");
        ExpectEqual(surface.props["view"].get<std::string>(), "ready", "ready view");
        ExpectEqual(surface.props["notices"][0]["text"].get<std::string>(), "<launcher.exit_previous>",
                    "previous exit noticed");
        // A phone below the minimum requirements: one warning after the others, on the ready and the setup view, closed
        // by its × (the app stores that); logged when it appears.
        router.SetState("launcher", LauncherState({{"phase", "ready"},
                                                   {"hasGame", true},
                                                   {"belowMinimum", true},
                                                   {"exit", {{"reason", "anr"}, {"when", "today"}}}}));
        Expect(surface.props["notices"].size() == 2 &&
                   surface.props["notices"][1]["text"].get<std::string>() == "<launcher.below_minimum>" &&
                   surface.props["notices"][1]["tone"].get<std::string>() == "warn" &&
                   surface.props["notices"][1]["dismiss"].get<std::string>() == "below-minimum",
               "below the minimum: a closable warning after the previous exit's");
        Expect(ShownCount("warn (notice on screen app:launcher): <launcher.below_minimum>") == 1,
               "the below-minimum notice is logged");
        router.OnEvent({"below-minimum", "click", ""});
        ExpectEqual(Verb(), "APP below-minimum-dismiss ", "its × closes it");
        router.SetState(
            "launcher",
            LauncherState({{"phase", "setup"}, {"hasGame", false}, {"problem", "no_game"}, {"belowMinimum", true}}));
        Expect(surface.props["notices"].size() == 1 &&
                   surface.props["notices"][0]["dismiss"].get<std::string>() == "below-minimum",
               "the setup view shows it too");
        router.SetState(
            "launcher",
            LauncherState({{"phase", "ready"}, {"hasGame", true}, {"exit", {{"reason", "anr"}, {"when", "today"}}}}));
        Expect(surface.props["notices"].size() == 1, "closed: gone");
        router.OnEvent({"play", "click", ""});
        ExpectEqual(Verb(), "APP play ", "play");
        router.OnEvent({"about", "click", ""});
        ExpectEqual(Verb(), "APP about ", "about from the header");
    }

    void Installing()
    {
        // While the app checks the files and installs the layers before the game starts, «Играть»
        // shows it, for as long as it takes (not only the router's pending second and a half).
        surface.now += 5.0;
        router.SetState("launcher", LauncherState({{"phase", "ready"}, {"hasGame", true}, {"starting", true}}));
        Expect(surface.props["play"]["busy"].get<bool>(), "starting: «Играть» is busy");
        router.SetState("launcher", LauncherState({{"phase", "ready"}, {"hasGame", true}, {"starting", false}}));
        Expect(!surface.props["play"]["busy"].get<bool>(), "started (or failed): «Играть» is free again");

        surface.now = 10;
        router.SetState(
            "launcher",
            LauncherState(
                {{"phase", "busy"},
                 {"hasGame", false},
                 {"import", {{"stage", "copying"}, {"bytes", 0}, {"total", 2147483648.0}, {"canCancel", true}}}}));
        surface.now = 70;
        router.SetState(
            "launcher",
            LauncherState(
                {{"phase", "busy"},
                 {"hasGame", false},
                 {"import",
                  {{"stage", "copying"}, {"bytes", 1073741824.0}, {"total", 2147483648.0}, {"canCancel", true}}}}));
        ExpectEqual(surface.props["busy"]["title"].get<std::string>(), "<launcher.stage.copying>", "stage title");
        Expect(surface.props["busy"]["percent"].get<double>() == 50, "percent of the stage");
        Expect(surface.props["busy"]["status"].get<std::string>().find("<launcher.left>") != std::string::npos,
               "time left once the rate is known");
        router.OnEvent({"cancel", "click", ""});
        ExpectEqual(Verb(), "APP cancel ", "cancel import");
        router.SetState("launcher", LauncherState({{"phase", "busy"},
                                                   {"hasGame", true},
                                                   {"import", {{"stage", "deleting"}, {"canCancel", false}}}}));
        Expect(surface.props["busy"]["title"].get<std::string>() == "<launcher.stage.deleting>" &&
                   surface.props["cancel"].is_null() &&
                   surface.props["title"].get<std::string>() == "<launcher.title_setup>",
               "deleting the game: said so on the game files screen, nothing to cancel");

        router.SetState("launcher", LauncherState({{"phase", "busy"},
                                                   {"hasGame", false},
                                                   {"import",
                                                    {{"stage", "choosing"},
                                                     {"canCancel", true},
                                                     {"choices",
                                                      {{{"id", "a"}, {"name", "A"}, {"missing", {"a-2.bin"}}},
                                                       {{"id", "b"}, {"name", "B"}, {"missing", Json::array()}}}}}}}));
        ExpectEqual(surface.props["view"].get<std::string>(), "choose", "several copies: the picker");
        Expect(surface.props["choose"]["rows"][0]["disabled"].get<bool>() &&
                   surface.props["choose"]["rows"][1]["selected"].get<bool>(),
               "a copy without its volumes cannot be chosen; the ready one is preselected");
        ExpectEqual(surface.props["choose"]["untested"].get<std::string>(), "",
                    "copies not read for their version are not marked");
        {
            surface.texts["launcher.choose_untested"] = "<marked: not {1}>";
            const Json tested = Json::array({"ver. 1.7.3 (26.11.21) x64"});
            const Json set = {{"id", "teho-1.7.3-x64"}, {"version", "ver. 1.7.3 (26.11.21) x64"}};
            router.SetState("launcher", LauncherState({{"phase", "busy"},
                                                       {"hasGame", false},
                                                       {"import",
                                                        {{"stage", "choosing"},
                                                         {"canCancel", true},
                                                         {"choices",
                                                          {{{"id", "a"},
                                                            {"name", "A"},
                                                            {"missing", Json::array()},
                                                            {"identified", true},
                                                            {"patchSet", set},
                                                            {"testedVersions", tested}},
                                                           {{"id", "b"},
                                                            {"name", "B"},
                                                            {"missing", Json::array()},
                                                            {"identified", true},
                                                            {"patchSet", nullptr},
                                                            {"testedVersions", tested}},
                                                           {{"id", "c"},
                                                            {"name", "C"},
                                                            {"missing", Json::array()},
                                                            {"identified", false},
                                                            {"patchSet", nullptr},
                                                            {"testedVersions", tested}}}}}}}));
            const Json &rows = surface.props["choose"]["rows"];
            Expect(rows[0]["cells"][1] == Json("") && rows[2]["cells"][1] == Json(""),
                   "the tested copy and an unread one are not marked");
            Expect(rows[1]["cells"][1]["text"].get<std::string>() == "<launcher.root_untested_mark>" &&
                       !rows[1]["disabled"].get<bool>(),
                   "a copy no set identifies is marked and can still be chosen");
            ExpectEqual(surface.props["choose"]["untested"].get<std::string>(),
                        "<marked: not ver. 1.7.3 (26.11.21) x64>", "the notice above names the tested version");
            router.OnEvent({"roots", "row", "1"});
            router.OnEvent({"roots", "click", ""});
            ExpectEqual(Verb(), "APP choose b", "the untested copy is installed when chosen");
        }
        router.OnEvent({"roots", "click", ""});
        ExpectEqual(Verb(), "APP choose b", "use the chosen copy");

        router.SetState(
            "launcher",
            LauncherState(
                {{"phase", "busy"},
                 {"hasGame", false},
                 {"import",
                  {{"stage", "reading"}, {"canCancel", true}, {"password", {{"for", "x.zip"}, {"retry", true}}}}}}));
        ExpectEqual(surface.props["password"]["title"].get<std::string>(), "<launcher.password_wrong>",
                    "wrong password asks again");
        router.OnEvent({"password-field", "press", ""});
        Expect(surface.text.secret && !surface.text.token.empty(), "password field opens a masked editor");
        router.SubmitText(surface.text.token, "secret", true);
        ExpectEqual(Verb(), "PASSWORD edit secret", "the typed password goes to the host");
    }

    void DownloadStates()
    {
        // Download library: nothing, not even «empty», until the host has read it.
        router.SetState("launcher", Json{{"screen", "downloads"},
                                         {"language", "english"},
                                         {"downloads", {{"loaded", false}, {"sets", Json::array()}}}});
        Expect(!surface.props["empty"].get<bool>() && surface.props["actions"].empty(),
               "an unread library says nothing");
        router.SetState("launcher", Json{{"screen", "downloads"},
                                         {"language", "english"},
                                         {"downloads", {{"loaded", true}, {"sets", Json::array()}}}});
        Expect(surface.props["empty"].get<bool>(), "a read library without sets is empty");
        {
            constexpr double kGiB = 1073741824.0;
            const Json rows = Json::array({Json{{"id", "a"},
                                                {"kind", "torrent"},
                                                {"name", "A"},
                                                {"state", "downloading"},
                                                {"downloaded", 1.2 * kGiB},
                                                {"wanted", 12.7 * kGiB},
                                                {"rate", 3.5 * 1048576.0}},
                                           Json{{"id", "b"},
                                                {"kind", "torrent"},
                                                {"name", "B"},
                                                {"state", "downloading"},
                                                {"downloaded", 1.0 * kGiB},
                                                {"wanted", 12.0 * kGiB},
                                                {"rate", 20000.0},
                                                {"offline", true},
                                                {"stalled", true}},
                                           Json{{"id", "c"},
                                                {"kind", "torrent"},
                                                {"name", "C"},
                                                {"state", "checking"},
                                                {"checked", 0.345},
                                                {"wanted", 12.0 * kGiB}},
                                           Json{{"id", "d"},
                                                {"kind", "torrent"},
                                                {"name", "D"},
                                                {"state", "paused"},
                                                {"downloaded", 6.0 * kGiB},
                                                {"wanted", 12.0 * kGiB}},
                                           Json{{"id", "e"},
                                                {"kind", "torrent"},
                                                {"name", "E"},
                                                {"state", "downloading"},
                                                {"downloaded", 1.0 * kGiB},
                                                {"wanted", 12.0 * kGiB},
                                                {"rate", 20000.0}}});
            router.SetState("launcher", Json{{"screen", "downloads"},
                                             {"language", "english"},
                                             {"downloads", {{"loaded", true}, {"sets", rows}}}});
            const auto meta = [this](int i) { return surface.props["sets"][i]["meta"].get<std::string>(); };
            ExpectEqual(meta(0),
                        "<downloads.state.downloading> · <downloads.size_of> · <downloads.speed> · <launcher.left>",
                        "downloading: done of total, speed, time left");
            ExpectEqual(meta(1), "<downloads.state.downloading> · <downloads.offline>",
                        "no network: said so, no time left and no peers' blame");
            ExpectEqual(surface.props["sets"][1]["metaTone"].get<std::string>(), "danger", "no network is a problem");
            ExpectEqual(meta(2), "<downloads.state.checking> · 34%",
                        "the hash check after a resume is its own state with its share");
            Expect(surface.props["sets"][2]["progress"].get<double>() == 34, "the check fills the bar");
            ExpectEqual(meta(3), "<downloads.state.paused> · <downloads.size_of>", "paused: done of total");
            ExpectEqual(meta(4), "<downloads.state.downloading> · <downloads.size_of> · <downloads.speed>",
                        "a time left of days is not shown");
        }
    }

    void DownloadSets()
    {
        const Json sets = Json::array({Json{{"id", "1"},
                                            {"kind", "torrent"},
                                            {"name", "Full"},
                                            {"state", "downloading"},
                                            {"downloaded", 50.0},
                                            {"wanted", 100.0},
                                            {"rate", 10.0},
                                            {"main", "pause"},
                                            {"menu", {"details", "delete"}}},
                                       Json{{"id", "2"},
                                            {"kind", "archive"},
                                            {"name", "a.zip"},
                                            {"state", "complete"},
                                            {"main", "import"},
                                            {"menu", {"delete"}}},
                                       Json{{"id", "3"},
                                            {"kind", "magnet"},
                                            {"name", "Quiet"},
                                            {"state", "metadata"},
                                            {"stalled", true},
                                            {"peers", 0.0},
                                            {"networkError", "Tracker x: 404"},
                                            {"main", "pause"}},
                                       Json{{"id", "4"},
                                            {"kind", "magnet"},
                                            {"name", "Full disk"},
                                            {"state", "error"},
                                            {"errorKind", "storage-full"},
                                            {"error", "No space left on device"},
                                            {"main", "resume"},
                                            {"menu", {"delete"}}}});
        router.SetState(
            "launcher",
            Json{{"screen", "downloads"}, {"language", "english"}, {"downloads", {{"loaded", true}, {"sets", sets}}}});
        ExpectEqual(surface.tag, "s-downloads", "downloads screen");
        ExpectEqual(surface.props["sets"][0]["action"].get<std::string>(), "pause", "an active set pauses inline");
        Expect(surface.props["sets"][0]["progress"].get<double>() == 50, "download progress");
        ExpectEqual(surface.props["sets"][1]["button"]["id"].get<std::string>(), "btn-2", "import is an inline button");
        const std::string stalledMeta = surface.props["sets"][2]["meta"].get<std::string>();
        Expect(stalledMeta.find("<downloads.stalled_no_peers>") != std::string::npos &&
                   stalledMeta.find("Tracker") == std::string::npos &&
                   surface.props["sets"][2]["metaTone"].get<std::string>() == "danger",
               "a stalled set says why in our words; the network problem only goes to the log");
        const std::string fullMeta = surface.props["sets"][3]["meta"].get<std::string>();
        Expect(fullMeta.find("<downloads.problem.storage-full>") != std::string::npos &&
                   fullMeta.find("No space left") == std::string::npos,
               "a full disk is named in our words; the system's text only goes to the log");
        Expect(ShownCount("danger (notice on screen app:downloads): Full disk: No space left on device") == 1,
               "a set's error is logged");
        Expect(ShownCount(
                   "warn (notice on screen app:downloads): Quiet: <downloads.stalled_no_peers>: Tracker x: 404") == 1,
               "a stalled set's reason is logged");
        router.OnEvent({"set-1", "action", "pause"});
        ExpectEqual(Verb(), "APP pause 1", "pause glyph");
        router.OnEvent({"btn-2", "click", ""});
        ExpectEqual(Verb(), "APP import 2", "import button");
        router.OnEvent({"set-1", "menu", ""});
        Expect(surface.props["sheet"]["actions"].size() == 2 &&
                   surface.props["sheet"]["actions"][1]["hold"].get<bool>(),
               "«⋯» lists the rare verbs; delete is held");
        router.OnEvent({"menu-delete", "click", ""});
        ExpectEqual(Verb(), "APP delete 1", "held delete");
        router.SetState(
            "launcher",
            Json{{"screen", "downloads"},
                 {"language", "english"},
                 {"downloads",
                  {{"loaded", true},
                   {"sets", sets},
                   {"dialog",
                    {{"kind", "candidates"},
                     {"id", "1"},
                     {"candidates",
                      {{{"label", "Game"}, {"kind", "loose-game-candidate"}, {"files", 3}, {"bytes", 10}},
                       {{"label", "setup.exe"}, {"kind", "installer-candidate"}, {"files", 2}, {"bytes", 20}}}},
                     {"all", {{"files", 9}, {"bytes", 40}}}}}}}});
        ExpectEqual(surface.props["dialog"]["kind"].get<std::string>(), "pick", "candidates are picked one at a time");
        Expect(surface.props["dialog"]["rows"].size() == 3 &&
                   surface.props["dialog"]["rows"][1]["title"].get<std::string>() ==
                       "setup.exe · <downloads.kind.installer-candidate>",
               "every candidate and all files");
        router.OnEvent({"candidate-1", "click", ""});
        ExpectEqual(Verb(), "APP download 1:1", "one candidate");
        router.OnEvent({"candidate-all", "click", ""});
        ExpectEqual(Verb(), "APP download 1:-1", "every file");
        // Failures in our words by their kind; the host's message only in the log.
        router.SetState(
            "launcher",
            Json{{"screen", "downloads"},
                 {"language", "english"},
                 {"downloads",
                  {{"loaded", true},
                   {"sets", sets},
                   {"error", "Invalid magnet link: it must start with magnet:?"},
                   {"errorKind", "magnet-prefix"},
                   {"dialog",
                    {{"kind", "error"}, {"text", "Cannot open selected document"}, {"errorKind", "open-failed"}}}}}});
        ExpectEqual(surface.props["error"].get<std::string>(), "<downloads.problem.magnet-prefix>",
                    "a failure above the sets in our words");
        ExpectEqual(surface.props["dialog"]["text"].get<std::string>(), "<downloads.problem.open-failed>",
                    "an error dialog in our words");
        Expect(
            ShownCount("danger (notice on screen app:downloads): Invalid magnet link: it must start with magnet:?") ==
                    1 &&
                ShownCount(
                    "danger (notice on screen app:downloads): <downloads.error>: Cannot open selected document") == 1,
            "the host's messages are logged");
        const size_t problemErrors = surface.errors.size();
        router.SetState("launcher",
                        Json{{"screen", "downloads"},
                             {"language", "english"},
                             {"downloads",
                              {{"loaded", true},
                               {"sets", sets},
                               {"dialog", {{"kind", "error"}, {"text", "x"}, {"errorKind", "new-kind"}}}}}});
        Expect(surface.props["dialog"]["text"].get<std::string>() == "<downloads.problem.failed>" &&
                   surface.errors.size() > problemErrors,
               "a kind without our words is an error and reads as the general failure");
    }

    void About()
    {
        router.SetState("launcher",
                        Json{{"screen", "about"},
                             {"language", "english"},
                             {"about",
                              {{"app", {{"source", "https://example.org/seawolves/tree/1"}}},
                               {"notices",
                                {"ui-RmlUi-MIT.txt", "storm-GPLv3.txt", "fmt-LICENSE.rst", "archive-boost_1_90_0.txt",
                                 "bgfx-BSD-2-Clause.txt", "FFmpeg-configure.txt", "FFmpeg-LGPL-2.1.txt",
                                 "font-OFL-GreatVibes.txt", "script-patches.txt"}}}}});
        ExpectEqual(surface.tag, "s-about", "about screen");
        ExpectEqual(surface.props["notices"][0]["title"].get<std::string>(), "Storm Engine",
                    "the engine's licence first");
        {
            std::string listed;
            for (const Json &row : surface.props["notices"])
                listed += row["title"].get<std::string>() + "|" + row["value"].get<std::string>() + ";";
            ExpectEqual(listed,
                        "Storm Engine|GPL-3.0;<about.notice_script_patches>|;bgfx|BSD-2-Clause;Boost "
                        "1.90.0|BSL-1.0;FFmpeg|LGPL-2.1;"
                        "FFmpeg|<about.notice_kind.build>;fmt|MIT;Great Vibes|OFL;RmlUi|MIT;",
                        "notices by name, the licence before its files");
        }
        router.OnEvent({"notice-8", "click", ""});
        ExpectEqual(Verb(), "APP license ui-RmlUi-MIT.txt", "a notice opens");
        Expect(surface.props["sources"].size() == 1, "one repository: one source row");
        router.OnEvent({"source", "click", ""});
        ExpectEqual(Verb(), "APP open-url https://example.org/seawolves/tree/1", "source link");
        ExpectEqual(surface.props["shareLog"]["title"].get<std::string>(), "<about.share_log>",
                    "«Поделиться логом» row");
        router.OnEvent({"share-log", "click", ""});
        ExpectEqual(Verb(), "APP share-log ", "the log goes to the host's share sheet");
        Expect(!surface.props["detailed"]["on"].get<bool>(), "«Подробная диагностика» is off until the host says on");
        router.OnEvent({"diagnostics-detailed", "change", "1"});
        ExpectEqual(Verb(), "APP diagnostics-detailed 1", "the switch goes to the host");
        Expect(surface.props["detailed"]["on"].get<bool>(), "the switch shows the tap at once");
        const auto aboutDetailed = [](bool on) {
            return Json{{"screen", "about"},
                        {"language", "english"},
                        {"about",
                         {{"app", {{"source", "https://example.org/seawolves/tree/1"}, {"detailedDiagnostics", on}}},
                          {"notices", Json::array()}}}};
        };
        router.SetState("launcher", aboutDetailed(false));
        Expect(surface.props["detailed"]["on"].get<bool>(), "a host state from before the tap does not turn it back");
        router.SetState("launcher", aboutDetailed(true));
        router.OnEvent({"diagnostics-detailed", "change", "0"});
        ExpectEqual(Verb(), "APP diagnostics-detailed 0", "off goes to the host");
        router.SetState("launcher", aboutDetailed(false));
        router.SetState("launcher", aboutDetailed(true));
        Expect(surface.props["detailed"]["on"].get<bool>(), "once the host had it, its state shows");
        ExpectEqual(surface.props["shareSave"]["title"].get<std::string>(), "<about.share_save>",
                    "«Отправить сохранение» row");
        router.OnEvent({"save-list", "click", ""});
        ExpectEqual(Verb(), "APP save-list ", "the host lists the saves");
        const auto aboutWithSaves = [](Json saves) {
            return Json{{"screen", "about"},
                        {"language", "english"},
                        {"about", {{"app", Json::object()}, {"notices", Json::array()}, {"saves", saves}}}};
        };
        router.SetState("launcher",
                        aboutWithSaves(Json::array(
                            {Json{{"key", "Player/Map save"}, {"name", "Map save"}, {"date", "14.11.2026 12:00"}},
                             Json{{"key", "Player/Old"}, {"name", "Old"}, {"date", "01.11.2026 09:30"}}})));
        ExpectEqual(surface.props["title"].get<std::string>(), "<about.share_save>",
                    "the save picker has its own title");
        Expect(surface.props["saves"]["rows"].size() == 3, "several saves: «Все сохранения» heads the list");
        ExpectEqual(surface.props["saves"]["rows"][0]["title"].get<std::string>(), "<about.share_all_saves>",
                    "the all-saves row");
        router.OnEvent({"save-all", "click", ""});
        ExpectEqual(Verb(), "APP share-all-saves ", "every save goes to the host's share sheet in one archive");
        Expect(surface.props["saves"]["rows"][1]["title"].get<std::string>() == "Map save" &&
                   surface.props["saves"]["rows"][1]["meta"].get<std::string>() == "14.11.2026 12:00",
               "a save row: name and date, in the host's order");
        router.OnEvent({"save-2", "click", ""});
        ExpectEqual(Verb(), "APP share-save Player/Old", "the picked save goes to the host's share sheet");
        router.OnEvent({"header-back", "click", ""});
        ExpectEqual(Verb(), "APP save-close ", "← closes the picker, not the screen");
        router.SetState("launcher", aboutWithSaves(Json::array()));
        Expect(surface.props["saves"]["rows"].empty() &&
                   surface.props["saves"]["empty"].get<std::string>() == "<about.saves_empty>",
               "no saves: the empty state");
        router.SetState("launcher", aboutWithSaves(Json::array({Json{
                                        {"key", "Player/Only"}, {"name", "Only"}, {"date", "14.11.2026 12:00"}}})));
        Expect(surface.props["saves"]["rows"].size() == 1 &&
                   surface.props["saves"]["rows"][0]["title"].get<std::string>() == "Only",
               "one save: no «Все сохранения» row");
    }

    void RestoreSave()
    {
        // «Восстановить сохранение»: the row asks the host for the picker; its question and result are dialogs.
        const auto aboutRestoring = [](Json restore) {
            return Json{{"screen", "about"},
                        {"language", "english"},
                        {"about", {{"app", Json::object()}, {"notices", Json::array()}, {"restore", restore}}}};
        };
        router.SetState("launcher", aboutRestoring(Json::object()));
        ExpectEqual(surface.props["restoreSave"]["title"].get<std::string>(), "<about.restore_save>",
                    "«Восстановить сохранение» row");
        Expect(surface.props["dialog"].is_null(), "no restore dialog before a file is picked");
        router.OnEvent({"save-restore", "click", ""});
        ExpectEqual(Verb(), "APP restore-pick ", "the host opens the file picker");
        router.SetState("launcher", aboutRestoring(Json{{"busy", true}}));
        ExpectEqual(surface.props["restoreSave"]["meta"].get<std::string>(), "<about.restore.busy>",
                    "a restore in progress shows on its row");
        const size_t sentBusy = surface.actions.size();
        router.OnEvent({"save-restore", "click", ""});
        Expect(surface.actions.size() == sentBusy, "no second picker while a restore runs");
        router.SetState("launcher",
                        aboutRestoring(Json{{"conflict", {{"names", Json::array({"Old"})}, {"total", 1}}}}));
        ExpectEqual(surface.props["dialog"]["title"].get<std::string>(), "<about.restore.conflict_one>",
                    "one save already there: its name");
        {
            std::string ids;
            for (const Json &action : surface.props["dialog"]["actions"])
                ids += action["id"].get<std::string>() + "|" + action["label"].get<std::string>() + ";";
            ExpectEqual(ids,
                        "restore-cancel|<common.cancel>;restore-replace|<about.restore.replace>;restore-keep|<about."
                        "restore.keep_both>;",
                        "cancel, replace, keep both");
        }
        router.OnEvent({"restore-replace", "click", ""});
        ExpectEqual(Verb(), "APP restore-replace ", "replace");
        router.OnEvent({"restore-keep", "click", ""});
        ExpectEqual(Verb(), "APP restore-keep ", "keep both");
        router.OnEvent({"restore", "dismiss", ""});
        ExpectEqual(Verb(), "APP restore-cancel ", "the scrim cancels the question");
        router.SetState(
            "launcher",
            aboutRestoring(Json{{"conflict", {{"names", Json::array({"A", "B", "C", "D", "E"})}, {"total", 9}}}}));
        Expect(surface.props["dialog"]["title"].get<std::string>() == "<about.restore.conflict_many>" &&
                   surface.props["dialog"]["text"].get<std::string>() == "A, B, C, D, …",
               "several saves already there: how many, the first names");
        router.SetState("launcher",
                        aboutRestoring(Json{{"result", {{"key", "no_space"}, {"args", Json::array({12})}}}}));
        Expect(surface.props["dialog"]["title"].get<std::string>() == "<about.restore.failed_title>" &&
                   surface.props["dialog"]["text"].get<std::string>() == "<about.restore.no_space>",
               "a refusal: its reason");
        router.OnEvent({"restore-close", "click", ""});
        ExpectEqual(Verb(), "APP restore-close ", "the result closes");
        router.SetState("launcher",
                        aboutRestoring(Json{{"result", {{"key", "done"}, {"args", Json::array({"Old (2)"})}}}}));
        ExpectEqual(surface.props["dialog"]["title"].get<std::string>(), "<about.restore.done_title>",
                    "the restored save");
        const size_t restoreErrors = surface.errors.size();
        router.SetState("launcher", aboutRestoring(Json{{"result", {{"key", "mystery"}, {"args", Json::array()}}}}));
        Expect(surface.props["dialog"]["text"].get<std::string>() == "<about.restore.failed>" &&
                   surface.errors.size() > restoreErrors,
               "a result without our words is an error and reads as the general failure");
    }

    void GameIds()
    {
        const size_t errors = surface.errors.size();
        router.SetState("launcher", Json{{"screen", "nowhere"}});
        Expect(surface.errors.size() == errors + 1, "an unknown application screen is an error");

        // Game interface ids are ini stems: the game's credits (about.ini) are "about" and must
        // never reach the application's about screen.
        for (const auto &entry : PortedScreens())
            Expect(std::string_view(entry.id).find(':') == std::string_view::npos ||
                       std::string_view(entry.id).starts_with("app:"),
                   std::string("only application screens use a ':' id: ") + entry.id);
        InterfaceModel game;
        game.screen = "about";
        game.opening = 1;
        game.revision = 1;
        router.SetInterface(game);
        Expect(router.Using() == "generic", "the game's about interface is not the application's about screen");
    }
};

static void Launcher()
{
    LauncherRun run;
    run.SetupView();
    run.ImportResults();
    run.Notices();
    run.Installing();
    run.DownloadStates();
    run.DownloadSets();
    run.About();
    run.RestoreSave();
    run.GameIds();
}

static void Games(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    // Dice, our reroll: two dice marked (already paid), the cup throws them.
    InterfaceModel reroll = Load(models, "boal_dice-reroll.json");
    router.SetInterface(reroll);
    Expect(router.Using() == "boal_dice" && surface.tag == "s-table-game" && router.Rejection().empty() &&
               surface.errors.empty(),
           "boal_dice is drawn by the table-game screen");
    Expect(surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
           "every exported node is known");
    Expect(surface.props["kind"] == "dice" && surface.props["locked"] == true, "back is locked during a round");
    Expect(surface.props["stats"][0]["value"] == "200" && surface.props["stats"][1]["value"] == "7 600",
           "stake and our money");
    Expect(surface.props["table"]["opponent"] == "Жак Дюбуа" &&
               surface.props["table"]["portrait"] == "INTERFACES/PORTRAITS/128/face_377" &&
               surface.props["table"]["bank"] == "2 400" && surface.props["table"]["coins"] == "silver",
           "opponent, portrait, bank and the coins the script lays");
    const Json ours = surface.props["bottom"]["pieces"];
    Expect(ours.size() == 5 && ours[0]["pips"].size() == 3 && ours[0]["action"] == "mark" &&
               ours[0]["disabled"] == false,
           "our dice: faces from the script pictures, tappable");
    Expect(ours[3]["lift"] == true && ours[3]["pips"].size() == 6 && ours[3]["cost"] == "−200" &&
               ours[3].count("action") == 0,
           "a marked die keeps its face, rises, shows the stake and cannot be unmarked");
    Expect(surface.props["bottom"]["label"] == "" && surface.props["top"]["label"] == "<games.combo_6>",
           "the combination of a complete row");
    Expect(surface.props["buttons"].size() == 1 && surface.props["buttons"][0]["label"] == "<games.roll>" &&
               surface.props["buttons"][0]["sub"] == "2 <games.die_few>" &&
               surface.props["buttons"][0]["disabled"] == false,
           "throw the two marked dice");
    router.OnEvent({"bottom", "mark", "2"});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_HeroDice2\t-1\tpress", "a tap on our die marks it in the script");
    router.OnEvent({"throw", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_PACK\t-1\tpress", "the primary throws the cup");
    router.OnEvent({"pack", "press", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_PACK\t-1\tpress", "the cup itself throws too");
    const size_t sent = surface.commands.size();
    router.OnEvent({"header-back", "click", ""});
    Expect(surface.commands.size() == sent, "no way out during a round");

    // Nothing marked in our reroll: keep the dice (B_ICON_1).
    InterfaceModel keep = reroll;
    for (Widget &widget : keep.widgets)
        if (widget.node == "ANDROID_TABLE_GAME")
            widget.context["heroMix"] = "0,0,0,0,0";
    keep.revision = 2;
    router.SetInterface(keep);
    Expect(surface.props["buttons"][0]["label"] == "<games.keep>", "nothing marked: keep the dice");
    router.OnEvent({"throw", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_ICON_1\t-1\tpress", "keeping is the script's pass");

    // The script pays the winner before it opens the table: the money rises while the round is still closed.
    InterfaceModel paid = reroll;
    for (Widget &widget : paid.widgets)
        if (widget.node == "ANDROID_TABLE_GAME")
            widget.context["moneyHero"] = "10000";
    paid.revision = 3;
    router.SetInterface(paid);

    // The round ends with our win: the bank is ours, another round or leave.
    InterfaceModel result = Load(models, "boal_dice-result.json");
    result.revision = 4;
    router.SetInterface(result);
    Expect(surface.props["locked"] == false && surface.props["table"]["bankLabel"] == "<games.won>" &&
               surface.props["table"]["bank"] == "+2 400" && surface.props["table"]["bankTone"] == "win",
           "after the round: what we won");
    Expect(surface.props["bottom"]["label"] == "<games.combo_4>" &&
               surface.props["bottom"]["pieces"][0].count("action") == 0,
           "our combination; dice are not tappable between rounds");
    Expect(surface.props["buttons"][1]["id"] == "again" && surface.props["buttons"][1]["variant"] == "primary" &&
               surface.props["buttons"][1]["sub"] == "<games.stake_short> 200",
           "another round is the primary");
    router.OnEvent({"again", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_PACK\t-1\tpress", "another round is B_PACK");
    router.OnEvent({"leave", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tEXIT_BTN\t-1\tpress", "leave the table");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tEXIT_BTN\t-1\tpress", "back leaves between rounds");

    // The opponent has no money left: only leaving.
    router.SetInterface(Load(models, "boal_dice-end.json"));
    Expect(surface.props["buttons"][0]["id"] == "again" && surface.props["buttons"][0]["disabled"] == true &&
               surface.props["buttons"][1]["id"] == "leave" && surface.props["buttons"][1]["variant"] == "primary",
           "the opponent quits: leaving is the primary");

    // A fact the script lacks is an error, never a guess.
    InterfaceModel broken = Load(models, "boal_dice-reroll.json");
    broken.widgets.back().unavailable_reason = "table-game:missing=heroDice";
    surface.errors.clear();
    router.SetInterface(broken);
    Expect(surface.errors.size() == 1, "a missing script fact is reported");

    // Cards, our turn.
    FakeSurface cards;
    Router table(cards);
    table.SetInterface(Load(models, "boal_cards.json"));
    Expect(table.Using() == "boal_cards" && cards.tag == "s-table-game" && cards.errors.empty() &&
               cards.props["kind"] == "cards",
           "boal_cards is drawn by the table-game screen");
    Expect(cards.props["table"]["coins"] == "gold" && cards.props["pack"][0]["kind"] == "deck", "gold coins, the deck");
    const Json hand = cards.props["bottom"]["pieces"];
    Expect(hand.size() == 3 && hand[0]["rank"] == "7" && hand[0]["suit"] == "spades" && hand[0]["red"] == false &&
               hand[1]["rank"] == "<games.rank_J>" && hand[1]["red"] == true,
           "our cards: rank and suit from the picture names");
    Expect(cards.props["bottom"]["label"] == "19 <games.point_many>", "our points (7 + J 2 + 10)");
    {
        FakeSurface modded;
        Router other(modded);
        InterfaceModel deck = Load(models, "boal_cards.json");
        for (Widget &widget : deck.widgets)
            if (widget.node == "PCard1")
                widget.context["points"] = "3"; // a deck that counts the jack 3
        other.SetInterface(deck);
        Expect(modded.props["bottom"]["label"] == "20 <games.point_many>", "the points are the game's count, not ours");
        deck.revision++;
        for (Widget &widget : deck.widgets)
            if (widget.node == "PCard1")
                widget.context.erase("points");
        other.SetInterface(deck);
        Expect(modded.errors.size() == 1, "a card the game counts nothing for is reported");
    }
    Expect(cards.props["top"]["pieces"].size() == 2 && cards.props["top"]["pieces"][0]["kind"] == "back" &&
               cards.props["top"]["label"] == "",
           "the opponent's cards face down, no points");
    Expect(cards.props["buttons"][0]["id"] == "stand" && cards.props["buttons"][1]["id"] == "take" &&
               cards.props["buttons"][1]["sub"] == "−500" && cards.props["buttons"][1]["disabled"] == false,
           "stand, or take a card for the stake");
    table.OnEvent({"take", "click", ""});
    ExpectEqual(cards.Last(), "widget:boal_cards\tB_PACK\t-1\tpress", "take a card is B_PACK");
    table.OnEvent({"stand", "click", ""});
    ExpectEqual(cards.Last(), "widget:boal_cards\tB_ICON_1\t-1\tpress", "stand is B_ICON_1");

    // The opponent's turn: nothing to press.
    InterfaceModel opponent = Load(models, "boal_cards-opponent.json");
    opponent.revision = 2;
    table.SetInterface(opponent);
    Expect(cards.props["buttons"][0]["disabled"] == true && cards.props["buttons"][1]["disabled"] == true &&
               cards.props["buttons"][1]["reason"] == "<games.wait>",
           "the opponent plays: both buttons wait");

    // Opened: he has 20, the bank is his.
    InterfaceModel opened = Load(models, "boal_cards-result.json");
    opened.revision = 3;
    table.SetInterface(opened);
    Expect(cards.props["top"]["label"] == "20 <games.point_many>" &&
               cards.props["top"]["pieces"][0]["rank"] == "<games.rank_A>",
           "the opponent's cards open with his points");
    Expect(cards.props["table"]["bankLabel"] == "<games.taken> Жак" && cards.props["table"]["bank"] == "3 000" &&
               cards.props["table"]["bankTone"] == "loss",
           "the bank went to the opponent");
    Expect(cards.props["buttons"][1]["id"] == "again" && cards.props["buttons"][1]["disabled"] == false,
           "another round");
}

static void GoodsOrder(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    InterfaceModel list = Load(models, "goodstransfer.json");
    router.SetInterface(list);
    Expect(router.Using() == "goodstransfer" && surface.tag == "s-goods-order" && router.Rejection().empty() &&
               surface.errors.empty(),
           "goodstransfer is drawn by the purchase list");
    Expect(surface.props["extras"]["rows"].empty(), "every exported node is known");
    const Json rows = surface.props["list"]["rows"];
    Expect(rows.size() == 6 && rows[0]["name"] == "Ядра" && rows[0]["image"]["texture"] == "atlas:GOODS/Balls" &&
               rows[0]["cells"][0] == "400" && rows[0]["cells"][1] == "600" && rows[4]["cells"][0] == "—",
           "one row per good: picture, in the hold, the order");
    Expect(rows[0].count("q") == 0, "no quantity until a good is opened");
    Expect(surface.props["subject"]["label"] == "Люггер 'Ласточка'" &&
               surface.props["subject"]["sub"] == "1 <common.of> 2" && surface.props["subject"]["prev"] == false &&
               surface.props["subject"]["next"] == true,
           "the ship the list is for, 1 of 2");
    Expect(surface.props["contraband"]["label"] == "Закупать контрабанду" && surface.props["contraband"]["on"] == false,
           "contraband switch");

    router.OnEvent({"goods", "row", "3"});
    const Json open = surface.props["list"]["rows"][3];
    Expect(open["selected"] == true && open["q"]["value"] == 400 && open["q"]["min"] == 0 && open["q"]["max"] == 1000 &&
               open["q"]["label"] == "<goods.order>" && open["q"]["all"] == false,
           "a tap opens the good's order as a quantity");
    Expect(surface.commands.empty() && surface.props["primary"].is_null(),
           "opening a row sends nothing and offers nothing to save");
    router.OnEvent({"goods", "change", "500"});
    Expect(surface.props["list"]["rows"][3]["cells"][1] == "500", "the amount shows at once");
    {
        // The treasurer's screen gets the button too: the standing order is the same draft flow, saved by «Сохранить
        // N».
        const std::string order = "widget:goodstransfer\tANDROID_GOODS_ORDER\t3\torder@5:Powder=";
        DraftFlow(
            "goodstransfer", router, surface, "goods", 520, "<goods.save> 520",
            [](const std::string &command) { return command.find("\torder@") != std::string::npos; },
            [&](const std::vector<std::string> &commands) {
                return commands == std::vector<std::string>{order + "520"};
            },
            [] {});
        Expect(surface.props["list"]["rows"][3]["cells"][1] == "520",
               "the saved amount shows until the game's order catches up");
        InterfaceModel saved = list;
        for (Widget &widget : saved.widgets)
            if (widget.node == "ANDROID_GOODS_ORDER" && widget.row_id == "Powder")
                widget.context["order"] = "520";
        saved.revision += 5;
        router.SetInterface(saved);
        Expect(surface.props["list"]["rows"][3]["q"]["value"] == 520 && surface.props["primary"].is_null(),
               "the game's new order: nothing left to save");
        router.OnEvent({"goods", "change", "300"});
        surface.commands.clear();
        router.OnEvent({"header-back", "click", ""});
        Expect(surface.commands.empty() && surface.props["list"]["rows"][3]["q"]["value"] == 520,
               "← first drops a changed order, saving nothing");
        router.SetInterface(list);
    }
    router.OnEvent({"contraband", "change", "1"});
    ExpectEqual(surface.Last(), "widget:goodstransfer\tCHECK_BUYCONTRABAND\t0\t1", "the contraband switch");
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), "widget:goodstransfer\tBTN_CHOOSESHIP_RIGHT\t-1\tpress",
                "the next ship through the game's arrow");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:goodstransfer\tEXIT_BTN\t-1\tpress", "← leaves");

    // The game's own amount window: that good is open, the list takes no orders, ← closes the window.
    FakeSurface surface2;
    Router window(surface2);
    window.SetInterface(Load(models, "goodstransfer-window.json"));
    const Json powder = surface2.props["list"]["rows"][3];
    Expect(window.Using() == "goodstransfer" && powder["selected"] == true && powder["q"]["disabled"] == true &&
               surface2.props["list"]["rows"][0]["disabled"] == true,
           "the game's window marks its good and locks the list");
    window.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface2.Last(), "widget:goodstransfer\tTG_CANCEL_BUTTON\t-1\tpress",
                "← closes the game's amount window first");
}

static void SailEmblem(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    router.SetInterface(Load(models, "sailsgerald.json"));
    Expect(router.Using() == "sailsgerald" && surface.tag == "s-sail-emblem" && router.Rejection().empty() &&
               surface.errors.empty(),
           "sailsgerald is drawn by the sail emblem screen");
    Expect(surface.props["extras"]["rows"].empty(), "every exported node is known");
    const Json emblems = surface.props["emblems"];
    Expect(emblems.size() == 7 && emblems[2]["selected"] == true && emblems[0]["applied"] == true &&
               emblems[2]["texture"] == "ships/gerald/emblem_03",
           "emblems with their textures, the chosen one and the one on the sails");
    Expect(surface.props["title"] == "Выбрать символику на парус" && surface.props["stats"][0]["value"] == "12 480",
           "the game's caption and the hero's money");
    Expect(surface.props["primary"]["label"] == "<sails.apply>" && surface.props["primary"]["sub"] == "1 250" &&
               surface.props["primary"]["disabled"] == false,
           "another emblem chosen: paint it for the price");
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tB_OK\t-1\tpress", "paint");
    router.OnEvent({"emblems", "emblem", "5"});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tSCROLL_GERALD\t5\tselect@5:emblem_06.tga.tx",
                "a tap chooses that emblem in the game");
    router.OnEvent({"emblem-prev", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tSCROLL_GERALD\t1\tselect@5:emblem_02.tga.tx",
                "‹ chooses the previous one");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tEXIT_BTN\t-1\tpress", "← leaves");

    // The emblem on the sails chosen: the game offers B_REMOVE. Less money than the price does not stop it (the game
    // charges without asking).
    router.SetInterface(Load(models, "sailsgerald-applied.json"));
    Expect(surface.props["primary"]["label"] == "<sails.remove>" && surface.props["primary"]["disabled"] == false &&
               surface.props["prev"] == false,
           "the current emblem: take it off, whatever the purse: " + surface.props["primary"].dump());
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tB_REMOVE\t-1\tpress", "take off");
}

// The one reading of a number in a game caption (screens/amount.h) and the error line when a caption that always
// carries one comes without it.
static void Amounts(const std::string &models)
{
    const auto read = [](const char *text) { return ParseAmount(text); };
    Expect(read("23 824") == 23824 && read("11\u00a0575") == 11575 && read("1 240 pesos") == 1240,
           "thousands grouped by a space or a no-break space");
    Expect(read("Наши деньги 1 500 песо") == 1500 && read("Стоимость найма одного матроса 23 песо") == 23,
           "the number inside a caption");
    Expect(read("4.6 / 140") == 4 && read("317 / 700") == 317 && read("40 : 9 / 40") == 40 && read("100%") == 100,
           "the first number ends at any other character");
    Expect(read("5 10") == 5 && read("1 2345") == 1 && read("12 345 678") == 12345678,
           "a thousands group is exactly three digits");
    Expect(read("-3") == -3 && read("Бонус -12") == -12 && read("- 3") == 3 && read("x-1 000") == -1000,
           "a minus sign right before the first digit");
    Expect(!read("") && !read("-") && !read("\xE2\x80\x94") && !read("песо"), "no digit: no number");
    Expect(read("2147483647") == 2147483647 && !read("2147483648") && !read("99 999 999 999"),
           "a number past int is no number");

    FakeSurface surface;
    Router router(surface);
    InterfaceModel model = Load(models, "hirecrew.json");
    for (Widget &widget : model.widgets)
        if (widget.node == "TAVERN_PRICE")
            widget.label = "\xE2\x80\x94";
    router.SetInterface(model);
    model.revision += 1;
    router.SetInterface(model);
    Expect(surface.errors.size() == 1 && surface.errors[0].find("TAVERN_PRICE") != std::string::npos,
           "a price caption without its number is an error line, once: " + Json(surface.errors).dump());

    Expect(ParseNumber<int>("12") == 12 && ParseNumber<int>("-3") == -3 && ParseNumber<double>("0.25") == 0.25,
           "a number written alone");
    Expect(!ParseNumber<int>("") && !ParseNumber<int>("3a") && !ParseNumber<int>(" 3") && !ParseNumber<int>("+3") &&
               !ParseNumber<int>("2147483648") && !ParseNumber<size_t>("-1") && !ParseNumber<double>("inf") &&
               !ParseNumber<int>("1.5"),
           "anything else is no number");

    // A row event whose value is no number: an error line naming it, and nothing done (atoi made it row 0).
    FakeSurface goods;
    Router list(goods);
    list.SetInterface(Load(models, "goodstransfer.json"));
    list.OnEvent({"goods", "row", "x"});
    Expect(goods.errors.size() == 1 && goods.errors[0].find("goods row: 'x'") != std::string::npos &&
               goods.props["list"]["rows"][0]["selected"] != true && goods.commands.empty(),
           "a row event without a number opens no row: " + Json(goods.errors).dump());
}

// A disabled control's tap reports "refused": a handler bound to every verb of its id (its action) does not run
// (the boal dice's «Ещё раз» sent its press while disabled), a handler bound to "refused" does, unbound it is no error.
struct RefusedScreen final : Screen
{
    static inline int actions = 0, refusals = 0;
    const char *Component() const override
    {
        return "s-generic";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return {};
    }
    Json Build(const Admission &, Bindings &bindings) override
    {
        bindings.On("again", "", [](const Event &) { ++actions; });
        bindings.On("learn", "", [](const Event &) { ++actions; });
        bindings.On("learn", "refused", [](const Event &) { ++refusals; });
        return Json::object();
    }
};

static void Refused()
{
    static const ScreenEntry entries[] = {{"refused_test", [] { return std::unique_ptr<Screen>(new RefusedScreen); }}};
    FakeSurface surface;
    Router router(surface, entries);
    InterfaceModel model;
    model.screen = "refused_test";
    model.opening = 1;
    model.revision = 1;
    router.SetInterface(model);
    Expect(router.Maps("again", "click") && !router.Maps("again", "refused") && router.Maps("learn", "refused"),
           "an every-verb handler maps taps, not a disabled control's refusal");
    router.OnEvent({"again", "refused", ""});
    Expect(RefusedScreen::actions == 0 && surface.errors.empty(),
           "a disabled control's refusal does not run its action and is no error");
    router.OnEvent({"learn", "refused", ""});
    Expect(RefusedScreen::refusals == 1 && RefusedScreen::actions == 0, "a handler bound to refused gets it");
    router.OnEvent({"again", "click", ""});
    Expect(RefusedScreen::actions == 1, "the enabled tap runs the action");
}

int main(int argc, char **argv)
{
    const std::string models = argc > 1 ? argv[1] : "tests/models";
    app_pack = argc > 2 ? argv[2] : "";
    if (argc > 3 && std::string(argv[3]) == "items")
    {
        Items(models);
        ItemCategories(models);
        return failures ? 1 : 0;
    }
    Amounts(models);
    Refused();
    Admission_();
    Roles();
    Commands();
    Generic(models);
    Predictions(models);
    SectionPrediction(models);
    Store(models);
    Commerce(models);
    Crew(models);
    Shipyard(models);
    Atlas();
    DialogueChannel(models);
    Games(models);
    Repair(models);
    Transfer(models);
    TransferCaptain(models);
    Alchemy(models);
    GoodsOrder(models);
    SailEmblem(models);
    DialogueText(models);
    DialogueOverClosingInterface(models);
    DialogueNumber(models);
    Nations(models);
    Hero(models);
    HeroPeople(models);
    Items(models);
    ItemCategories(models);
    Ship(models);
    OpenOnSubject(models);
    Journal(models);
    Launcher();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("screen routing checks passed\n");
    return failures ? 1 : 0;
}
