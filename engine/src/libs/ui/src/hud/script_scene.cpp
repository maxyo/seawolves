// A HUD scene written in script (interface/touch/hud/ of the pack's scripts). The script builds what the scene shows
// from the states it reads (UiState) and answers the taps it binds; the runtime keeps what runs every frame or every
// touch move: the readings it lays into the view, the continuous controls, the geometry the engine is told, and the
// tapped choice drawn until the game answers it.
//
// The script's view: {layout, log (1: the event lines), holds (1: the scene keeps the HUD through a blocked game),
// elements.<id>.<props> (by the element's pack component; a prop may be a reading {reading, as, fill}),
// controls.<id>.{kind: hold {control} | helm {left, right} | lever {up, down}}, covers.<id> = "map-labels"}.
#include "hud.h"
#include "app_ui_names.hpp"
#include "../component_spec.h"
#include "../screens/amount.h"
#include "../screens/controls.h"
#include "../screens/script_props.h"

#include <cmath>
#include <set>
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

class ScriptScene final : public Scene, private ScriptCall
{
  public:
    ScriptScene(Hud &hud, const ScriptScreen &screen, ScriptPort &port) : hud_(hud), screen_(screen), port_(port)
    {
    }

    void Build(const States &states, View &view, Input &input) override
    {
        states_ = &states;
        if (first_ || rebuild_ || !SameStructure(states))
            Run();
        Compose(states, view, input);
    }

    double Tick(const States &states, double) override
    {
        holds_.Forget(states);
        if (overlay_ && Ended(*overlay_))
            hud_.Rebuild(); // the answer came without a state change: the mark goes
        return kIdle;
    }

    void Drawn(const States &states) override
    {
        drawn_ = elements_;
        Cover(&states);
    }
    void Hidden(const States &) override
    {
        Cover(nullptr);
    }
    void Released(const States &states) override
    {
        holds_.Release(states.gameplay);
    }
    void Suspended(const States &) override
    {
        Cover(nullptr);
        overlay_.reset();
        first_ = true; // the player comes back to a scene whose own state starts afresh
    }
    bool Holds(const States &) const override
    {
        return holds_view_;
    }
    bool Back(const States &states) override
    {
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
        std::string kind, a, b; // hold {control}, helm {left, right}, lever {up, down}
    };
    struct Overlay
    {
        Json patch;
        std::vector<uint64_t> requests;
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
        if (const ViewTree *elements = Child(view, "elements"))
            for (const ViewTree &element : elements->children)
                elements_[element.name] = Props(element);
        if (const ViewTree *controls = Child(view, "controls"))
            for (const ViewTree &control : controls->children)
            {
                const std::string kind = Value(control, "kind");
                if (kind == "hold")
                    controls_[control.name] = {kind, Value(control, "control"), {}};
                else if (kind == "helm")
                    controls_[control.name] = {kind, Value(control, "left"), Value(control, "right")};
                else if (kind == "lever")
                    controls_[control.name] = {kind, Value(control, "up"), Value(control, "down")};
                else
                    throw std::runtime_error("control " + control.name + ": no kind '" + kind + "'");
                if (!elements_.contains(control.name))
                    elements_[control.name] = Json::object();
            }
        if (const ViewTree *covers = Child(view, "covers"))
            for (const ViewTree &cover : covers->children)
            {
                if (cover.value != "map-labels")
                    throw std::runtime_error("cover " + cover.name + ": no kind '" + cover.value + "'");
                covers_[cover.name] = cover.value;
            }
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
            target = target.is_number() && value.is_string() ? Json::parse(value.get<std::string>()) : value;
        }
        if (overlay_ && Ended(*overlay_))
            overlay_.reset();
        if (overlay_)
            Apply(view.elements, overlay_->patch);
        if (log_)
            if (Json lines = hud_.LogLines(); !lines.empty())
                view.elements["log"] = Json{{"lines", std::move(lines)}};
        view.back = !back_.empty();
        for (const std::string &bound : bound_)
        {
            const size_t split = bound.find('\n');
            input.On(bound.substr(0, split), bound.substr(split + 1),
                     [this](const Event &event) { Tap(event, *states_); });
        }
        for (const auto &[id, control] : controls_)
            BindControl(id, control, input);
    }

    void BindControl(const std::string &id, const Control &control, Input &input)
    {
        if (control.kind == "hold")
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

    bool Ended(const Overlay &overlay) const
    {
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
        if (predicted_ && !requests_.empty())
        {
            Json patch = Patch(elements_, *predicted_);
            if (!patch.is_null())
                overlay_ = Overlay{std::move(patch), requests_};
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
        predicted_ = std::move(elements);
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
        if (!hud_.Interactive() || !Drawn() || Waiting(map_request_))
            return;
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
        if (const std::optional<int> value = screens::ParseAmount(text))
            return *value;
        throw std::runtime_error("no number in " + what + " ('" + text + "')");
    }
    bool HasAmount(const std::string &text) const override
    {
        return screens::ParseAmount(text).has_value();
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
};
} // namespace

std::unique_ptr<Scene> CreateScriptScene(Hud &hud, const ScriptScreen &screen, ScriptPort &port)
{
    return std::make_unique<ScriptScene>(hud, screen, port);
}
} // namespace storm::ui::hud
