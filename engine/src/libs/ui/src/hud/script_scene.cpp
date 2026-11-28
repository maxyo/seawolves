// A HUD scene written in script (interface/touch/hud/ of the pack's scripts). The script builds what the scene shows
// from the states it reads (UiState) and answers the taps it binds; the runtime keeps what runs every frame or every
// touch move: the readings it lays into the view, the continuous controls, the geometry the engine is told, and the
// tapped choice drawn until the game answers it.
//
// The script's view: {layout, log (1: the event lines), holds (1: the scene keeps the HUD through a blocked game),
// elements.<id>.<props> (by the element's pack component; a prop may be a reading {reading, as, fill}),
// controls.<id>.{kind: hold {control} | helm {left, right} | lever {up, down} | stick | deck | wheel {moves, dims,
// captions.<move>} | fan}, covers.<id> = "map-labels" | "navigator", band.{after, row, under}, lingers.<id> = 1,
// sheet = "<part>" with its props}.
#include "hud.h"
#include "app_ui_names.hpp"
#include "../component_spec.h"
#include "../numbers.h"
#include "../screens/format.h"
#include "../screens/script_props.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>

namespace storm::ui::hud
{
namespace
{
const ViewTree *Child(const ViewTree &tree, const char *name)
{
    for (const ViewTree &child : tree.children)
        if (SameNode(child.name, name))
            return &child;
    return nullptr;
}

std::string Value(const ViewTree &tree, const char *name)
{
    const ViewTree *child = Child(tree, name);
    if (!child)
        throw std::runtime_error("view " + tree.name + ": no " + name);
    return child->value;
}

bool Flag(const ViewTree &tree, const char *name)
{
    const ViewTree *child = Child(tree, name);
    return child && child->value == "1";
}

// The changes `predicted` makes to `shown`: fields of objects, rows of lists by their `id`, other values whole.
Json Patch(const Json &shown, const Json &predicted)
{
    if (predicted.is_object() && shown.is_object())
    {
        Json patch = Json::object();
        for (const auto &[key, value] : predicted.items())
        {
            const auto was = shown.find(key);
            if (was == shown.end())
                patch[key] = value;
            else if (Json change = Patch(*was, value); !change.is_null())
                patch[key] = std::move(change);
        }
        return patch.empty() ? Json() : patch;
    }
    if (predicted.is_array() && shown.is_array() && !predicted.empty() && predicted[0].is_object() &&
        predicted[0].contains("id"))
    {
        Json rows = Json::object();
        for (const Json &row : predicted)
            for (const Json &before : shown)
                if (before.is_object() && before.value("id", Json()) == row.value("id", Json()))
                    if (Json change = Patch(before, row); !change.is_null())
                        rows[row["id"].dump()] = std::move(change);
        return rows.empty() ? Json() : Json{{"$rows", rows}};
    }
    return predicted == shown ? Json() : predicted;
}

void Apply(Json &target, const Json &patch)
{
    if (patch.is_object() && patch.contains("$rows") && target.is_array())
    {
        for (Json &row : target)
            if (row.is_object() && row.contains("id"))
                if (const auto found = patch["$rows"].find(row["id"].dump()); found != patch["$rows"].end())
                    Apply(row, *found);
        return;
    }
    if (patch.is_object() && target.is_object())
    {
        for (const auto &[key, value] : patch.items())
            Apply(target[key], value);
        return;
    }
    target = patch;
}

// The walk stick (control "stick"): the thumb as the engine's Move. The engine holds a stick from its "1" packet until
// its "0" packet (events, no clock), so every run of "1" packets ends with exactly one "0" in the session it was said
// in, whatever the HUD does meanwhile: the thumb lifted while the HUD is not interactive, the HUD stopping being
// interactive under a held thumb (a fader, a dialogue), a new movement session and the scene's release all say it (a
// session the engine has already ended drops it as stale). A thumb still down is said again once the HUD is
// interactive in a live session.
class WalkStick
{
  public:
    explicit WalkStick(Hud &hud) : hud_(hud)
    {
    }
    // The stick's "x,y": ±100, y growing downwards; the engine takes ±1000, forward up.
    void Set(const std::string &value)
    {
        const size_t comma = value.find(',');
        if (comma == std::string::npos)
        {
            hud_.Error("HUD scene: the move stick reported '" + value + "', expected 'x,y'");
            return;
        }
        x_ = static_cast<int>(std::clamp(std::strtod(value.c_str(), nullptr), -100.0, 100.0) * 10.0);
        y_ = static_cast<int>(std::clamp(std::strtod(value.c_str() + comma + 1, nullptr), -100.0, 100.0) * -10.0);
        Sync(hud_.Current().gameplay);
    }
    void Release(const Gameplay &gameplay)
    {
        x_ = y_ = 0;
        Sync(gameplay);
    }
    bool Busy() const
    {
        return moving_ || x_ != 0 || y_ != 0;
    }
    void Sync(const Gameplay &gameplay)
    {
        const bool held = x_ != 0 || y_ != 0;
        const bool live = gameplay.analog_movement && hud_.Interactive();
        const bool same = gameplay.input_owner == owner_ && gameplay.movement_generation == generation_;
        if (moving_ && (!held || !live || !same))
        {
            moving_ = false;
            Say(false);
        }
        if (held && live && (!moving_ || x_ != said_x_ || y_ != said_y_))
        {
            moving_ = true;
            owner_ = gameplay.input_owner;
            generation_ = gameplay.movement_generation;
            said_x_ = x_;
            said_y_ = y_;
            Say(true);
        }
    }

  private:
    void Say(bool active)
    {
        hud_.Send(Move{owner_, generation_, active, active ? x_ : 0, active ? y_ : 0}, "move");
    }
    Hud &hud_;
    bool moving_ = false;
    int x_ = 0, y_ = 0;           // the thumb now (0, 0: up)
    int said_x_ = 0, said_y_ = 0; // what the engine was last told while moving_
    std::string owner_;
    uint64_t generation_ = 0;
};

std::vector<std::string> Split(const std::string &text)
{
    std::vector<std::string> words;
    std::istringstream in(text);
    for (std::string word; in >> word;)
        words.push_back(word);
    return words;
}

class ScriptScene final : public Scene, private ScriptCall
{
  public:
    ScriptScene(Hud &hud, const ScriptScreen &screen, ScriptPort &port) : hud_(hud), screen_(screen), port_(port)
    {
    }

    void Build(const States &states, View &view, Input &input) override
    {
        states_ = &states;
        const LandCommands &commands = states.land_commands;
        if (commands.known && commands.active && !commands.complete)
        {
            // The engine marked its menu as something no sheet can show. The engine never draws its own menu under
            // the touch HUD, so an invisible menu would hold the player: close it and report it.
            hud_.Error("land commands: the engine's menu is not drawable here (stage " + commands.stage + "); closed");
            hud_.Send(MenuStep{MenuStep::Menu::Land, commands.revision, "close", ""});
        }
        const SeaOrders &orders = states.sea_orders;
        if (orders.known && orders.active && !orders.complete)
            // The engine marked its menu as something only it can draw: it stays on the game's own surface (no
            // coverage is acknowledged).
            hud_.Error("sea orders: the engine's menu is not drawable here (stage " + orders.stage +
                       "); the game's own menu stays");
        if (!MenuOf(choice_menu_).known || !MenuOf(choice_menu_).active)
            choice_.clear(); // a tapped row of a menu that closed is over
        if (first_ || rebuild_ || !SameStructure(states))
            Run();
        Compose(states, view, input);
    }

    double Tick(const States &states, double now) override
    {
        double next = kIdle;
        holds_.Forget(states);
        if (stick_.Busy())
            stick_.Sync(states.gameplay);
        if ((walk_.x || walk_.y) && (!hud_.Interactive() || !controls_.contains(deck_)))
            Walk(0, 0); // the engine stops a walk the gameplay no longer binds (android_deck_walk.hpp)
        for (auto &[id, fan] : fans_)
            fan.Expire(now, next);
        // A tapped row stays drawn until the game answers its step: refused, or applied and its menu revision drawn
        // (activated, or not selected after all).
        if (!choice_.empty() && ChoiceEnded(MenuOf(choice_menu_)))
        {
            choice_.clear();
            hud_.Rebuild();
        }
        for (auto &[id, linger] : lingers_)
            if (linger.lost >= 0.0)
            {
                const double fade = linger.lost + hud_.Seconds("hintHoldMs"), gone = fade + hud_.Seconds("toastLeave");
                if ((now >= fade && !linger.leaving) || now >= gone)
                    hud_.Rebuild();
                else
                    next = std::min(next, (now < fade ? fade : gone) - now);
            }
        if (overlay_ && Ended(*overlay_))
            hud_.Rebuild(); // the answer came without a state change: the mark goes
        return next;
    }

    void Drawn(const States &states) override
    {
        drawn_ = elements_;
        Cover(&states);
        CoverNavigator(states, hud_.Interactive());
        CoverOrders(states, hud_.Interactive() && hud_.Shown().modal);
        FollowBand();
        // A tapped row goes on once the sheet showing the menu's new revision is on the screen (select, then activate
        // against the revision that reports the selection).
        const Menu &menu = MenuOf(choice_menu_);
        if (menu.known && menu.active && menu.complete && hud_.Interactive() && hud_.Shown().modal)
            Follow(menu);
    }
    void Hidden(const States &) override
    {
        Cover(nullptr);
        CoverNavigator(hud_.Current(), false);
        CoverOrders(hud_.Current(), false);
        Fold();
    }
    void Released(const States &states) override
    {
        holds_.Release(states.gameplay);
        stick_.Release(states.gameplay);
        if (walk_.x || walk_.y)
            Walk(0, 0);
        wheel_open_ = false;
        wheel_move_.clear();
        if (fights_ && states.combat.known && states.combat.mode != "unknown")
            Fight(states.combat, "all", "cancel");
        hud_.Rebuild();
    }
    void Suspended(const States &states) override
    {
        Cover(nullptr);
        CoverNavigator(states, false);
        CoverOrders(states, false);
        overlay_.reset();
        first_ = true; // the player comes back to a scene whose own state starts afresh
        const LandCommands &commands = states.land_commands;
        if (commands.known && commands.active)
            hud_.Send(MenuStep{MenuStep::Menu::Land, commands.revision, "close", ""});
        const SeaOrders &orders = states.sea_orders;
        if (orders.known && orders.active)
            hud_.Send(MenuStep{MenuStep::Menu::Sea, orders.revision, "close", ""});
        choice_.clear();
        Fold();
        lingers_.clear();
    }
    // Whether the scene holds is the script's, from the states now: a state it read changed since its last build
    // (the menu it held for closed) asks it again.
    bool Holds(const States &states) const override
    {
        if (!first_ && !SameStructure(states))
        {
            auto &scene = const_cast<ScriptScene &>(*this);
            scene.states_ = &states;
            scene.Run();
        }
        return holds_view_;
    }
    bool Back(const States &states) override
    {
        for (auto &[id, fan] : fans_)
            if (fan.Back())
                return true;
        if (back_.empty())
            return false;
        Tap({back_, "", ""}, states);
        return true;
    }

  private:
    struct Slot
    {
        std::string element;
        Json::json_pointer at;
        ReadingRef reading;
    };
    struct Control
    {
        std::string kind, a, b; // hold {control}, helm {left, right}, lever {up, down}, wheel {moves, dims}
        std::map<std::string, std::string> captions; // wheel: each move's name
    };
    struct Overlay
    {
        Json patch;
        std::vector<uint64_t> requests;
        bool choice = false; // a menu row on its way: drawn until the choice ends
    };
    // An element the script gives up that stays a little (lingers.<id>): what it showed, when it was lost.
    struct Linger
    {
        Json props;
        double lost = -1.0;
        bool leaving = false;
    };

    // --- the build -------------------------------------------------------------------------

    bool SameStructure(const States &states) const
    {
        for (const auto &[channel, state] : read_)
            if (ScriptState(channel, states, hud_) != state)
                return false;
        return true;
    }

    void Run()
    {
        read_.clear();
        bound_.clear();
        back_.clear();
        view_.reset();
        building_ = true;
        struct Done
        {
            bool &flag;
            ~Done()
            {
                flag = false;
            }
        } done{building_};
        port_.Build(screen_, {}, *this);
        first_ = false;
        rebuild_ = false;
        if (!view_)
            throw std::runtime_error("HUD scene " + screen_.capability + " showed no view");
        Parse(*view_);
    }

    void Parse(const ViewTree &view)
    {
        layout_ = Value(view, "layout");
        log_ = Flag(view, "log");
        holds_view_ = Flag(view, "holds");
        elements_ = Json::object();
        slots_.clear();
        controls_.clear();
        covers_.clear();
        sheet_part_.clear();
        sheet_ = Json::object();
        fights_ = false;
        deck_.clear();
        band_view_ = {};
        if (const ViewTree *band = Child(view, "band"))
            band_view_ = {Value(*band, "after"), Split(Value(*band, "row")), Value(*band, "under")};
        if (const ViewTree *elements = Child(view, "elements"))
            for (const ViewTree &element : elements->children)
                elements_[element.name] = Props(element);
        if (const ViewTree *controls = Child(view, "controls"))
            for (const ViewTree &control : controls->children)
                ParseControl(control);
        std::set<std::string> lingering;
        if (const ViewTree *lingers = Child(view, "lingers"))
            for (const ViewTree &linger : lingers->children)
                lingering.insert(linger.name);
        // An element no longer said to linger goes at once.
        std::erase_if(lingers_, [&](const auto &linger) { return !lingering.count(linger.first); });
        for (const std::string &id : lingering)
            lingers_[id];
        if (const ViewTree *sheet = Child(view, "sheet"))
        {
            const ComponentSpec *spec = hud_.Part(sheet->value);
            if (!spec)
                throw std::runtime_error("the pack has no part " + sheet->value + " for the sheet");
            sheet_part_ = sheet->value;
            sheet_ = screens::ScriptProps(*sheet, *spec);
        }
        if (const ViewTree *covers = Child(view, "covers"))
            for (const ViewTree &cover : covers->children)
            {
                if (cover.value != "map-labels" && cover.value != "navigator")
                    throw std::runtime_error("cover " + cover.name + ": no kind '" + cover.value + "'");
                covers_[cover.name] = cover.value;
            }
    }

    void ParseControl(const ViewTree &control)
    {
        const std::string kind = Value(control, "kind");
        Control parsed{kind, {}, {}, {}};
        if (kind == "hold")
            parsed.a = Value(control, "control");
        else if (kind == "helm")
            parsed.a = Value(control, "left"), parsed.b = Value(control, "right");
        else if (kind == "lever")
            parsed.a = Value(control, "up"), parsed.b = Value(control, "down");
        else if (kind == "wheel")
        {
            parsed.a = Value(control, "moves");
            parsed.b = Value(control, "dims");
            if (const ViewTree *captions = Child(control, "captions"))
                for (const ViewTree &caption : captions->children)
                    parsed.captions[caption.name] = caption.value;
            fights_ = true;
        }
        else if (kind == "stick")
            fights_ = true; // the land's: cancelled input also cancels the combat holds
        else if (kind == "deck")
            deck_ = control.name;
        else if (kind == "fan")
            fans_.try_emplace(control.name, hud_, control.name);
        else
            throw std::runtime_error("control " + control.name + ": no kind '" + kind + "'");
        controls_[control.name] = std::move(parsed);
        // A held control is drawn where it is declared; the stick, the wheel and the fan are the script's elements.
        if ((kind == "hold" || kind == "helm" || kind == "lever") && !elements_.contains(control.name))
            elements_[control.name] = Json::object();
    }

    // An element's props by its component; each reading is taken out into a slot and its value of now put in.
    Json Props(const ViewTree &element)
    {
        const ComponentSpec *spec = hud_.Component(layout_, element.name);
        if (!spec)
            throw std::runtime_error("the layout scene " + layout_ + " has no element " + element.name);
        ViewTree props = element;
        std::vector<std::pair<std::vector<size_t>, ReadingRef>> found;
        std::vector<size_t> path;
        TakeReadings(props, path, found);
        Json shaped = screens::ScriptProps(props, *spec);
        for (const auto &[at, reading] : found)
            slots_.push_back({element.name, Pointer(props, shaped, at), reading});
        return shaped;
    }

    void TakeReadings(ViewTree &node, std::vector<size_t> &path,
                      std::vector<std::pair<std::vector<size_t>, ReadingRef>> &found)
    {
        if (Child(node, "reading"))
        {
            const ViewTree *fill = Child(node, "fill");
            ReadingRef reading{Value(node, "reading"), Value(node, "as"), fill ? fill->value : ""};
            if (!reading.Known(*states_, hud_))
                throw std::runtime_error("the reading " + reading.readings + " is placed while the engine gives none");
            const Json value = reading.Resolve(*states_, hud_);
            node.children.clear();
            node.value = value.is_string() ? value.get<std::string>() : value.dump();
            found.emplace_back(path, std::move(reading));
            return;
        }
        for (size_t i = 0; i < node.children.size(); ++i)
        {
            path.push_back(i);
            TakeReadings(node.children[i], path, found);
            path.pop_back();
        }
    }

    // Where the node at `at` (child indices from the element) is in the props shaped from it.
    static Json::json_pointer Pointer(const ViewTree &element, const Json &shaped, const std::vector<size_t> &at)
    {
        Json::json_pointer pointer;
        const ViewTree *node = &element;
        const Json *json = &shaped;
        for (const size_t index : at)
        {
            node = &node->children[index];
            if (json->is_array())
            {
                pointer /= index;
                json = &json->at(index);
                continue;
            }
            std::string key = node->name;
            for (const auto &item : json->items())
                if (SameNode(item.key(), node->name))
                    key = item.key();
            pointer /= key;
            json = &json->at(key);
        }
        return pointer;
    }

    // --- every state change ----------------------------------------------------------------

    void Compose(const States &states, View &view, Input &input)
    {
        view.layout = layout_;
        view.log = log_;
        view.elements = elements_;
        for (const Slot &slot : slots_)
        {
            Json &target = view.elements[slot.element][slot.at];
            const Json value = slot.reading.Resolve(states, hud_);
            if (target.is_number() && value.is_string())
                target = Json::parse(value.get<std::string>());
            else
                target = target.is_string() && !value.is_string() ? Json(value.dump()) : value;
        }
        if (overlay_ && Ended(*overlay_))
            overlay_.reset();
        Json sheet = sheet_;
        if (overlay_)
        {
            Json shown = {{"elements", std::move(view.elements)}, {"sheet", std::move(sheet)}};
            Apply(shown, overlay_->patch);
            view.elements = std::move(shown["elements"]);
            sheet = std::move(shown["sheet"]);
        }
        if (log_)
            if (Json lines = hud_.LogLines(); !lines.empty())
            {
                Json &log = view.elements["log"] = Json{{"lines", std::move(lines)}};
                if (band_.left > 0)
                    log["safeLeft"] = band_.left;
                if (band_.right > 0)
                    log["safeRight"] = band_.right;
                if (band_.top > 0)
                    log["offset"] = band_.top;
            }
        for (const auto &[id, kind] : covers_)
            if (kind == "navigator")
                input.On(id, "zoom", [this](const Event &) { Zoom(); });
        view.back = !back_.empty();
        for (const std::string &bound : bound_)
        {
            const size_t split = bound.find('\n');
            input.On(bound.substr(0, split), bound.substr(split + 1),
                     [this](const Event &event) { Tap(event, *states_); });
        }
        for (const auto &[id, control] : controls_)
            BindControl(id, control, view, input);
        Lingering(view);
        if (!sheet_part_.empty())
        {
            Fold(); // the sheet takes every touch: a picker open under it folds
            view.sheet = sheet_part_;
            view.sheet_props = std::move(sheet);
            view.modal = true;
        }
        for (auto &[id, fan] : fans_)
            if (controls_.contains(id))
                Picker(id, fan, view, input);
            else
                fan.Close(); // not offered now: an open one goes, and does not come back with its button
    }

    // The elements that linger: shown while the script gives them; then held as they were for the pack's hintHoldMs
    // (one that comes straight back does not blink), then `leaving` for its toastLeave, then gone.
    void Lingering(View &view)
    {
        const double now = hud_.Now();
        for (auto &[id, linger] : lingers_)
        {
            if (const auto shown = view.elements.find(id); shown != view.elements.end())
            {
                linger = {*shown, -1.0, false};
                (*shown)["leaving"] = false;
                continue;
            }
            if (linger.props.is_null())
                continue;
            if (linger.lost < 0.0)
                linger.lost = now;
            const double fade = linger.lost + hud_.Seconds("hintHoldMs");
            if (now >= fade + hud_.Seconds("toastLeave"))
            {
                linger = {};
                continue;
            }
            linger.leaving = now >= fade;
            view.elements[id] = linger.props;
            view.elements[id]["leaving"] = linger.leaving;
        }
    }

    // A picker's slots <id>-1…N are the script's elements: drawn while it is open, in place of what each "replaces".
    void Picker(const std::string &id, Fan &fan, View &view, Input &input)
    {
        if (!view.elements.contains(id))
        {
            fan.Close();
            return;
        }
        std::vector<Fan::Option> options;
        std::vector<std::string> slots;
        for (int k = 1;; ++k)
        {
            const std::string slot = id + "-" + std::to_string(k);
            const auto found = view.elements.find(slot);
            if (found == view.elements.end())
                break;
            Fan::Option option{*found, {}};
            if (bound_.count(slot + "\nclick"))
                option.pick = [this, slot] { Tap({slot, "click", ""}, *states_); };
            options.push_back(std::move(option));
            slots.push_back(slot);
            view.elements.erase(found);
        }
        Json button = view.elements[id];
        fan.Build(std::move(button), options, view, input, [this, id] {
            for (auto &[other, picker] : fans_)
                if (other != id)
                    picker.Close(); // one picker open at a time
        });
        if (!fan.Open())
            return;
        for (const std::string &slot : slots)
            if (const LayoutElement *element = hud_.Element(layout_, slot))
                for (const std::string &under : element->replaces)
                    view.elements.erase(under);
    }

    void Fold()
    {
        for (auto &[id, fan] : fans_)
            fan.Close();
    }

    void BindControl(const std::string &id, const Control &control, View &view, Input &input)
    {
        if (control.kind == "stick")
            // Bound whether or not the view shows the stick: a finger that was on it when a sheet opened lifts later,
            // and that release must stop the hero.
            input.On(id, "change", [this](const Event &event) { stick_.Set(event.value); });
        else if (control.kind == "wheel")
            Wheel(id, control, view, input);
        else if (control.kind == "deck")
            input.On(id, "change", [this](const Event &event) { Deck(event.value); });
        else if (control.kind == "fan")
            return;
        else if (control.kind == "hold")
        {
            input.On(id, "down", [this, name = control.a](const Event &) { holds_.Hold(name, true); });
            input.On(id, "up", [this, name = control.a](const Event &) { holds_.Hold(name, false); });
            input.Ignore(id, "click");
        }
        else if (control.kind == "helm")
            input.On(id, "change",
                     [this, control](const Event &event) { holds_.Helm(event.value, control.a, control.b); });
        else
            // The lever pushed up or down holds that side; let go, neither.
            input.On(id, "hold", [this, control](const Event &event) {
                const bool up = event.value == "up", down = event.value == "down";
                holds_.Hold(up ? control.b : control.a, false);
                if (up || down)
                    holds_.Hold(up ? control.a : control.b, true);
                else
                    holds_.Hold(control.b, false);
            });
    }

    // The sword-move wheel: the strike button's swipe opens it on a move, a drag previews another, the lift commits.
    // The script builds the wheel's element (`moves`); it is drawn only while the wheel is open, on the move previewed,
    // with the elements `dims` names subdued under it.
    void Wheel(const std::string &id, const Control &control, View &view, Input &input)
    {
        const auto open = [this](const Event &event) { Turn(true, event.value); };
        input.On(id, "open", open);
        input.On(id, "preview", open);
        input.On(id, "cancel", [this](const Event &) { Turn(false, {}); });
        input.On(id, "commit", [this](const Event &event) {
            Turn(false, {});
            Strike(event.value, "tap");
        });
        input.Ignore(id, "click");
        const auto moves = view.elements.find(control.a);
        if (moves == view.elements.end())
            return;
        if (!wheel_open_ || !view.elements.contains(id) || !CombatCurrent(*states_))
        {
            view.elements.erase(moves);
            return;
        }
        std::string sector = "centre";
        for (const Json &move : (*moves)["moves"])
            if (move.value("id", "") == wheel_move_)
                sector = move.value("sector", "centre");
        (*moves)["selected"] = sector;
        const auto caption = control.captions.find(wheel_move_);
        (*moves)["caption"] = caption == control.captions.end() ? "" : caption->second;
        for (const std::string &dim : Split(control.b))
            if (view.elements.contains(dim))
                view.dimmed.push_back(dim);
    }

    void Turn(bool open, const std::string &move)
    {
        if (wheel_open_ == open && wheel_move_ == move)
            return;
        wheel_open_ = open;
        wheel_move_ = move;
        hud_.Rebuild();
    }

    // --- the land's combat and menu ----------------------------------------------------------

    // A combat action, when the engine still offers it to the player the HUD shows.
    void Strike(const std::string &action, const std::string &phase)
    {
        const States &states = hud_.Current();
        const Combat &combat = states.combat;
        if (!hud_.Interactive() || !CombatCurrent(states) || !combat.Has(action))
            return;
        if (action == "pistol" && combat.pistol != "unknown" && combat.pistol != "loaded")
            return;
        Fight(combat, action, phase);
    }

    void Fight(const Combat &combat, const std::string &action, const std::string &phase)
    {
        const auto offered = combat.actions.find(action);
        const uint64_t generation = offered == combat.actions.end() ? 0 : offered->second;
        hud_.Send(CombatAction{combat.activity, generation, action, phase});
    }

    // The engine's menu a "menu" command names; its state now.
    const Menu &MenuOf(MenuStep::Menu menu) const
    {
        const States &states = hud_.Current();
        if (menu == MenuStep::Menu::Sea)
            return states.sea_orders;
        return states.land_commands;
    }
    // What a row id means in: the stage (rows are numbered per stage) and, at sea, the ship and the scene.
    std::string ChoiceContext(MenuStep::Menu kind, const Menu &menu) const
    {
        if (kind != MenuStep::Menu::Sea)
            return menu.stage;
        return menu.stage + "\t" + static_cast<const SeaOrders &>(menu).recipient + "\t" + std::to_string(menu.scene);
    }

    // A step of a menu the engine allows now; the number of the request when it is one of a tapped row's.
    uint64_t Step(MenuStep::Menu kind, const std::string &verb, const std::string &id)
    {
        const Menu &menu = MenuOf(kind);
        if (!hud_.Interactive() || !menu.known)
            return 0;
        if (verb == "open" ? !menu.available || menu.active : !menu.active)
            return 0;
        if (kind == MenuStep::Menu::Land && verb == "open" && !id.empty())
        {
            const auto &offered = hud_.Current().land_commands.offered;
            if (std::find(offered.begin(), offered.end(), id) == offered.end())
                return 0;
        }
        if ((verb == "back" && !menu.can_back) || (verb == "close" && !menu.can_close))
            return 0;
        if (verb == "open")
        {
            // The menu takes the screen: what the finger holds is let go first.
            holds_.Release(hud_.Current().gameplay);
            if (walk_.x || walk_.y)
                Walk(0, 0);
        }
        if (verb == "selectRecipient" || verb == "back" || verb == "close")
        {
            // A delayed selection reply must not activate an abandoned row while the menu moves.
            choice_.clear();
            hud_.Rebuild();
        }
        const MenuStep step{kind, menu.revision, verb, id};
        if (verb == "selectItem" || verb == "activateItem")
            return hud_.Request(step);
        hud_.Send(step);
        return 0;
    }

    bool ChoiceEnded(const Menu &menu) const
    {
        const RequestLedger::Status status = hud_.Answered(choice_request_);
        if (status == RequestLedger::Status::Shown)
            return choice_activated_ || menu.selected != choice_;
        return status == RequestLedger::Status::Refused || status == RequestLedger::Status::None;
    }

    // A row tapped acts at once. The engine wants it selected first and activated second, each against the menu
    // revision it answers to: the second step follows the revision that reports the row as selected. A tap while
    // another waits replaces it unless that one is being activated already. True when the tap is on its way.
    bool Choose(MenuStep::Menu kind, const std::string &id)
    {
        const Menu &menu = MenuOf(kind);
        const Menu::Item *item = menu.Find(id);
        if (!choice_.empty() && (kind != choice_menu_ || ChoiceContext(kind, menu) != choice_context_))
            choice_.clear(); // a tap of the last stage (or ship) is over
        if (!item || !item->enabled || (!choice_.empty() && (choice_activated_ || choice_ == id)))
            return false;
        choice_ = id;
        choice_menu_ = kind;
        choice_context_ = ChoiceContext(kind, menu);
        choice_request_ = 0;
        choice_revision_ = 0;
        choice_activated_ = false;
        Follow(menu);
        return !choice_.empty();
    }

    void Follow(const Menu &menu)
    {
        if (!choice_.empty() && ChoiceContext(choice_menu_, menu) != choice_context_)
        {
            // The menu went on to another stage or ship (its rows reuse the ids): the tap belonged to the last one.
            choice_.clear();
            hud_.Rebuild();
            return;
        }
        if (choice_.empty() || choice_revision_ == menu.revision)
            return;
        const Menu::Item *item = menu.Find(choice_);
        if (!item || !item->enabled || choice_activated_)
        {
            choice_.clear(); // activated (the menu moved on) or gone: the tap is over
            hud_.Rebuild();
            return;
        }
        choice_revision_ = menu.revision;
        choice_activated_ = menu.selected == choice_;
        if (choice_activated_ && item->kind == "travel")
            hud_.ExpectLandTransition("the travel place '" + item->label + "'", hud_.Current().land_commands, choice_);
        choice_request_ = Step(choice_menu_, choice_activated_ ? "activateItem" : "selectItem", choice_);
        if (!choice_request_)
        {
            choice_.clear(); // not sent: nothing is on its way
            hud_.Rebuild();
        }
    }

    bool Ended(const Overlay &overlay) const
    {
        if (overlay.choice)
            return choice_.empty() || ChoiceContext(choice_menu_, MenuOf(choice_menu_)) != choice_context_;
        for (const uint64_t request : overlay.requests)
            if (const auto status = hud_.Answered(request);
                status == RequestLedger::Status::Waiting || status == RequestLedger::Status::Applied)
                return false;
        return !overlay.requests.empty();
    }

    void Tap(const Event &event, const States &states)
    {
        states_ = &states;
        requests_.clear();
        predicted_.reset();
        chose_ = false;
        tapped_ = &event;
        struct Done
        {
            const Event *&tapped;
            ~Done()
            {
                tapped = nullptr;
            }
        } done{tapped_};
        port_.Event(screen_, event.id, event.verb, event.value, *this);
        // The mark of a tap that sent the game nothing (a command not offered now) is not drawn: there is no answer
        // to end it; a mark of the scene's own state is its next build's.
        if (predicted_ && (!requests_.empty() || chose_))
        {
            Json patch = Patch(Json{{"elements", elements_}, {"sheet", sheet_}}, *predicted_);
            if (!patch.is_null())
                overlay_ = Overlay{std::move(patch), requests_, chose_};
        }
        hud_.Rebuild();
    }

    // What the world map is told of the compass's box on the screen, so that it draws no label under it.
    void Cover(const States *states)
    {
        const WorldMap *map = nullptr;
        if (states && hud_.Interactive() && states->world_map.known && states->world_map.available)
            map = &states->world_map;
        std::string id;
        for (const auto &[element, kind] : covers_)
            id = element;
        Placement box;
        MapLabelArea area{map ? map->revision : label_revision_, std::nullopt};
        if (map && !id.empty() && hud_.Shown().elements.contains(id) && hud_.Measure(id, "", box) &&
            box.screen_width > 0 && box.screen_height > 0 && box.width > 0 && box.height > 0)
        {
            const auto px = [](float value) { return static_cast<uint32_t>(std::lround(value)); };
            area.area = std::array<uint32_t, 6>{px(box.screen_width), px(box.screen_height), px(box.x), px(box.y),
                                                px(box.width),        px(box.height)};
        }
        std::string geometry = "none";
        if (area.area)
            for (const uint32_t value : *area.area)
                geometry += ":" + std::to_string(value);
        const std::string owner = map ? std::to_string(map->revision) + "\t" + std::to_string(map->activity) : "";
        if (!map)
        {
            if (!label_owner_.empty() && label_geometry_ != "none")
                hud_.Send(area, "worldmap-labelarea");
            label_geometry_ = "none";
            return;
        }
        if (owner != label_owner_ || geometry != label_geometry_)
        {
            label_owner_ = owner;
            label_revision_ = map->revision;
            label_geometry_ = geometry;
            hud_.Send(area, "worldmap-labelarea");
        }
    }

    // --- the sea ------------------------------------------------------------------------------

    // The ship of the gameplay the HUD shows; an older or newer scene's is not ours.
    static const Ship *CurrentShip(const States &states)
    {
        const Ship &ship = states.ship;
        return ship.known && ship.gameplay_revision == states.gameplay.revision ? &ship : nullptr;
    }
    static std::string NavigatorOwner(const Ship &ship)
    {
        return std::to_string(ship.gameplay_revision) + "\t" + std::to_string(ship.hud_revision) + "\t" +
               std::to_string(ship.navigator_generation);
    }
    Navigator NavigatorCommand(const Ship &ship, Navigator::Verb verb) const
    {
        return {ship.gameplay_revision, ship.hud_revision, ship.navigator_generation, verb, navigator_layout_, {}};
    }

    // The engine draws its radar into the disc of the element covered "navigator". It needs the place of the disc,
    // once per owner and geometry, and, once it has drawn there, the word that our readouts around it are on the
    // screen: only then does it drop its own. An owner the HUD does not draw the disc for is cleared, also one never
    // prepared: the engine keeps its compass off until it hears either (android_hud_presenter.hpp).
    void CoverNavigator(const States &states, bool drawn)
    {
        std::string id;
        for (const auto &[element, kind] : covers_)
            if (kind == "navigator")
                id = element;
        const Ship *ship = drawn ? CurrentShip(states) : nullptr;
        Placement disc;
        if (!ship || !ship->navigator || id.empty() || !hud_.Shown().elements.contains(id) ||
            !hud_.Measure(id, ".mm-disc", disc))
        {
            navigator_prepared_ = false;
            const Ship *known = CurrentShip(states);
            if (known && navigator_cleared_ != NavigatorOwner(*known))
            {
                navigator_cleared_ = NavigatorOwner(*known);
                hud_.Send(NavigatorCommand(*known, Navigator::Verb::Clear), "navigator-prepare");
            }
            return;
        }
        const std::string owner = NavigatorOwner(*ship);
        const std::array<long, 7> geometry = {std::lround(disc.screen_width),
                                              std::lround(disc.screen_height),
                                              std::lround(disc.x + disc.width * 0.5f),
                                              std::lround(disc.y + disc.height * 0.5f),
                                              std::lround(std::min(disc.width, disc.height) * 0.5f),
                                              std::lround(disc.density * 1000.0f),
                                              states.gameplay.language == "russian" ? 1 : 0};
        if (!navigator_prepared_ || owner != navigator_owner_ || geometry != navigator_geometry_)
        {
            navigator_prepared_ = true;
            navigator_owner_ = owner;
            navigator_geometry_ = geometry;
            navigator_cleared_.clear();
            navigator_visible_ = 0;
            ++navigator_layout_;
            Navigator prepare = NavigatorCommand(*ship, Navigator::Verb::Prepare);
            prepare.geometry = geometry;
            hud_.Send(prepare, "navigator-prepare");
        }
        if (ship->navigator_layout == navigator_layout_ && navigator_visible_ != navigator_layout_)
        {
            navigator_visible_ = navigator_layout_;
            hud_.Send(NavigatorCommand(*ship, Navigator::Verb::Visible), "navigator-visible");
        }
    }

    // One tap on the radar = the next scale: closer until the engine has no closer one, then back out.
    void Zoom()
    {
        const Ship *ship = CurrentShip(hud_.Current());
        if (!hud_.Interactive() || !ship || !navigator_prepared_ || navigator_owner_ != NavigatorOwner(*ship))
            return;
        if (zoom_in_ ? !ship->zoom_in : !ship->zoom_out)
            zoom_in_ = !zoom_in_;
        if (zoom_in_ ? !ship->zoom_in : !ship->zoom_out)
            return; // the engine has one scale only
        hud_.Send(NavigatorCommand(*ship, zoom_in_ ? Navigator::Verb::In : Navigator::Verb::Out));
    }

    // The sheet over the scene shows exactly this revision of the sea orders: only now may the engine stop drawing
    // its own menu. The engine keeps the word until the menu changes.
    void CoverOrders(const States &states, bool drawn)
    {
        const SeaOrders &orders = states.sea_orders;
        if (drawn && !sheet_part_.empty() && orders.known && orders.active && orders.complete)
        {
            if (orders.revision != orders_covered_)
            {
                orders_covered_ = orders.revision;
                hud_.Send(MenuStep{MenuStep::Menu::Sea, orders.revision, "shown", "1"}, "orders-ack");
            }
            return;
        }
        if (!orders_covered_)
            return;
        // A menu revision the engine has replaced needs no word: its coverage ended with it.
        if (orders.known && orders.revision == orders_covered_)
            hud_.Send(MenuStep{MenuStep::Menu::Sea, orders.revision, "shown", "0"}, "orders-ack");
        orders_covered_ = 0;
    }

    // The deck walk stick: its direction, and how far it is pushed along the stick curve.
    void Deck(const std::string &value)
    {
        const size_t comma = value.find(',');
        if (comma == std::string::npos)
        {
            hud_.Error("HUD scene: the deck stick reported '" + value + "', expected 'x,y'");
            return;
        }
        const double x = std::strtod(value.c_str(), nullptr), down = std::strtod(value.c_str() + comma + 1, nullptr);
        const double length = std::hypot(x, down);
        const int strength = StickStrength(length);
        if (!strength || !hud_.Interactive())
            return Walk(0, 0);
        // y grows downwards: up walks forward.
        Walk(static_cast<int>(std::lround(x / length * strength)),
             static_cast<int>(std::lround(-down / length * strength)));
    }
    void Walk(int x, int y)
    {
        if (x == walk_.x && y == walk_.y)
            return;
        walk_ = {x, y};
        hud_.Send(DeckWalk{hud_.Current().gameplay.revision, x, y}, "deckwalk");
    }

    // The band of event lines (the declared band: the log runs right of `after`'s last column and beside the `row`
    // of buttons; where that leaves it under the pack's minimum it drops under the row, left of `under`). On a short
    // screen the fleet wraps into further columns towards the middle; measured in the design's orientation and
    // mirrored back for «Под левую руку».
    void FollowBand()
    {
        static constexpr float kBandMin = 240.0f, kGap = 8.0f;
        const View &shown = hud_.Shown();
        Band band;
        if (!band_view_.after.empty())
        {
            const bool mirrored = hud_.Mirrored();
            const auto x0 = [mirrored](const Placement &p) {
                const float left = p.x - p.layer_x;
                return (mirrored ? p.layer_width - (left + p.width) : left) / p.density;
            };
            const std::string &after = band_view_.after;
            Placement first, last;
            const bool columns = shown.elements.contains(after) && hud_.Measure(after, ".item:first-child", first) &&
                                 hud_.Measure(after, ".item:last-child", last);
            if (columns && x0(last) > x0(first))
                band.left = static_cast<int>(std::ceil(x0(last) + last.width / last.density)) + 8;
            float row_left = 0.0f, row_bottom = 0.0f, map_left = 0.0f, layer_width = 0.0f;
            for (const std::string &id : band_view_.row)
            {
                Placement button;
                if (!shown.elements.contains(id) || !hud_.Measure(id, "", button))
                    continue;
                const float x = x0(button), bottom = (button.y - button.layer_y + button.height) / button.density;
                row_left = row_left > 0.0f ? std::min(row_left, x) : x;
                row_bottom = std::max(row_bottom, bottom);
                layer_width = button.layer_width / button.density;
            }
            Placement map;
            if (shown.elements.contains(band_view_.under) && hud_.Measure(band_view_.under, "", map))
                map_left = x0(map);
            const float column_right = columns ? x0(last) + last.width / last.density : 0.0f;
            if (row_left > 0.0f && map_left > 0.0f && layer_width > 0.0f)
            {
                const float left = band.left > 0 ? static_cast<float>(band.left) : column_right + kGap;
                if (row_left - kGap - left < kBandMin)
                {
                    band.left = static_cast<int>(std::ceil(column_right + kGap));
                    band.right = static_cast<int>(std::ceil(layer_width - (map_left - kGap)));
                    band.top = static_cast<int>(std::ceil(row_bottom + kGap));
                }
            }
            if (mirrored)
                std::swap(band.left, band.right); // the notice's safeLeft / safeRight are the screen's
        }
        if (band == band_)
            return;
        band_ = band;
        if (shown.elements.contains("log"))
            hud_.Rebuild();
    }

    // --- ScriptCall --------------------------------------------------------------------------

    void Show(const ViewTree &view) override
    {
        if (!building_)
            throw std::runtime_error("a HUD scene shows its view only while it builds");
        view_ = view;
    }
    void On(const std::string &id, const std::string &verb) override
    {
        bound_.insert(id + '\n' + verb);
    }
    void Predict(const ViewTree &view) override
    {
        if (building_)
            throw std::runtime_error("a prediction is made only in a tap");
        Json elements = Json::object();
        if (const ViewTree *tree = Child(view, "elements"))
            for (const ViewTree &element : tree->children)
            {
                const ComponentSpec *spec = hud_.Component(layout_, element.name);
                if (!spec)
                    throw std::runtime_error("the layout scene " + layout_ + " has no element " + element.name);
                ViewTree props = element;
                Strip(props);
                elements[element.name] = screens::ScriptProps(props, *spec);
            }
        Json predicted = {{"elements", std::move(elements)}};
        if (const ViewTree *sheet = Child(view, "sheet"); sheet && !sheet_part_.empty())
            predicted["sheet"] = screens::ScriptProps(*sheet, *hud_.Part(sheet_part_));
        predicted_ = std::move(predicted);
    }
    // A predicted view's readings say nothing the build's do not: the prediction marks taps.
    static void Strip(ViewTree &node)
    {
        std::erase_if(node.children, [](const ViewTree &child) { return Child(child, "reading") != nullptr; });
        for (ViewTree &child : node.children)
            Strip(child);
    }
    void BackTo(const std::string &id) override
    {
        if (!building_)
            throw std::runtime_error("the back key is taken only while the scene builds");
        back_ = id;
    }
    std::string Text(const std::string &key) const override
    {
        return hud_.Text(key);
    }
    void Rebuild() override
    {
        rebuild_ = true;
        hud_.Rebuild();
    }
    bool First() const override
    {
        return first_;
    }
    void Command(const ScriptCommand &command) override
    {
        std::visit([this](const auto &asked) { Run(asked); }, command);
    }
    void Run(const HudControlCommand &command)
    {
        const Gameplay &gameplay = states_->gameplay;
        if (!hud_.Interactive() || !gameplay.Has(command.control))
            return hud_.Log("HUD scene " + screen_.capability + ": " + command.control +
                            " is not bound now; the tap does nothing");
        hud_.Send(GameplayControl{gameplay.revision, command.control});
    }
    void Run(const HudMapCommand &command)
    {
        const WorldMap &map = states_->world_map;
        if (!map.known || !map.available || !map.commands_ready)
            throw std::runtime_error("a map command while the engine offers none");
        // Exactly the command drawn, once: a tap on an element not drawn as it is now, or while one waits, sends
        // nothing.
        const bool drawn = Drawn(); // throws outside a tap
        const char *refused = !hud_.Interactive()     ? "the HUD is not interactive"
                              : !drawn                ? "the element is not drawn as built"
                              : Waiting(map_request_) ? "another map command waits for the game"
                                                      : nullptr;
        if (refused)
            return hud_.Log("HUD scene " + screen_.capability + ": map tap on " + tapped_->id +
                            " sent nothing: " + refused);
        holds_.Release(states_->gameplay);
        map_request_ = hud_.Request(MapCommand{map.revision, command.id});
        requests_.push_back(map_request_);
    }
    void Run(const HudShortcutCommand &command)
    {
        namespace shortcut = app::shortcut;
        for (const char *known : {shortcut::mainmenu, shortcut::character, shortcut::characterscreen,
                                  shortcut::shipscreen, shortcut::journalscreen, shortcut::itemsscreen})
            if (command.name == known)
                return hud_.Send(Shortcut{command.name});
        throw std::runtime_error("'" + command.name + "' is not a game menu of the host");
    }
    void Run(const HudMenuCommand &command)
    {
        if (command.menu != "land" && command.menu != "sea")
            throw std::runtime_error("no menu '" + command.menu + "' of the HUD");
        const auto kind = command.menu == "sea" ? MenuStep::Menu::Sea : MenuStep::Menu::Land;
        if (command.verb == "choose")
        {
            if (!tapped_)
                throw std::runtime_error("a menu row is chosen from a tap");
            chose_ = Choose(kind, command.id);
            return;
        }
        const bool sea = kind == MenuStep::Menu::Sea;
        if (command.verb != "open" && command.verb != "back" && command.verb != "close" &&
            !(sea && command.verb == "recipient"))
            throw std::runtime_error("no " + command.menu + " menu verb '" + command.verb + "'");
        if (command.verb == "open" && (!command.id.empty() || sea))
            Fold(); // the menu opens over the picker it was opened from
        if (sea && command.verb == "recipient")
        {
            if (static_cast<const SeaOrders &>(MenuOf(kind)).recipient != command.id)
                Step(kind, "selectRecipient", command.id);
            return;
        }
        Step(kind, command.verb, command.id);
    }
    // The sails to a position: drawn as asked (the script's prediction) until the engine answers.
    void Run(const HudSailsCommand &command)
    {
        const Ship *ship = CurrentShip(*states_);
        if (!hud_.Interactive() || !ship || !ship->can_set_sails || command.position < 0 || command.position > 2)
            return hud_.Log("HUD scene " + screen_.capability + ": the sails cannot be set now; the tap does nothing");
        requests_.push_back(hud_.Request(hud::Sails{states_->gameplay.revision, command.position}));
    }
    // Exactly the quick action drawn, once per offer: a second tap on it and a tap on an offer the engine has replaced
    // since send nothing.
    void Run(const HudSeaActionCommand &command)
    {
        const SeaQuick &quick = states_->sea_quick;
        const bool drawn = Drawn();
        const bool offered = quick.known && quick.available &&
                             (command.action == quick.action || (command.action == "Map" && quick.map));
        const char *refused = !offered                          ? "the engine does not offer it now"
                              : !hud_.Interactive()             ? "the HUD is not interactive"
                              : !drawn                          ? "the element is not drawn as built"
                              : quick_tapped_ == quick.revision ? "this offer was tapped already"
                                                                : nullptr;
        if (refused)
            return hud_.Log("HUD scene " + screen_.capability + ": the sea action " + command.action +
                            " sent nothing: " + refused);
        quick_tapped_ = quick.revision;
        hud_.Send(SeaQuickTap{quick.revision, command.action});
    }
    void Run(const HudCombatCommand &command)
    {
        Strike(command.action, command.phase);
    }
    void Run(const HudContextCommand &command)
    {
        if (hud_.Interactive())
            hud_.Send(Context{command.name});
    }
    template <class Other> void Run(const Other &)
    {
        throw std::runtime_error("a HUD scene sends the HUD's commands only");
    }
    // The element tapped is on the screen as the last build made it.
    bool Drawn() const
    {
        if (!tapped_)
            throw std::runtime_error("a command the game answers is sent from a tap");
        const auto shown = drawn_.find(tapped_->id), built = elements_.find(tapped_->id);
        return shown != drawn_.end() && built != elements_.end() && *shown == *built;
    }
    bool Waiting(uint64_t request) const
    {
        const auto status = hud_.Answered(request);
        return status == RequestLedger::Status::Waiting || status == RequestLedger::Status::Applied;
    }
    void Pass(const std::string &screen, const ViewTree &note) override
    {
        screens::Router *router = hud_.Router();
        if (!router)
            throw std::runtime_error("no router takes the note for the " + screen + " screen");
        router->Pass(screen, note, Hud::kSubjectSeconds);
    }
    std::optional<ViewTree> Take(const std::string &) override
    {
        throw std::runtime_error("a HUD scene takes no notes");
    }
    std::optional<ViewTree> State(const std::string &channel) const override
    {
        Json state = ScriptState(channel, *states_, hud_);
        read_[channel] = state;
        return screens::StateTree(channel, state);
    }
    std::string Fill(const std::string &text, const std::string &argument) const override
    {
        return screens::Fill(text, {argument});
    }
    std::string OneLine(const std::string &text) const override
    {
        return screens::OneLine(text);
    }
    void Error(const std::string &text) override
    {
        throw std::runtime_error(text);
    }
    // What only a screen of a game's window does.
    [[noreturn]] void Refuse(const char *what) const
    {
        throw std::runtime_error(std::string(what) + " is not offered to HUD scenes");
    }
    void Send(const std::string &, int, const std::string &, const std::string &, const std::string &,
              const std::string &) override
    {
        Refuse("UiSend");
    }
    void Back(const std::string &) override
    {
        Refuse("UiBack");
    }
    void Modal(const std::string &, const std::string &) override
    {
        Refuse("UiModal");
    }
    bool Pending(const std::string &) const override
    {
        Refuse("UiPending");
    }
    bool Offers(const std::string &, const std::string &) const override
    {
        Refuse("UiOffers");
    }
    void Hold() override
    {
        Refuse("UiHold (a HUD tap's mark lasts until its command is answered: UiPredict)");
    }
    std::string Answer() const override
    {
        Refuse("UiAnswer");
    }
    void Await() override
    {
        Refuse("UiAwait");
    }
    void Toast(const std::string &) override
    {
        Refuse("UiToast");
    }
    void Read(const std::string &, const std::string &) override
    {
        Refuse("UiRead");
    }
    void EditText(const std::string &, const std::string &) override
    {
        Refuse("UiEditText");
    }
    int Amount(const std::string &text, const std::string &what) const override
    {
        if (const std::optional<int> value = ParseAmount(text))
            return *value;
        throw std::runtime_error("no number in " + what + " ('" + text + "')");
    }
    bool HasAmount(const std::string &text) const override
    {
        return ParseAmount(text).has_value();
    }
    double Number(const std::string &, const std::string &) const override
    {
        Refuse("UiNumber");
    }
    std::string Decimal(const std::string &) const override
    {
        Refuse("UiDecimal");
    }
    DealView Deal(const DealSpec &) override
    {
        Refuse("UiDeal");
    }

    Hud &hud_;
    const ScriptScreen screen_;
    ScriptPort &port_;
    HoldSet holds_{hud_};
    WalkStick stick_{hud_};
    std::map<std::string, Fan> fans_;
    std::map<std::string, Linger> lingers_;
    bool fights_ = false; // the view has the land's stick or wheel
    bool wheel_open_ = false;
    std::string wheel_move_;
    std::string sheet_part_; // the pack part of the sheet over the scene, "" = none
    Json sheet_ = Json::object();
    // A tapped menu row on its way through select → activate (Choose), in the menu and context it was tapped in.
    std::string choice_, choice_context_;
    MenuStep::Menu choice_menu_ = MenuStep::Menu::Land;
    uint64_t choice_request_ = 0, choice_revision_ = 0;
    bool choice_activated_ = false, chose_ = false;
    const States *states_ = nullptr;
    bool first_ = true, rebuild_ = false, building_ = false;
    Json drawn_ = Json::object();   // the elements as the last frame drew them: what a tap may act on
    const Event *tapped_ = nullptr; // the tap the script runs for
    std::optional<ViewTree> view_;
    mutable std::map<std::string, Json> read_; // the states the last build read: the structure it shows
    // The last build's view: the layout scene, its elements' props, where readings go, the controls and covers.
    std::string layout_;
    bool log_ = false, holds_view_ = false;
    Json elements_ = Json::object();
    std::vector<Slot> slots_;
    std::map<std::string, Control> controls_;
    std::map<std::string, std::string> covers_;
    std::set<std::string> bound_; // "<id>\n<verb>"
    std::string back_;
    // A tap's prediction and the requests it sent; the overlay over every view until they are answered.
    std::optional<Json> predicted_;
    std::vector<uint64_t> requests_;
    std::optional<Overlay> overlay_;
    uint64_t map_request_ = 0;
    std::string label_owner_, label_geometry_;
    uint64_t label_revision_ = 0;
    // the sea: the deck walk, the navigator's disc, the orders' coverage, the quick action tapped, the event band
    std::string deck_; // the deck control's id, "" = none
    struct Walked
    {
        int x = 0, y = 0;
    } walk_; // the deck walk sent: right, forward, -100…100
    bool navigator_prepared_ = false, zoom_in_ = true;
    std::string navigator_owner_, navigator_cleared_;
    std::array<long, 7> navigator_geometry_{};
    uint64_t navigator_layout_ = 0, navigator_visible_ = 0;
    uint64_t orders_covered_ = 0; // the sea orders' revision the sheet was said to cover, 0 = none
    uint64_t quick_tapped_ = 0;
    struct BandView
    {
        std::string after;
        std::vector<std::string> row;
        std::string under;
    } band_view_;
    struct Band
    {
        int left = 0, right = 0, top = 0;
        bool operator==(const Band &) const = default;
    } band_; // dp from the safe area; 0 = the layout's own
};
} // namespace

std::unique_ptr<Scene> CreateScriptScene(Hud &hud, const ScriptScreen &screen, ScriptPort &port)
{
    return std::make_unique<ScriptScene>(hud, screen, port);
}
} // namespace storm::ui::hud
