// Host checks for screen routing: admission (KNOWN / REQUIRED / extras), the generic
// classification and the mapping of component events to engine actions. Synthetic
// models only (tests/models); no GL context, no game data.
#include "fakes.h"
#include "../tools/script_game.h"
#include "../src/hud/hud.h"
#include "../src/picture_atlas.h"
#include "../src/screens/amount.h"
#include "../src/screens/controls.h"
#include "storm_ui/dialogue_state.h"

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
    ExpectEqual(to_string(action), "widget:store\tTABLE_LIST\t3\tselect@7:row-3", "versioned select");
    action.value.clear();
    ExpectEqual(to_string(action), "widget:store\tTABLE_LIST\t3\tselect", "plain select");
    action.verb = "toggle";
    action.value = "1";
    ExpectEqual(to_string(action), "widget:store\tTABLE_LIST\t3\t1", "check value");
    action.verb = "set";
    action.value = "25";
    ExpectEqual(to_string(action), "widget:store\tTABLE_LIST\t3\tset@7:25", "slider position");
    action.verb = "press";
    action.value.clear();
    action.guard = "row-3";
    ExpectEqual(to_string(action), "widget:store\tTABLE_LIST\t3\tguard@7:row-3|press", "guarded press");
    Action back;
    back.screen = "store";
    back.verb = "back";
    ExpectEqual(to_string(back), "back", "global back");
    Action plain;
    plain.node = "button-1";
    plain.verb = "click";
    ExpectEqual(to_string(plain), "", "an action of a plain document is no engine command");
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
    Scripted(router);
    InterfaceModel model = Load(models, "tradebook.json");
    router.SetInterface(model);
    Expect(surface.props["headerSection"] == "tradebook" && router.Predicted() == 0,
           "the trade book's own section is selected");

    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:tradebook\tI_SHIP\t-1\tpress", "the tap presses the ship section's button");
    router.Tick();
    Expect(router.Predicted() == 1 && surface.props["headerSection"] == "ship",
           "the tap's own build selects the ship section");
    AnswerAll(router, surface, true);
    router.SetInterface(model); // the engine's model after its answer: the trade book is still the game's interface
    Expect(surface.props["headerSection"] == "ship" && surface.errors.empty() && router.Answering(),
           "the applied press is awaited until the next interface");
    Expect(surface.props.contains("towns") && !surface.props["towns"].empty(), "the trade book's body stays meanwhile");
    const size_t sent = surface.actions.size();
    router.OnEvent({"header", "section", "items"});
    router.Tick();
    Expect(surface.actions.size() == sent && surface.props["headerSection"] == "ship" && surface.errors.empty(),
           "another section tapped while the ship opens is not sent nor drawn");
    InterfaceModel ship = Load(models, "ship.json");
    router.SetInterface(ship);
    Expect(surface.tag == "s-ship" && surface.props["headerSection"] == "ship" && surface.errors.empty() &&
               !router.Answering(),
           "the ship screen replaces it with the same section selected");

    router.SetInterface(model);
    router.OnEvent({"header", "section", "items"});
    router.Tick();
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
            // Gold has no weight (the fixture game's item 4 weighs nothing): itemsbox.c writes "-" in both weight
            // columns of its row, and the game shows it so.
            InterfaceModel gold = Load(models, "itemsbox-deal.json");
            Widget coins;
            for (Widget &widget : gold.widgets)
                if (widget.node == "TABLE_LIST" && widget.value == "1")
                {
                    widget.value = "0";
                    coins = widget;
                }
            coins.value = "1";
            coins.index = 4;
            coins.row_id = "row:4";
            for (Cell &cell : coins.cells)
                if (cell.column == 1 || cell.column == 3)
                    cell.text = "-";
            gold.widgets.push_back(coins);
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
        {
            // One piece weighs what the game's item says (the fixture's item 5: 0.45), not the shown total of one
            // decimal divided by the count (1.4 / 3).
            InterfaceModel fine = Load(models, "itemsbox-deal.json");
            Widget potions;
            for (Widget &widget : fine.widgets)
                if (widget.node == "TABLE_LIST" && widget.value == "1")
                {
                    widget.value = "0";
                    potions = widget;
                }
            potions.value = "1";
            potions.index = 5;
            potions.row_id = "row:5";
            for (Cell &cell : potions.cells)
            {
                if (cell.column == 0)
                    cell.text = "3";
                if (cell.column == 1)
                    cell.text = "1.4";
            }
            fine.widgets.push_back(potions);
            FakeSurface fine_surface;
            Router box(fine_surface);
            Scripted(box);
            box.SetInterface(fine);
            Json facts = Json::array();
            for (const Json &row : fine_surface.props["list"]["rows"])
                if (row["selected"] == true)
                    facts = row["facts"];
            Expect(facts.size() == 1 && facts[0]["v"] == "0,45",
                   "itemsbox: a piece's weight is the game's item's: " + facts.dump());
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
        // The row's own facts: the store's trade type of its good (the fixture game's refStore); the open row's
        // prices, weight and pack as the dock's facts.
        router.SetInterface(Load(models, "store.json"));
        router.SetInterface(Load(models, "store-deal.json"));
        ExpectEqual(surface.props["list"]["rows"][5]["meta"], "<trade.export>",
                    "the store's trade type is named from the string table");
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
    Scripted(router);
    router.SetInterface(Load(models, "hirecrew.json"));
    Expect(router.Using() == "hirecrew" && surface.tag == "s-crew" && surface.errors.empty(),
           "hirecrew is drawn by the crew screen: " + Json(surface.errors).dump());
    Json row = surface.props["list"]["rows"][0];
    Expect(row["q"]["value"] == 40 && row["q"]["max"] == 65 && row["cells"][2]["text"] == "23",
           "crew on the ship, the rail over the ship's and the tavern's men, price per man: " + row.dump());
    Expect(surface.props["primary"].is_null(), "nothing to confirm yet: no deal button");
    {
        // Both crews: the game's captions and words, each crew's own figures (the fixture game's characters).
        const Json &ship = surface.props["ship"], &tavern = surface.props["tavern"];
        Expect(ship["groups"].size() == 3 && ship["groups"][0]["label"] == "Матросы" &&
                   ship["groups"][0]["value"] == 71 && ship["groups"][0]["word"] == "опытные" &&
                   ship["groups"][2]["value"] == 18 && ship["morale"] == 62 && ship["moraleWord"] == "Хорошая" &&
                   ship["moraleLabel"] == "<ship.morale>",
               "the ship's crew: experience bars and morale with the game's words: " + ship.dump());
        Expect(tavern["groups"].size() == 3 && tavern["groups"][0]["value"] == 22 &&
                   tavern["groups"][0]["word"] == "неопытные" && tavern["groups"][1]["value"] == 30 &&
                   tavern["morale"] == 55,
               "the tavern's men: the colony's crew: " + tavern.dump());
    }
    // hirecrew.json: 40 on the ship, 25 in the tavern, 23 a man; the window started from 40 (the game's
    // TavernBak). The amount goes to the game (hirecrew.c ChangeQTY_EDIT clamps it and sets it on both crews); the
    // screen shows what the game answered, not a limit or a cost of its own.
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
           "the game's clamp and cost: «Нанять 21» for 483: " + surface.props["primary"].dump() + crew_q().dump() +
               surface.props["stats"].dump());
    surface.commands.clear();
    router.OnEvent({"primary", "click", ""});
    router.Tick();
    Expect(!surface.commands.empty() &&
               surface.commands.back() == "widget:hirecrew\tQTY_OK_BUTTON\t-1\tguard@7:ship:0|press",
           "«Нанять 21» is the game's OK, bound to the ship: " + Json(surface.commands).dump());
    // The game took the 21 men on: its backup is the crew it confirmed (the fixture keeps the 40 it started from,
    // so the screen is put back on a fresh window of 40).
    answer(40, 25, "");
    surface.commands.clear();
    router.OnEvent({"crew", "commit", "35"});
    router.Tick();
    Expect(surface.commands.size() == 2 && surface.commands[1] == "widget:hirecrew\tQTY_EDIT\t-1\tguard@7:ship:0|-5",
           "left of the pivot fires: a negative amount: " + Json(surface.commands).dump());
    answer(35, 30, "");
    Expect(surface.props["primary"]["label"] == "<crew.fire> 5" && surface.props["primary"]["sub"] == "",
           "the game fired five, at no cost: " + surface.props["primary"].dump());
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), "widget:hirecrew\tSHIPS_SCROLL\t1\tselect@7:ship:1", "the next ship");
    answer(35, 30, "");

    InterfaceModel broken = Load(models, "hirecrew.json");
    broken.widgets.erase(std::remove_if(broken.widgets.begin(), broken.widgets.end(),
                                        [](const Widget &w) { return w.node == "QTY_EDIT"; }),
                         broken.widgets.end());
    broken.opening = 9;
    router.SetInterface(broken);
    surface.now += 1.0;
    router.Tick();
    Expect(!router.Active() && router.Rejection() == "QTY_EDIT(edit)" && surface.errors.size() == 1,
           "without its edit box the crew screen is rejected loudly: " + router.Rejection() +
               Json(surface.errors).dump());
}

static void Repair(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    InterfaceModel model = Load(models, "repair.json");
    router.SetInterface(model);
    Expect(router.Using() == "repair" && surface.tag == "s-repair" && surface.errors.empty(),
           "repair is drawn by the repair screen: " + Json(surface.errors).dump());
    const Json &parts = surface.props["parts"];
    Expect(parts.size() == 3 && parts[0]["q"]["value"] == 75 && parts[0]["q"]["min"] == 0 &&
               parts[0]["q"]["max"] == 100 && parts[0]["q"]["floor"] == 62 && !parts[0]["q"].contains("ceiling"),
           "hull: 75 % on the 0…100 % scale from its state, 62 % (75 shown less the 13 the game's order has set); the "
           "game stops the arrows: " +
               parts.dump());
    ExpectEqual(parts[0]["q"]["allLabel"], "<common.all>", "fill to the end is «Все», as everywhere");
    Expect(parts[2]["short"] == true && parts[2]["need"] == "<repair.no_sailcloth>", "sails without sailcloth say so");
    Expect(surface.props["stock"][0]["value"] == "40" && surface.props["stock"][1]["value"] == "0",
           "planks and sailcloth in the squadron's holds, the game's");
    ExpectEqual(surface.props["primary"]["sub"], "6 <save.hours> · <repair.planks> 16",
                "repair button: time and the planks the game says the order takes");
    std::string prefix = "widget:repair\t";

    surface.commands.clear();
    router.OnEvent({"hull", "commit", "78"});
    router.Tick();
    Expect(surface.commands.size() == 3 && surface.commands[0] == prefix + "REPAIR_RIGHT_H\t-1\tpress",
           "+3 % = three presses of the right arrow: " + Json(surface.commands).dump());
    // The game's arrows stopped at 77 (the planks ran out): the row shows where it stopped.
    const auto answer = [&](const char *node, const char *label) {
        model.revision += 1;
        Find(model, node, "text")->label = label;
        AnswerAll(router, surface, true);
        router.SetInterface(model);
    };
    answer("REPAIR_QTY_H", "77%");
    Expect(surface.props["parts"][0]["q"]["value"] == 77 && surface.errors.empty(),
           "the game's order is shown: " + surface.props["parts"][0]["q"].dump());
    surface.commands.clear();
    router.OnEvent({"hull", "change", "70"});
    router.Tick();
    Expect(surface.commands.size() == 7 && surface.commands[0] == prefix + "REPAIR_LEFT_H\t-1\tpress",
           "70 from the game's 77: seven presses of the left arrow: " + Json(surface.commands).dump());
    answer("REPAIR_QTY_H", "70%");
    surface.commands.clear();
    router.OnEvent({"masts", "commit", "3"});
    router.Tick();
    Expect(surface.commands.size() == 1 && surface.commands[0] == prefix + "REPAIR_RIGHT_M\t-1\tpress",
           "masts move all at once: one press");
    answer("REPAIR_QTY_M", "3/3");

    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_OK\t-1\tpress", "repair");
    AnswerAll(router, surface, true);
    router.SetInterface(model);
    router.OnEvent({"all-ships", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_ALL_SQUADRON\t-1\tpress", "all ships");
    AnswerAll(router, surface, true);
    router.SetInterface(model);
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), prefix + "SHIPS_SCROLL\t1\tselect@13:ship:1", "the next ship");
    AnswerAll(router, surface, true);
    router.SetInterface(model);
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_CANCEL\t-1\tpress", "back cancels");
}

// The ship exchange on its script screen (screens/transfer/transfer.c; its props and calls per model are
// engine/tests/ui_scripts/cases/transfer.json): what the router does with a game that answers across models.
struct TransferRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    const std::string prefix = "widget:transfer_main\t";
    explicit TransferRun(std::string path) : models(std::move(path))
    {
        Scripted(router);
    }

    size_t Count(const std::string &command) const
    {
        return std::count(surface.commands.begin(), surface.commands.end(), prefix + command);
    }

    void Tabs()
    {
        router.SetInterface(Load(models, "transfer-loot.json"));
        Expect(router.Using() == "transfer_main" && surface.tag == "s-transfer" && surface.errors.empty() &&
                   surface.props["tabs"].size() == 3 && surface.props["ships"].is_null(),
               "loot is drawn by the transfer script screen, cargo open: " + Json(surface.errors).dump());
        surface.commands.clear();
        router.OnEvent({"header", "tab", "2"});
        Expect(surface.commands.empty() && surface.props["tabs"][2]["selected"] == true &&
                   surface.props["ships"]["rows"][0]["icon"] == "hull",
               "the ships tab is ours: nothing is sent, the figures are listed");
        router.OnEvent({"header", "tab", "0"});
        Expect(surface.props["ships"].is_null() && surface.props["tabs"][0]["selected"] == true, "back to the cargo");
        // «Корабли» from the crew window: the game closes it first, the models until it has still show it open.
        InterfaceModel crew = Load(models, "transfer-crew.json");
        crew.revision = 100;
        router.SetInterface(crew);
        router.OnEvent({"header", "tab", "2"});
        ExpectEqual(surface.Last(), prefix + "CREW_EXIT_BTN\t-1\tpress", "ships tab closes the crew window");
        AnswerAll(router, surface, true);
        crew.revision += 1;
        router.SetInterface(crew);
        Expect(surface.props["tabs"][2]["selected"] == true && surface.props["ships"].is_null(),
               "while the crew window is still open: the ships tab is chosen, the crew stays drawn");
        InterfaceModel closed = Load(models, "transfer-loot.json");
        closed.revision = crew.revision + 1;
        router.SetInterface(closed);
        Expect(surface.props["tabs"][2]["selected"] == true && !surface.props["ships"].is_null(),
               "the crew window closed: the ships tab opens");
    }

    void QuickTake()
    {
        // The hand on a row not selected: the game selects it, then its arrow takes all of theirs (495 in packs of
        // 20: 25 presses, the last what is left).
        FakeSurface quick_surface;
        Router quick(quick_surface);
        Scripted(quick);
        quick.SetInterface(Load(models, "transfer-loot.json"));
        quick.OnEvent({"goods", "quick", "0"});
        ExpectEqual(quick_surface.Last(), prefix + "TABLE_LIST\t0\tselect", "quick take: the row is selected first");
        AnswerAll(quick, quick_surface, true);
        quick_surface.commands.clear();
        InterfaceModel row = Load(models, "transfer-loot-row.json");
        row.revision = 100;
        quick.SetInterface(row);
        Expect(quick_surface.commands == std::vector<std::string>(25, prefix + "B_LEFT\t-1\tpress"),
               "quick take: then all of theirs by the game's arrow: " + Json(quick_surface.commands).dump());
    }

    void Arrows()
    {
        // Back from the crew window the game hides both arrows (its focus is off the table): the row is selected
        // again once, not on every build while they stay hidden.
        FakeSurface hidden_surface;
        Router hidden_router(hidden_surface);
        Scripted(hidden_router);
        InterfaceModel hidden = Load(models, "transfer-loot-row.json");
        hidden.widgets.erase(std::remove_if(hidden.widgets.begin(), hidden.widgets.end(),
                                            [](const Widget &w) { return w.node == "B_LEFT" || w.node == "B_RIGHT"; }),
                             hidden.widgets.end());
        hidden_router.SetInterface(hidden);
        AnswerAll(hidden_router, hidden_surface, true);
        hidden.revision += 1;
        hidden_router.SetInterface(hidden);
        const auto selects = std::count(hidden_surface.commands.begin(), hidden_surface.commands.end(),
                                        prefix + "TABLE_LIST\t0\tselect");
        Expect(selects == 1,
               "arrows hidden: the selected row is selected again once: " + Json(hidden_surface.commands).dump());
    }

    void Questions()
    {
        // A tap beside a question whose way out leaves things as they are takes it; beside one whose every answer
        // decides something (the prisoners) it is refused.
        InterfaceModel capture = Load(models, "transfer-crew-capture.json");
        capture.revision = 200;
        router.SetInterface(capture);
        surface.commands.clear();
        router.OnEvent({"modal", "dismiss", ""});
        Expect(surface.commands.empty(), "the prisoners' fate is answered by its buttons only");
        InterfaceModel fate = Load(models, "transfer-fate.json");
        fate.revision = 201;
        router.SetInterface(fate);
        router.OnEvent({"modal", "dismiss", ""});
        ExpectEqual(surface.Last(), prefix + "REMOVE_CANCEL_OFFICER\t-1\tpress",
                    "beside the ship's question: keep the ship");
        InterfaceModel captain = Load(models, "transfer-captain.json");
        captain.revision = 202;
        router.SetInterface(captain);
        Expect(surface.props["sheet"]["kind"] == "captain" && surface.props["sheet"]["skills"].size() == 7 &&
                   surface.props["sheet"]["skills"][0]["k"] == "<skill.sailing>",
               "the captain choice is the captain sheet with the chosen one's ship skills: " +
                   surface.props["sheet"]["skills"].dump());
        router.OnEvent({"sheet", "dismiss", ""});
        ExpectEqual(surface.Last(), prefix + "CANCEL_ADD_OFFICER\t-1\tpress", "beside the captain sheet: «Отмена»");
    }
};

static void Transfer(const std::string &models)
{
    TransferRun run{models};
    run.Tabs();
    run.QuickTake();
    run.Arrows();
    run.Questions();
}

// Alchemy on its script screen (cases/alchemy.json has the views of the recorded models and the game's answers): a
// recipe chosen by the scroll's own tap, bound to the opening; the game's exit.
static void Alchemy(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    const std::string prefix = "widget:alchemy\t";
    router.SetInterface(Load(models, "alchemy.json"));
    Expect(router.Using() == "alchemy" && surface.tag == "s-alchemy" && surface.errors.empty(),
           "alchemy is drawn by its script screen: " + Json(surface.errors).dump());
    router.OnEvent({"recipes", "row", "potion1"});
    ExpectEqual(surface.Last(), prefix + "ANDROID_ALCHEMY_RECIPE\t1\tchoose@11:potion1",
                "another recipe: the scroll's own tap, bound to the opening");
    AnswerAll(router, surface, true);
    surface.commands.clear();
    router.OnEvent({"recipes", "row", "cartridge"});
    Expect(surface.commands.empty(), "the shown recipe is not chosen again");
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "EXIT_BTN\t-1\tpress", "back leaves through the game's exit");
}

// Shipyard on its script screen (cases/shipyard.json has the views of the recorded models): our ship's figures
// shown beside an offer's in a later build, the game's question answered from its sheet, a repair asked of the
// game's arrows and shown where the game stopped them.
static void Shipyard(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    const std::string prefix = "widget:shipyard\t";
    InterfaceModel main = Load(models, "shipyard-main.json");
    router.SetInterface(main);
    Expect(router.Using() == "shipyard" && surface.tag == "s-shipyard" && surface.errors.empty(),
           "shipyard is drawn by its script screen: " + Json(surface.errors).dump());
    Expect(surface.props["detail"]["facts"][0]["own"].is_null(), "our ship on the card: nothing beside its figures");
    router.OnEvent({"offer-0", "click", ""});
    ExpectEqual(surface.Last(), prefix + "TABLE_SHIPYARD\t0\tselect@7:shipyard:offer:ship1", "an offer is chosen");
    AnswerAll(router, surface, true);
    router.SetInterface(Load(models, "shipyard-preview.json"));
    const Json &detail = surface.props["detail"];
    Expect(detail["facts"][0]["v"] == "151 / 151" && detail["facts"][0]["own"] == "610 / 610" &&
               detail["against"] == "Марта" && detail["actions"][0]["sub"] == "2 170 − 11 575",
           "the offer's figures beside those our ketch had; buying names the price and what the yard pays for "
           "our ketch: " +
               detail.dump().substr(0, 400));
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), prefix + "BUTTON_BUY\t-1\tpress", "buying presses the game's button");
    AnswerAll(router, surface, true);
    router.SetInterface(Load(models, "shipyard-confirm-buy.json"));
    Expect(surface.props["sheet"]["kind"] == "confirm" &&
               surface.props["sheet"]["text"] == "Купить корабль за -9 405 песо?",
           "the game's question is the sheet, with what the trade-in comes to as the game says it");
    router.OnEvent({"sheet-ok", "click", ""});
    ExpectEqual(surface.Last(), prefix + "MSG_OK\t-1\tpress", "the sheet's ok is the game's");
    AnswerAll(router, surface, true);

    InterfaceModel repair = Load(models, "shipyard-repair.json");
    router.SetInterface(repair);
    surface.commands.clear();
    router.OnEvent({"hull", "commit", "58"});
    router.Tick();
    Expect(surface.commands.size() == 8 && surface.Last() == prefix + "REPAIR_RIGHT_H\t-1\tpress",
           "+8 % = eight presses of the game's right arrow: " + Json(surface.commands).dump());
    // The purse stopped the arrows at 54 %: the part shows where the game stopped, its price is the game's.
    repair.revision += 1;
    Find(repair, "REPAIR_QTY_H", "text")->label = "54%";
    Find(repair, "REPAIR_WINDOW_TEXT", "text")->label = "2480";
    AnswerAll(router, surface, true);
    router.SetInterface(repair);
    const Json &sheet = surface.props["sheet"];
    Expect(sheet["parts"][0]["q"]["value"] == 54 && sheet["parts"][0]["q"]["floor"] == 50 &&
               sheet["actions"][1]["sub"] == "2 480" && sheet["actions"][1]["disabled"] == false &&
               surface.errors.empty(),
           "the game's order and price: " + sheet.dump().substr(0, 400));
    router.OnEvent({"sheet-ok", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_OK\t-1\tpress", "repair");
    AnswerAll(router, surface, true);
    router.SetInterface(repair);
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), prefix + "REPAIR_CANCEL\t-1\tpress", "← closes the game's window first");
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

// The character screen in script on one router: the game's multi-step moves walked one model at a time (a skill row
// selected, then its arrow; a perk selected, opened, then the game's accept; a post selected, then opened; a
// candidate selected, then accepted), the game's refusal of a perk and the sections.
static void Hero(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    InterfaceModel model = Load(models, "hero.json");
    router.SetInterface(model);
    ExpectEqual(surface.tag, "s-hero", "s-hero mounted");
    Expect(surface.props["tabs"].size() == 4 && surface.props["card"]["title"] == "Anne Bonny" &&
               surface.props["headerSection"] == "hero",
           "skills, the two perk groups, officers; the card; the sections");
    Expect(surface.props["extras"]["rows"].empty(), "every widget is known: no extras");

    router.OnEvent({"personal", "inc", "0"});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_SKILL_1\t0\tselect@4:Leadership",
                "a point asked selects the skill row first");
    AnswerAll(router, surface, true);
    model.revision += 1;
    Row(model, "TABLE_SKILL_1", 0)->value = "1";
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tB_SKILLUP\t-1\tpress", "then presses the game's arrow");
    AnswerAll(router, surface, true);
    model.revision += 1;
    router.SetInterface(model);
    Expect(surface.props["personal"]["rows"][0]["cells"][1]["noLeft"] == false,
           "a point put in during this visit may be taken back");

    // A perk: selected, its window opened, then the game's accept.
    router.OnEvent({"header-tabs", "select", "perks1"});
    router.Tick();
    Expect(surface.props["tab"] == "perks", "the personal group's tree");
    router.OnEvent({"perks", "row", "criticalhit"});
    router.Tick();
    router.OnEvent({"perk-learn", "click", ""});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_PERKS\t1\tselect@4:criticalhit",
                "learning selects the perk");
    AnswerAll(router, surface, true);
    model.revision += 1;
    Row(model, "TABLE_PERKS", 0)->value = "0";
    Row(model, "TABLE_PERKS", 1)->value = "1";
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_PERKS\t1\tactivate@4:criticalhit", "then opens its window");
    AnswerAll(router, surface, true);
    InterfaceModel window = model;
    window.revision += 1;
    window.widgets.push_back(Make("PERK_ACCEPT", "button", "Learn", {"press"}));
    window.widgets.push_back(Make("PERK_CANCEL", "button", "Cancel", {"press"}));
    router.SetInterface(window);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tPERK_ACCEPT\t-1\tpress", "then takes the game's accept");
    AnswerAll(router, surface, true);

    // The game refuses: its window's accept is off. The window is closed and the button stays off in this state.
    model.revision = window.revision + 1;
    router.SetInterface(model);
    router.OnEvent({"perk-learn", "click", ""});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tTABLE_PERKS\t1\tactivate@4:criticalhit",
                "a selected perk opens its window at once");
    AnswerAll(router, surface, true);
    window = model;
    window.revision += 1;
    window.widgets.push_back(Make("PERK_ACCEPT", "button", "Learn"));
    window.widgets.back().enabled = false; // the game's refusal: SetSelectable("PERK_ACCEPT", false)
    window.widgets.push_back(Make("PERK_CANCEL", "button", "Cancel", {"press"}));
    router.SetInterface(window);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tPERK_CANCEL\t-1\tpress", "a refused perk's window is closed");
    AnswerAll(router, surface, true);
    model.revision = window.revision + 1;
    router.SetInterface(model);
    Expect(surface.props["detail"]["learn"]["disabled"] == true, "and the game's refusal stays on the button");

    // A post: selected, then opened; the chooser's candidate: selected, then accepted.
    router.OnEvent({"header-tabs", "select", "officers"});
    router.Tick();
    router.OnEvent({"post-2", "click", ""});
    Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t2\tselect@", 0) == 0,
           "a post tapped selects its slot: " + surface.Last());
    AnswerAll(router, surface, true);
    InterfaceModel assign = Load(models, "hero-assign.json");
    assign.revision = model.revision + 1;
    for (Widget &widget : assign.widgets)
        if (widget.node == "PASSENGERSLIST")
            widget.value = "0";
    router.SetInterface(assign);
    ExpectEqual(surface.props["dialog"]["kind"], "assign", "the game's chooser drawn as our sheet");
    {
        // The chooser's rows reused for another post before the game selected the tapped one: nothing is accepted.
        router.OnEvent({"candidate-1", "", ""});
        AnswerAll(router, surface, true);
        InterfaceModel other = assign;
        other.revision = assign.revision + 1;
        Row(other, "CHARACTERS_SCROLL", 2)->value = "0";
        Row(other, "CHARACTERS_SCROLL", 1)->value = "1";
        Row(other, "PASSENGERSLIST", 1)->value = "1";
        router.SetInterface(other);
        Expect(surface.Last().find("ACCEPT_ADD_OFFICER") == std::string::npos,
               "a candidate tapped for one post is not accepted for another: " + surface.Last());
        assign.revision = other.revision + 1;
        router.SetInterface(assign);
    }
    router.OnEvent({"candidate-1", "", ""});
    Expect(surface.Last().rfind("widget:lscchar_all\tPASSENGERSLIST\t1\tselect", 0) == 0,
           "a candidate tapped is selected: " + surface.Last());
    AnswerAll(router, surface, true);
    assign.revision += 1;
    Row(assign, "PASSENGERSLIST", 1)->value = "1";
    router.SetInterface(assign);
    ExpectEqual(surface.Last(), "widget:lscchar_all\tACCEPT_ADD_OFFICER\t-1\tpress",
                "then accepted by the game's button");
    AnswerAll(router, surface, true);

    model.revision = assign.revision + 1;
    router.SetInterface(model);
    router.OnEvent({"header", "section", "ship"});
    ExpectEqual(surface.Last(), "widget:lscchar_all\tI_SHIP\t-1\tpress", "another section");
    Expect(surface.errors.empty(), "no error line: " + (surface.errors.empty() ? std::string() : surface.errors[0]));
}

static void Items(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    InterfaceModel model = Load(models, "items.json");
    router.SetInterface(model);
    ExpectEqual(router.Using(), "items", "the items draw on the items screen");
    ExpectEqual(surface.tag, "s-items", "s-items mounted");
    const Json &view = surface.props;
    Expect(view["slots"].size() == 10 && view["slots"][0]["label"] == "<items.slot_blade>" &&
               view["slots"][0]["image"]["texture"] == "atlas:ITEMS_1/itm1" && view["slots"][1]["empty"] == true,
           "seven slots and the captain's three artefact slots, what she wears drawn by the game's picture");
    Expect(view["rows"].size() == 3 && view["rows"][0]["cells"][0]["badge"] == "check" &&
               view["rows"][2]["cells"][1] == "\xC3\x97"
                                              "3",
           "the list: the worn mark, a count only above one");
    Expect(view["stats"][0]["value"] == "4.6 / 140" && view["subject"]["label"] == "Anne Bonny" &&
               view["tabs"].size() == 4 && view["title"] == "Items",
           "the load in the header, the person switch, the game's categories");
    Expect(view["detail"]["title"] == "Rapier" && view["detail"]["text"] == "A light blade." &&
               view["detail"]["action"]["label"] == "Take off" && view["detail"]["action"]["disabled"] == false,
           "the chosen item with the game's own button");
    Expect(view["extras"]["rows"].empty(), "every widget is known: no extras");

    router.OnEvent({"item-equip", "click", ""});
    ExpectEqual(surface.Last(), "widget:items\tEQUIP_BUTTON\t-1\tpress@5:inventory:7:character:1:role:0",
                "the action is the game's press on the row it answered for");
    AnswerAll(router, surface, true);
    router.OnEvent({"items", "row", "1"});
    ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t1\tselect@5:inventory:9:character:1:role:0",
                "a row is chosen");
    router.Tick();
    Expect(surface.props["rows"][1]["selected"] == true, "drawn chosen in the tap's frame");
    const size_t chosen = surface.commands.size();
    AnswerAll(router, surface, true);
    model.revision += 1;
    for (Widget &widget : model.widgets)
        if (widget.node == "TABLE_ITEMS")
            widget.value = widget.index == 1 ? "1" : "0";
        else if (widget.node == "EQUIP_BUTTON")
            widget.label = "Equip that", widget.row_id = "inventory:9:character:1:role:0";
    router.SetInterface(model);
    Expect(surface.commands.size() == chosen && surface.props["detail"]["title"] == "Dagger" &&
               surface.props["detail"]["facts"][0]["dTone"] == "danger" &&
               surface.props["detail"]["facts"][2]["dTone"] == "ok",
           "choosing describes, never equips; its figures against the worn rapier");
    router.OnEvent({"items", "row", "1"});
    Expect(surface.commands.size() == chosen, "choosing it again sends nothing");

    // A slot lists what fits it and chooses what it holds.
    router.OnEvent({"slot-0", "click", ""});
    ExpectEqual(surface.Last(), "widget:items\tTABLE_ITEMS\t0\tselect@5:inventory:7:character:1:role:0",
                "the blade slot chooses the rapier it holds");
    AnswerAll(router, surface, true);
    model.revision += 1;
    for (Widget &widget : model.widgets)
        if (widget.node == "TABLE_ITEMS")
            widget.value = widget.index == 0 ? "1" : "0";
        else if (widget.node == "EQUIP_BUTTON")
            widget.label = "Take off", widget.row_id = "inventory:7:character:1:role:0";
    router.SetInterface(model);
    Expect(surface.props["rows"].size() == 2 && surface.props["slots"][0]["selected"] == true, "and lists the blades");
    router.OnEvent({"item-tabs", "select", "2"});
    ExpectEqual(surface.Last(), "widget:items\tTABBTN_3\t-1\tpress", "a game category");
    router.Tick();
    Expect(surface.props["tabs"][2]["selected"] == true, "drawn chosen in the tap's frame");
    AnswerAll(router, surface, true);
    model.revision += 1;
    router.SetInterface(model);
    Expect(surface.props["slots"][0]["selected"] == false && surface.props["rows"].size() == 3,
           "a category clears the slot filter");

    // The game refuses the button: it stays, off, with the game's reason.
    for (Widget &widget : model.widgets)
        if (widget.node == "EQUIP_BUTTON")
            widget.enabled = false, widget.actions.clear(), widget.unavailable_reason = "Needs a weapon perk";
    model.revision += 1;
    router.SetInterface(model);
    const size_t refused = surface.commands.size();
    router.OnEvent({"item-equip", "click", ""});
    Expect(surface.commands.size() == refused && surface.props["detail"]["action"]["disabled"] == true &&
               surface.props["detail"]["action"]["reason"] == "Needs a weapon perk",
           "a refused button is off with the game's reason, and sends nothing");

    // Another person: the game shows him a frame after his slot is chosen.
    router.OnEvent({"header", "subject-next", ""});
    ExpectEqual(surface.Last(), "widget:items\tCHARACTERS_SCROLL\t1\tselect@5:1:1", "the next person's things");
    router.Tick();
    Expect(surface.props["subject"]["label"] == "Jack Rackham", "his name in the tap's own frame");
    AnswerAll(router, surface, true);
    InterfaceModel step = Load(models, "../flows/items-jack-scroll.json");
    step.revision = model.revision + 1;
    router.SetInterface(step);
    Expect(surface.props["subject"]["label"] == "Jack Rackham", "the game's step: still his name");
    InterfaceModel jack = Load(models, "../flows/items-jack.json");
    jack.revision = model.revision + 2;
    router.SetInterface(jack);
    Expect(surface.props["subject"]["label"] == "Jack Rackham" && surface.props["rows"][1]["cells"][1] == "\xC3\x97"
                                                                                                          "2",
           "the game shows his things");
    router.OnEvent({"header", "subject-list", ""});
    Expect(surface.props["subject"]["open"] == true && surface.props["subject"]["rows"].size() == 2,
           "the people listed");
    router.OnEvent({"header-subjects", "dismiss", ""});
    Expect(surface.props["subject"]["open"] == false, "a tap beside the list closes it");

    // The game's map window: back closes it.
    InterfaceModel map = Load(models, "items.json");
    map.revision = model.revision + 3;
    map.widgets.push_back(Make("MAP_CAPTION", "text", "Treasure map"));
    map.widgets.push_back(Make("MAP_EXIT_BTN", "button", "", {"press"}));
    Widget picture = Make("MAP_PICTURE", "image");
    picture.images.push_back(ImageRef{});
    picture.images.back().kind = "texture";
    picture.images.back().image = "interfaces/maps/treasure";
    map.widgets.push_back(picture);
    router.SetInterface(map);
    Expect(surface.props["map"]["title"] == "Treasure map" &&
               surface.props["map"]["image"]["texture"] == "interfaces/maps/treasure",
           "the map window: the game's picture by its texture name");
    Expect(router.WantsBack(), "back closes the map first");
    router.Back();
    ExpectEqual(surface.Last(), "widget:items\tMAP_EXIT_BTN\t-1\tpress", "back is the map's close");
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
        Scripted(router);
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
        Scripted(router);
        hud.RequestSubject({"character", 1, -1});
        router.SetInterface(Load(models, "hero.json"));
        Expect(surface.commands.empty() && !router.Take("character"),
               "the hero's portrait: the captain is already the subject, nothing to select");
        hud.RequestSubject({"character", 77, -1});
        InterfaceModel again = Load(models, "hero.json");
        again.revision += 1;
        router.SetInterface(again);
        Expect(!surface.errors.empty(), "a person not in the character list: that is said");
    }
    for (const auto &[character, slot] : {std::pair{14, 3}, std::pair{20, 10}})
    {
        FakeSurface surface;
        Router router(surface);
        SubjectMount mount;
        storm::ui::hud::Hud hud(mount);
        router.AttachHud(&hud);
        Scripted(router);
        hud.RequestSubject({"character", character, -1});
        router.SetInterface(Load(models, "hero-people.json"));
        Expect(surface.Last().rfind("widget:lscchar_all\tCHARACTERS_SCROLL\t" + std::to_string(slot) + "\tselect@",
                                    0) == 0,
               "a tapped officer on two posts or a companion is selected by the first slot: " + surface.Last());
    }
    {
        FakeSurface surface;
        Router router(surface);
        Scripted(router);
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
        Scripted(router);
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
        Expect(!surface.errors.empty(), "a ship the squadron does not have: said aloud");
    }
}

// The ship screen (its script, screens/hero/ship.c) on one router: the card, the crew, the hold with the game's
// overboard window, the guns' window; the game answers every move.
struct ShipRun
{
    std::string models;
    FakeSurface surface;
    Router router{surface};
    InterfaceModel model = Load(models, "ship.json");
    const std::string prefix = "widget:ship\t";

    void Answer()
    {
        AnswerAll(router, surface, true);
        model.revision += 1;
        router.SetInterface(model);
    }
    void Remove(const char *start)
    {
        std::erase_if(model.widgets, [start](const Widget &w) { return w.node.rfind(start, 0) == 0; });
    }
    void Add(const char *node, const char *type, const char *label = "")
    {
        model.widgets.push_back(Make(node, type, label, {type == std::string("edit") ? "edit" : "press"}));
    }

    void Opening()
    {
        Scripted(router);
        router.SetInterface(model);
        ExpectEqual(surface.tag, "s-ship", "the ship is drawn by its script screen");
        Expect(surface.errors.empty(), "no errors: " + Json(surface.errors).dump());
        const Json card = surface.props["card"];
        Expect(card["title"] == "Swallow" && card["sub"] == "Sloop \xC2\xB7 <yard.class> 6" &&
                   card["captain"] == "<ship.captain> Peter Marsh \xC2\xB7 <ship.companion>" &&
                   card["hull"]["value"] == 50 && card["crew"]["mark"] == 22 &&
                   card["exchange"]["id"] == "ship-exchange",
               "the card: the game's name, type and class, a companion's captain, the bars: " + card.dump());
        router.OnEvent({"ship-rename", "click", ""});
        ExpectEqual(surface.text.text, "Swallow", "the quill asks for the name in the game's field");
        router.SubmitText(surface.text.token, "Kestrel", true);
        ExpectEqual(surface.Last(), prefix + "EDIT_BOX\t-1\tKestrel", "the name goes to the game's field");
        Answer();
    }

    void Crew()
    {
        router.OnEvent({"header-tabs", "select", "crew"});
        const Json crew = surface.props["crew"];
        Expect(crew["groups"].size() == 3 && crew["groups"][0]["value"] == 62 && crew["groups"][1]["word"] == "Green" &&
                   crew["morale"] == 45 && crew["moraleWord"] == "NORMAL",
               "the crew's quality from the game's crew and words: " + crew.dump());
        Expect(surface.props["facts"][1]["title"] == "1 240 pesos" &&
                   surface.props["facts"][1]["meta"] == "Ship upkeep" && surface.props["crewActions"].size() == 2,
               "food and upkeep as the game writes them, the shares and the morale's buttons");
        router.OnEvent({"crew-morale", "click", ""});
        ExpectEqual(surface.Last(), prefix + "CREW_MORALE_BUTTON\t-1\tpress", "the morale asks the game");
        Add("REMOVE_WINDOW_CAPTION", "text", "Raise morale");
        Add("REMOVE_WINDOW_TEXT", "text", "Costs 340 pesos");
        Add("REMOVE_ACCEPT_OFFICER", "button", "Yes");
        Add("REMOVE_CANCEL_OFFICER", "button", "No");
        Answer();
        Expect(surface.props["dialog"]["title"] == "Raise morale" &&
                   surface.props["dialog"]["text"] == "Costs 340 pesos",
               "the game's question with its price");
        Expect(router.WantsBack(), "back closes the question first");
        router.Back();
        ExpectEqual(surface.Last(), prefix + "REMOVE_CANCEL_OFFICER\t-1\tpress", "back is the game's no");
        Remove("REMOVE_");
        Answer();
    }

    void Hold()
    {
        router.OnEvent({"header-tabs", "select", "cargo"});
        Json view = surface.props;
        Expect(view["list"]["rows"].size() == 3 &&
                   view["list"]["rows"][1]["meta"] == "Pack 20 \xC2\xB7 Pack weight 1" &&
                   view["load"]["text"] == "189 <common.of> 700 <transfer.cwt>",
               "the goods with their packs, the deadweight: " + view["list"].dump().substr(0, 300));
        router.OnEvent({"goods", "row", "1"});
        ExpectEqual(surface.Last(), prefix + "TABLE_LIST\t1\tactivate@7:ship:good:5:owner:31",
                    "a row opens the game's overboard window of its good");
        Add("QTY_CAPTION", "text", "Gunpowder");
        Add("QTY_EDIT", "edit");
        Add("QTY_OK_BUTTON", "button", "Ok");
        Add("QTY_CANCEL_BUTTON", "button", "Cancel");
        Add("QTY_BOMB_BUTTON", "button", "Mine");
        Find(model, "QTY_EDIT", "edit")->value = "0";
        Answer();
        view = surface.props;
        Expect(view["list"]["rows"][1]["q"]["value"] == 220 && view["list"]["rows"][1]["q"]["max"] == 220 &&
                   view["cargoActions"].size() == 2 && view["cargoActions"][1]["disabled"] == true,
               "the open row: all of the hold's 220 stay, nothing to throw yet; the mine beside it");
        router.OnEvent({"goods", "change", "0"});
        ExpectEqual(surface.Last(), prefix + "QTY_EDIT\t-1\t220", "keeping none sets all of it to go");
        // The game takes the amount out of the hold while it is set: the good leaves the list, the hold lightens.
        Find(model, "QTY_EDIT", "edit")->value = "220";
        std::erase_if(model.widgets, [](const Widget &w) { return w.node == "TABLE_LIST" && w.index == 1; });
        for (Widget &widget : model.widgets)
            if (widget.node == "TABLE_OTHER" && widget.row_id == "Capacity")
                widget.cells[2].text = "178 / 700";
        Answer();
        view = surface.props;
        Expect(view["list"]["rows"].size() == 3 && view["list"]["rows"][1]["name"] == "Gunpowder" &&
                   view["list"]["rows"][1]["q"]["value"] == 0 &&
                   view["load"]["text"] == "189 \xE2\x80\xBA 178 <common.of> 700 <transfer.cwt>" &&
                   view["cargoActions"][1]["label"] == "<ship.overboard> 220",
               "the row stays in its place, the deadweight shows the drop: " + view["load"].dump());
        router.OnEvent({"goods-drop", "click", ""});
        ExpectEqual(surface.Last(), prefix + "QTY_OK_BUTTON\t-1\tpress", "overboard is the game's ok");
        Answer();
        router.OnEvent({"goods", "row", "0"});
        ExpectEqual(surface.Last(), prefix + "QTY_CANCEL_BUTTON\t-1\tpress", "another good first closes the window");
        Remove("QTY_");
        Answer();
        ExpectEqual(surface.Last(), prefix + "TABLE_LIST\t0\tactivate@7:ship:good:0:owner:31", "then that good opens");
        Expect(surface.errors.empty(), "no errors: " + Json(surface.errors).dump());
    }

    void Cannons()
    {
        FakeSurface guns_surface;
        Router guns_router(guns_surface);
        Scripted(guns_router);
        InterfaceModel guns = Load(models, "ship-cannons.json");
        guns_router.SetInterface(guns);
        const Json sheet = guns_surface.props["sheet"];
        Expect(guns_surface.errors.empty() && sheet["sides"].size() == 4 && sheet["sides"][1]["q"]["max"] == 4 &&
                   sheet["calibres"]["rows"].size() == 2 && sheet["actions"][0]["sub"] == "Cannon 6 lb",
               "the guns' window: the sides up to their ports, the calibres in the hold, «fit all» names what it fits");
        guns_surface.commands.clear();
        guns_router.OnEvent({"guns-port", "commit", "1"});
        Expect(guns_surface.commands.size() == 2 && guns_surface.Last() == prefix + "CANNONS_LEFT_L\t-1\tpress",
               "3 to 1 guns on the port side: two presses of the game's arrow: " + Json(guns_surface.commands).dump());
        Expect(guns_router.WantsBack(), "back closes the guns' window first");
    }
};

static void Ship(const std::string &models)
{
    ShipRun run{models};
    run.Opening();
    run.Crew();
    run.Hold();
    run.Cannons();
}

static void Journal(const std::string &)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
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
    AnswerAll(router, surface, true);
    router.SetInterface(text);
    view = surface.props;
    Expect(view["list"].size() == 2 && view["list"][1]["selected"] == true && view["entry"]["title"] == "Debts" &&
               view["entry"]["entries"].size() == 2 &&
               view["entry"]["entries"][0]["text"] == "Arrived.\nMet the brother.",
           "the titles stay while the text is open; the text split into dated entries");
    const size_t sent = surface.commands.size();
    router.OnEvent({"entries", "row", "0"});
    ExpectEqual(surface.Last(), "back", "another entry: back to the titles first");
    AnswerAll(router, surface, true);
    model.revision = text.revision + 1;
    router.SetInterface(model);
    ExpectEqual(surface.Last(), "widget:questbook\tQUEST_TITLE\t0\topen@8:burden", "then open it");
    Expect(surface.commands.size() == sent + 2, "two steps");
    Expect(surface.props["entry"].is_object() && surface.props["entry"]["title"] == "Debts",
           "meanwhile the open text stays on the screen, not the titles of the step between: " +
               surface.props["entry"].dump());

    AnswerAll(router, surface, true);
    text.revision = model.revision + 1;
    router.SetInterface(text);
    Expect(router.WantsBack(), "an open text: back is the screen's");
    router.Back();
    ExpectEqual(surface.Last(), "back", "back closes the text");
    AnswerAll(router, surface, true);
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
        Scripted(toast_router);
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
        AnswerAll(toast_router, toast_surface, true);
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

// The table window's globals in a recorded model's state (the fields that change from the model before it).
static void TableState(std::initializer_list<std::pair<const char *, const char *>> globals)
{
    for (const auto &[name, value] : globals)
        ScriptGame::Get().Set(name, value);
}

static void Games(const std::string &models)
{
    FakeSurface surface;
    Router router(surface);
    Scripted(router);
    // Dice, our reroll: two dice marked (already paid), the cup throws them.
    InterfaceModel reroll = Load(models, "boal_dice-reroll.json");
    TableState({{"iRate", "200"},
                {"DiceState.Hero.Result.Type", "5"},
                {"DiceState.Comp.Result.Type", "6"},
                {"iMoneyP", "7600"},
                {"iChest", "2400"},
                {"openExit", "0"},
                {"bStartGame", "2"},
                {"dir_i", "1"},
                {"dir_i_start", "1"},
                {"bLockClick", "0"},
                {"DiceState.Hero.d1", "3"},
                {"DiceState.Hero.d2", "3"},
                {"DiceState.Hero.d3", "3"},
                {"DiceState.Hero.d4", "6"},
                {"DiceState.Hero.d5", "1"},
                {"DiceState.Hero.d1.Mix", "0"},
                {"DiceState.Hero.d2.Mix", "0"},
                {"DiceState.Hero.d3.Mix", "0"},
                {"DiceState.Hero.d4.Mix", "1"},
                {"DiceState.Hero.d5.Mix", "1"}});
    router.SetInterface(reroll);
    Expect(router.Using() == "boal_dice" && surface.tag == "s-table-game" && router.Rejection().empty() &&
               surface.errors.empty(),
           "boal_dice is drawn by the table-game screen");
    Expect(surface.props["extras"]["rows"].empty() && surface.props["extras"]["note"] == "",
           "every exported node is known");
    Expect(surface.props["kind"] == "dice" && surface.props["locked"] == true, "back is locked during a round");
    Expect(surface.props["stats"][0]["value"] == "200" && surface.props["stats"][1]["value"] == "7 600",
           "stake and our money");
    Expect(surface.props["table"]["opponent"] == "Jacques Dubois" &&
               surface.props["table"]["portrait"] == "INTERFACES/PORTRAITS/128/face_377" &&
               surface.props["table"]["bank"] == "2 400" && surface.props["table"]["coins"] == "silver",
           "the game's opponent by its own name, portrait, bank and the coins the script lays");
    const Json ours = surface.props["bottom"]["pieces"];
    Expect(ours.size() == 5 && ours[0]["pips"].size() == 3 && ours[0]["action"] == "mark" &&
               ours[0]["disabled"] == false,
           "our dice: faces from the script pictures, tappable");
    Expect(ours[3]["lift"] == true && ours[3]["pips"].size() == 6 && ours[3].count("action") == 0,
           "a marked die keeps its face, rises and cannot be unmarked");
    Expect(surface.props["bottom"]["label"] == "" && surface.props["top"]["label"] == "<games.combo_6>",
           "the combination of a complete row");
    Expect(surface.props["buttons"].size() == 1 && surface.props["buttons"][0]["label"] == "<games.roll>" &&
               surface.props["buttons"][0]["sub"] == "2 <games.die_many>" &&
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
    TableState({{"DiceState.Hero.d4.Mix", "0"}, {"DiceState.Hero.d5.Mix", "0"}});
    keep.revision = 2;
    router.SetInterface(keep);
    Expect(surface.props["buttons"][0]["id"] == "pass" && surface.props["buttons"][0]["label"] == "<games.keep>",
           "nothing marked: keep the dice");
    router.OnEvent({"pass", "click", ""});
    ExpectEqual(surface.Last(), "widget:boal_dice\tB_ICON_1\t-1\tpress", "keeping is the script's pass");

    // The round ends with our win: the game paid the bank and says so in its line; another round or leave.
    InterfaceModel result = Load(models, "boal_dice-result.json");
    TableState({{"iRate", "200"},
                {"DiceState.Hero.Result.Type", "4"},
                {"DiceState.Comp.Result.Type", "6"},
                {"iMoneyP", "10000"},
                {"iChest", "0"},
                {"openExit", "1"},
                {"bStartGame", "4"},
                {"dir_i", "-1"},
                {"dir_i_start", "1"},
                {"bLockClick", "0"},
                {"DiceState.Hero.d5", "6"},
                {"DiceState.Hero.d4.Mix", "0"},
                {"DiceState.Hero.d5.Mix", "0"}});
    result.revision = 4;
    router.SetInterface(result);
    Expect(surface.props["locked"] == false && surface.props["table"]["bank"] == "" &&
               surface.props["table"]["line"] != "",
           "after the round: the game's line, an empty bank");
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
    TableState({{"iMoneyP", "12400"}, {"bLockClick", "1"}});
    router.SetInterface(Load(models, "boal_dice-end.json"));
    Expect(surface.props["buttons"][0]["id"] == "again" && surface.props["buttons"][0]["disabled"] == true &&
               surface.props["buttons"][1]["id"] == "leave" && surface.props["buttons"][1]["variant"] == "primary",
           "the opponent quits: leaving is the primary");

    // Cards, our turn.
    FakeSurface cards;
    Router table(cards);
    Scripted(table);
    TableState({{"iRate", "500"},
                {"iMoneyP", "9450"},
                {"iChest", "2500"},
                {"openExit", "0"},
                {"bStartGame", "3"},
                {"dir_i", "1"},
                {"dir_i_start", "1"},
                {"cardMove", "0"}});
    table.SetInterface(Load(models, "boal_cards.json"));
    Expect(table.Using() == "boal_cards" && cards.tag == "s-table-game" && cards.errors.empty() &&
               cards.props["kind"] == "cards",
           "boal_cards is drawn by the table-game screen");
    Expect(cards.props["table"]["coins"] == "gold" && cards.props["pack"][0]["kind"] == "deck", "gold coins, the deck");
    const Json hand = cards.props["bottom"]["pieces"];
    Expect(hand.size() == 3 && hand[0]["rank"] == "7" && hand[0]["suit"] == "spades" && hand[0]["red"] == false &&
               hand[1]["rank"] == "<games.rank_J>" && hand[1]["red"] == true,
           "our cards: rank and suit from the picture names");
    Expect(cards.props["bottom"]["label"] == "20 <games.point_many>",
           "our points as the game counts its cards (its deck counts the jack 3)");
    {
        FakeSurface modded;
        Router other(modded);
        Scripted(other);
        InterfaceModel deck = Load(models, "boal_cards.json");
        for (Widget &widget : deck.widgets)
            if (widget.node == "PCard1")
                widget.label = "hearts_Z";
        other.SetInterface(deck);
        Expect(modded.errors.size() == 1, "a card the game counts nothing for is reported");
    }
    Expect(cards.props["top"]["pieces"].size() == 2 && cards.props["top"]["pieces"][0]["kind"] == "back" &&
               cards.props["top"]["label"] == "",
           "the opponent's cards face down, no points");
    Expect(cards.props["buttons"][0]["id"] == "pass" && cards.props["buttons"][1]["id"] == "throw" &&
               cards.props["buttons"][1]["disabled"] == false,
           "stand, or take a card");
    table.OnEvent({"throw", "click", ""});
    ExpectEqual(cards.Last(), "widget:boal_cards\tB_PACK\t-1\tpress", "take a card is B_PACK");
    table.OnEvent({"pass", "click", ""});
    ExpectEqual(cards.Last(), "widget:boal_cards\tB_ICON_1\t-1\tpress", "stand is B_ICON_1");

    // The opponent's turn: nothing to press.
    InterfaceModel opponent = Load(models, "boal_cards-opponent.json");
    TableState({{"iMoneyP", "8950"}, {"iChest", "3000"}, {"bStartGame", "4"}, {"dir_i", "-1"}, {"cardMove", "1"}});
    opponent.revision = 2;
    table.SetInterface(opponent);
    Expect(cards.props["buttons"][0]["disabled"] == true && cards.props["buttons"][1]["disabled"] == true &&
               cards.props["buttons"][1]["reason"] == "<games.wait>",
           "the opponent plays: both buttons wait");

    // Opened: he has 20; the game's line says who won.
    InterfaceModel opened = Load(models, "boal_cards-result.json");
    TableState({{"openExit", "1"}, {"bStartGame", "2"}, {"cardMove", "0"}});
    opened.revision = 3;
    table.SetInterface(opened);
    Expect(cards.props["top"]["label"] == "20 <games.point_many>" &&
               cards.props["top"]["pieces"][0]["rank"] == "<games.rank_A>",
           "the opponent's cards open with his points");
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
    Scripted(router);
    router.SetInterface(Load(models, "sailsgerald.json"));
    Expect(router.Using() == "sailsgerald" && surface.tag == "s-sail-emblem" && router.Rejection().empty() &&
               surface.errors.empty(),
           "sailsgerald is drawn by the sail emblem screen");
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
    AnswerAll(router, surface, true);
    router.OnEvent({"emblems", "emblem", "5"});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tSCROLL_GERALD\t5\tselect@5:emblem_06.tga.tx",
                "a tap chooses that emblem in the game");
    AnswerAll(router, surface, true);
    router.OnEvent({"emblem-prev", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tSCROLL_GERALD\t1\tselect@5:emblem_02.tga.tx",
                "‹ chooses the previous one");
    AnswerAll(router, surface, true);
    router.OnEvent({"header-back", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tEXIT_BTN\t-1\tpress", "← leaves");

    // The emblem on the sails chosen: the game offers B_REMOVE. Less money than the price does not stop it (the game
    // charges without asking).
    ScriptGame::Get().Set("TestHero.Money", "900");
    router.SetInterface(Load(models, "sailsgerald-applied.json"));
    Expect(surface.props["primary"]["label"] == "<sails.remove>" && surface.props["primary"]["disabled"] == false &&
               surface.props["prev"] == false,
           "the current emblem: take it off, whatever the purse: " + surface.props["primary"].dump());
    router.OnEvent({"primary", "click", ""});
    ExpectEqual(surface.Last(), "widget:sailsgerald\tB_REMOVE\t-1\tpress", "take off");
    ScriptGame::Get().Set("TestHero.Money", "12480");
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
    Scripted(router);
    InterfaceModel model = Load(models, "hirecrew.json");
    for (Widget &widget : model.widgets)
        if (widget.node == "TAVERN_PRICE")
            widget.label = "\xE2\x80\x94";
    router.SetInterface(model);
    model.revision += 1;
    router.SetInterface(model);
    Expect(!surface.errors.empty() && surface.errors[0].find("TAVERN_PRICE") != std::string::npos,
           "a price caption without its number is an error line: " + Json(surface.errors).dump());

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
    Alchemy(models);
    GoodsOrder(models);
    SailEmblem(models);
    DialogueText(models);
    DialogueOverClosingInterface(models);
    DialogueNumber(models);
    Nations(models);
    Hero(models);
    Items(models);
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
