#include "controls.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>

namespace storm::ui::screens
{

namespace
{

template <size_t N> bool OneOf(const std::string &text, const char *const (&list)[N])
{
    return std::find(std::begin(list), std::end(list), text) != std::end(list);
}

} // namespace

bool Short(const std::string &text, size_t chars)
{
    return !text.empty() && text.find('\n') == std::string::npos && Rml::StringUtilities::LengthUTF8(text) <= chars;
}

std::string SectionOf(const Widget &button)
{
    static const char *const pairs[][2] = {{"I_CHARACTER", "hero"},    {"I_SHIP", "ship"},
                                           {"I_QUESTBOOK", "journal"}, {"I_TRADEBOOK", "tradebook"},
                                           {"I_NATIONS", "nations"},   {"I_ITEMS", "items"}};
    std::string node = Upper(button.node);
    if (node.ends_with("_2"))
        node.resize(node.size() - 2);
    for (const auto &pair : pairs)
        if (node == pair[0])
            return pair[1];
    return {};
}

// Button-role matching data, NOT UI text: the captions a button of the game's own screens may carry
// (lower case, compared whole) and the substrings that mark a destructive one, for every shipped
// language (ru, en, de, es, fr, it) plus the glyph "×". Add a language here when its string table is added.
const char *const kExitLabels[] = {"назад",    "отмена",    "выход",   "уйти",      "закрыть",   // ru
                                   "cancel",   "back",      "exit",    "close",     "×",         // en
                                   "zurück",   "abbrechen", "beenden", "verlassen", "schließen", // de
                                   "atrás",    "cancelar",  "salir",   "cerrar",    "volver",    // es
                                   "retour",   "annuler",   "quitter", "fermer",    "sortir",    // fr
                                   "indietro", "annulla",   "esci",    "uscire",    "chiudi"};   // it
const char *const kConfirmLabels[] = {
    "ок",      "ok",      "oк",       "да",        "готово",     "принять",   "применить", "подтвердить",
    "начать",  "взять",   "купить",   "забрать",                                                     // ru (+en ok)
    "ja",      "fertig",  "annehmen", "anwenden",  "bestätigen", "starten",   "nehmen",    "kaufen", // de
    "sí",      "listo",   "aceptar",  "aplicar",   "confirmar",  "empezar",   "comenzar",  "tomar",
    "comprar", "recoger",                                                                             // es
    "oui",     "terminé", "accepter", "appliquer", "confirmer",  "commencer", "prendre",   "acheter", // fr
    "sì",      "fatto",   "pronto",   "accetta",   "applica",    "conferma",  "inizia",    "prendi",
    "compra"};                                                                                    // it
const char *const kDangerWords[] = {"delete",    "удалить",    "сжечь",  "kill",       "казнить", // en, ru
                                    "löschen",   "verbrennen", "töten",  "hinrichten",            // de
                                    "eliminar",  "borrar",     "quemar", "matar",                 // es
                                    "supprimer", "effacer",    "brûler", "tuer",                  // fr
                                    "elimina",   "cancella",   "brucia", "uccidi"};               // it

ButtonRole RoleOf(const Widget &button)
{
    const std::string node = Upper(button.node);
    const std::string label = Lower(button.label);
    if (!SectionOf(button).empty())
        return ButtonRole::Section;
    for (const char *prefix : {"EXIT", "CANCEL", "CLOSE", "BACK"})
        if (node.starts_with(prefix))
            return ButtonRole::Exit;
    for (const char *suffix : {"_EXIT", "_CANCEL", "_CLOSE"})
        if (node.ends_with(suffix))
            return ButtonRole::Exit;
    if (OneOf(label, kExitLabels))
        return ButtonRole::Exit;
    for (const char *word : kDangerWords)
        if (label.find(word) != std::string::npos || Lower(button.node).find(word) != std::string::npos)
            return ButtonRole::Danger;
    for (const char *prefix : {"OK", "ACCEPT", "YES", "CONFIRM", "DONE", "APPLY", "START"})
        if (node.starts_with(prefix))
            return ButtonRole::Confirm;
    for (const char *suffix : {"_OK", "_ACCEPT", "_YES"})
        if (node.ends_with(suffix))
            return ButtonRole::Confirm;
    if (OneOf(label, kConfirmLabels))
        return ButtonRole::Confirm;
    return ButtonRole::Other;
}

std::string Fill(std::string text, std::initializer_list<std::string> args)
{
    int number = 1;
    for (const std::string &arg : args)
    {
        const std::string mark = "{" + std::to_string(number++) + "}";
        for (size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + arg.size()))
            text.replace(at, mark.size(), arg);
    }
    return text;
}

// One decimal with the decimal mark of the language ("2,3").
std::string Decimal(const Bindings &bindings, double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    std::string text = buffer;
    const size_t dot = text.find('.');
    if (dot != std::string::npos)
        text.replace(dot, 1, bindings.Text("launcher.decimal_mark"));
    return text;
}

std::string Decimal(const std::string &text, const std::string &language)
{
    if (language == "english")
        return text;
    std::string out = text;
    for (size_t i = 1; i + 1 < out.size(); ++i)
        if (out[i] == '.' && out[i - 1] >= '0' && out[i - 1] <= '9' && out[i + 1] >= '0' && out[i + 1] <= '9')
            out[i] = ',';
    return out;
}

// A size in GB from a tenth of a GB, else in MB; `scale` (when given) picks the unit, so
// that "done of total" share one.
std::string Size(const Bindings &bindings, double bytes, double scale)
{
    if ((scale < 0 ? bytes : scale) >= kGiB / 10)
        return Fill(bindings.Text("launcher.gb"), {Decimal(bindings, bytes / kGiB)});
    return Fill(bindings.Text("launcher.mb"), {Decimal(bindings, bytes / kMiB)});
}

std::string OneLine(const std::string &text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out += c;
    }
    const size_t first = out.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first, out.find_last_not_of(' ') - first + 1);
}

Json ButtonView(Bindings &bindings, const Widget &button, const std::string &id, const char *variant)
{
    bindings.Forward(id, button, "press");
    return Json{{"id", id},
                {"label", button.label},
                {"variant", variant},
                {"disabled", !button.Offers("press")},
                {"reason", button.unavailable_reason},
                {"busy", bindings.Pending(id)}};
}

void AmountDraft::Open(const std::string &key, int origin, int other)
{
    open_ = true;
    key_ = key;
    origin_ = origin;
    other_ = other;
    target_ = origin;
    sent_ = false;
}

void AmountDraft::Follow(const std::string &key, int origin, int other, Bindings &bindings)
{
    if (!open_ || key != key_ || origin != origin_ || other != other_)
        Open(key, origin, other); // a new control, or the game's figures changed (a move went through)
    else if (sent_)
    {
        switch (bindings.AnsweredSince(floor_))
        {
        case Bindings::Answers::Refused: // the engine refused an action of the deal (logged by the router)
        // all applied, and the model after them still has the old figures: the game said no
        case Bindings::Answers::Applied:
            Open(key, origin, other);
            break;
        case Bindings::Answers::Waiting:
            if (bindings.Now() - sent_at_ >= kNoAnswer)
            {
                bindings.Error("the deal on " + key + " got no answer from the game in 5 s");
                Open(key, origin, other);
            }
            else
                bindings.RebuildAfter(sent_at_ + kNoAnswer - bindings.Now());
            break;
        }
    }
}

void AmountDraft::Close()
{
    open_ = false;
    key_.clear();
    sent_ = false;
}

int AmountDraft::Value(int low, int high) const
{
    return !open_ ? origin_ : std::clamp(target_, low, std::max(low, high));
}

void AmountDraft::Bind(Bindings &bindings, const std::string &id, int low, int high, const std::string &more_reason,
                       const std::string &less_reason, std::function<void(int)> moved)
{
    const auto set = [this, &bindings, low, high = std::max(low, high), more_reason, less_reason,
                      moved](const Event &event) {
        int wanted = 0;
        if (!EventNumber(bindings, event, wanted))
            return;
        // Past an end the purse or the room allows: the reason, once per push (a drag reports many samples).
        const std::string &reason = wanted > high ? more_reason : wanted < low ? less_reason : std::string();
        if (!reason.empty() && bindings.Now() - reason_at_ > 1.5)
        {
            reason_at_ = bindings.Now();
            bindings.Toast(reason);
        }
        if (!sent_)
        {
            target_ = std::clamp(wanted, low, high);
            if (moved)
                moved(target_);
        }
        bindings.Rebuild();
    };
    bindings.On(id, "change", set);
    bindings.On(id, "commit", set);
}

Json AmountDraft::Buttons(Bindings &bindings, int low, int high, Labels labels, const std::string &sub, bool disabled,
                          const std::string &reason, std::function<void(int)> commit)
{
    const int value = Value(low, high);
    const int delta = value - origin_;
    if (!open_ || (!sent_ && delta == 0))
        return Json();
    const std::string label = bindings.Text(delta > 0 ? labels.more : labels.less) + " " +
                              std::to_string(labels.absolute ? value : std::abs(delta));
    if (sent_)
    {
        // Sent, the game's answer not here yet: the same button waits, and a second press sends nothing (nor
        // what the screen's bar has in its place without a deal, such as a take-all).
        bindings.On("primary", "", [](const Event &) {});
        return Json{{"label", label}, {"sub", sub}, {"disabled", false}, {"reason", ""}, {"busy", true}};
    }
    bindings.On("primary", "", [this, &bindings, value, delta, disabled, commit](const Event &) {
        if (disabled || sent_)
            return;
        Sent(value, bindings);
        commit(delta);
        bindings.Rebuild();
    });
    return Json{{"label", label},
                {"sub", sub},
                {"disabled", disabled},
                {"reason", reason},
                {"busy", bindings.Pending("primary")}};
}

void AmountDraft::CancelAction(Bindings &bindings, Json &actions)
{
    if (!open_ || sent_ || target_ == origin_)
        return;
    actions.push_back(Json{{"id", "deal-cancel"},
                           {"label", bindings.Text("common.cancel")},
                           {"variant", "secondary"},
                           {"disabled", false},
                           {"reason", ""},
                           {"busy", false}});
    bindings.On("deal-cancel", "", [this, &bindings](const Event &) {
        target_ = origin_;
        bindings.Rebuild();
    });
}

void AmountDraft::Sent(int value, Bindings &bindings)
{
    target_ = value;
    sent_ = true;
    sent_at_ = bindings.Now();
    floor_ = bindings.RequestFloor();
}

bool AmountDraft::Drop()
{
    if (!open_ || sent_ || target_ == origin_)
        return false;
    target_ = origin_;
    return true;
}

void TapChoice(Bindings &bindings, const Widget &choice)
{
    const char *verb = choice.value == "1" && choice.Offers("activate") ? "activate"
                       : choice.Offers("select")                        ? "select"
                       : choice.Offers("open")                          ? "open"
                       : choice.Offers("activate")                      ? "activate"
                                                                        : nullptr;
    if (!verb)
        return;
    // Verbs the engine lists are bound to the row identity; the legacy plain select is not.
    const bool listed = std::find(choice.actions.begin(), choice.actions.end(), verb) != choice.actions.end();
    bindings.Send(choice, verb, listed ? choice.row_id : std::string());
}

Json ActionRow(Bindings &bindings, const Widget &widget, const std::string &id)
{
    const bool choice = widget.type == "choice";
    const bool usable =
        choice ? widget.Offers("select") || widget.Offers("activate") || widget.Offers("open") : widget.Offers("press");
    const Widget *target = &widget;
    if (choice)
        bindings.On(id, "", [&bindings, target](const Event &) { TapChoice(bindings, *target); });
    else
        bindings.Forward(id, widget, "press");
    std::string title = widget.label;
    std::replace(title.begin(), title.end(), '\t', ' ');
    return Json{{"kind", "action"},
                {"id", id},
                {"title", title},
                {"disabled", !usable},
                {"reason", widget.unavailable_reason},
                {"selected", choice && widget.value == "1"},
                {"busy", bindings.Pending(id)}};
}

void ControlRows(Bindings &bindings, std::span<const Widget *const> widgets, const std::string &id_prefix,
                 const std::vector<std::string> &labels, Json &rows)
{
    for (size_t i = 0; i < widgets.size(); ++i)
    {
        const Widget *widget = widgets[i];
        const std::string label = i < labels.size() ? labels[i] : std::string();
        const std::string id = id_prefix + std::to_string(i);
        if (widget->type == "check" && widget->exclusive)
        {
            // One radio group: the following options of the same node.
            size_t end = i;
            while (end < widgets.size() && widgets[end]->type == "check" && widgets[end]->exclusive &&
                   SameNode(widgets[end]->node, widget->node))
                ++end;
            const std::vector<const Widget *> group(widgets.begin() + static_cast<long>(i),
                                                    widgets.begin() + static_cast<long>(end));
            const bool segment = group.size() <= 4 && std::all_of(group.begin(), group.end(),
                                                                  [](const Widget *w) { return Short(w->label, 12); });
            if (segment)
            {
                Json options = Json::array();
                int selected = -1;
                bool usable = false;
                for (size_t k = 0; k < group.size(); ++k)
                {
                    options.push_back(group[k]->label);
                    if (group[k]->value == "1")
                        selected = static_cast<int>(k);
                    usable = usable || group[k]->Offers("toggle");
                }
                bindings.On(id, "change", [&bindings, group](const Event &event) {
                    size_t chosen = 0;
                    if (!EventNumber(bindings, event, chosen))
                        return;
                    if (chosen < group.size() && group[chosen]->Offers("toggle"))
                        bindings.Send(*group[chosen], "toggle", "1");
                });
                // The chosen option is drawn chosen in the tap's frame (the game's answer confirms it).
                bindings.Predict(id, "change", [id](const Event &event, Json &view) {
                    if (const std::optional<int> chosen = ParseNumber<int>(event.value))
                        Bindings::Patch(view, id, "selected", *chosen);
                });
                rows.push_back(Json{{"kind", "segment"},
                                    {"id", id},
                                    {"label", label},
                                    {"options", options},
                                    {"selected", selected},
                                    {"disabled", !usable},
                                    {"reason", widget->unavailable_reason}});
            }
            else
                for (size_t k = 0; k < group.size(); ++k)
                {
                    const std::string row_id = id_prefix + std::to_string(i + k);
                    const Widget *option = group[k];
                    bindings.On(row_id, "change",
                                [&bindings, option](const Event &) { bindings.Send(*option, "toggle", "1"); });
                    rows.push_back(Json{{"kind", "radio"},
                                        {"id", row_id},
                                        {"label", option->label},
                                        {"on", option->value == "1"},
                                        {"disabled", !option->Offers("toggle")},
                                        {"reason", option->unavailable_reason}});
                }
            i = end - 1;
        }
        else if (widget->type == "check")
        {
            bindings.On(id, "change", [&bindings, widget](const Event &event) {
                bindings.Send(*widget, "toggle", event.value == "1" ? "1" : "0");
            });
            // The switch is drawn where the finger put it in the tap's frame (the game's answer confirms it).
            bindings.Predict(id, "change", [id](const Event &event, Json &view) {
                Bindings::Patch(view, id, "on", event.value == "1");
            });
            rows.push_back(Json{{"kind", "toggle"},
                                {"id", id},
                                {"label", widget->label},
                                {"on", widget->value == "1"},
                                {"disabled", !widget->Offers("toggle")},
                                {"reason", widget->unavailable_reason}});
        }
        else if (widget->type == "slider")
        {
            const int span = std::max(1, widget->max - widget->min);
            const int position = Amount(bindings, widget->value, widget->node.c_str());
            const int percent = static_cast<int>(std::lround(100.0 * (position - widget->min) / span));
            bindings.Ignore(id, "change"); // drag progress: the position goes out on release
            bindings.On(id, "commit", [&bindings, widget, span](const Event &event) {
                const int step = std::max(1, widget->step);
                double share = 0.0;
                if (!EventNumber(bindings, event, share))
                    return;
                const double wanted = widget->min + share * span / 100.0;
                const int snapped = widget->min + static_cast<int>(std::lround((wanted - widget->min) / step)) * step;
                bindings.Send(*widget, "set", std::to_string(std::clamp(snapped, widget->min, widget->max)));
            });
            rows.push_back(Json{{"kind", "slider"},
                                {"id", id},
                                {"label", label.empty() ? widget->label : label},
                                {"value", std::clamp(percent, 0, 100)},
                                {"disabled", !widget->Offers("set")},
                                {"reason", widget->unavailable_reason}});
        }
        else if (widget->type == "stepper")
        {
            bindings.On(id + "-left", "", [&bindings, widget](const Event &) { bindings.Send(*widget, "left"); });
            bindings.On(id + "-right", "", [&bindings, widget](const Event &) { bindings.Send(*widget, "right"); });
            rows.push_back(Json{{"kind", "stepper"},
                                {"id", id},
                                {"label", label},
                                {"text", widget->value.empty() ? widget->label : widget->value},
                                {"left", widget->Offers("left")},
                                {"right", widget->Offers("right")}});
        }
        else if (widget->type == "edit")
        {
            const std::string title = label;
            bindings.On(id, "focus", [&bindings, widget, title](const Event &event) {
                if (widget->Offers("edit"))
                    bindings.EditText(event, *widget, title, false);
            });
            rows.push_back(Json{{"kind", "field"}, {"id", id}, {"label", label}, {"value", widget->value}});
        }
    }
}

} // namespace storm::ui::screens
