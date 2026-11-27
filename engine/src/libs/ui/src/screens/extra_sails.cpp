// Sail emblem at the shipyard (interface "sailsgerald"). The engine exports
// the emblems of the scroll image (xinterface mobile_sails_gerald.hpp: SCROLL_GERALD rows with the
// texture path, the current one marked) and the marker ANDROID_SAILS_GERALD (price, the hero's
// money, the emblem on the sails now). Fills <s-sail-emblem>: a tap chooses an emblem (the game's
// own selection), one contextual primary paints the chosen emblem (B_OK) or takes the one on the
// sails off (B_REMOVE), whichever of the two the game offers for it. ← is the game's exit.
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"SCROLL_GERALD", "choice", true},
    {"ANDROID_SAILS_GERALD", "sails_gerald", true},
    {"EXIT_BTN", "button"},
    {"B_OK", "button"},
    {"B_REMOVE", "button"},
    {"B_CANCEL", "button"},
    {"INFO_CAPTION", "text"},
    // The game's ‹ › move the scroll image; the screen selects the neighbour itself.
    {"SAILS_GERALD_LEFT_BUTTON", "button"},
    {"SAILS_GERALD_RIGHT_BUTTON", "button"},
};

std::string Context(const Widget &widget, const char *key)
{
    const auto found = widget.context.find(key);
    return found == widget.context.end() ? std::string() : found->second;
}

// 1250 → "1 250".
std::string Spaced(const std::string &digits)
{
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i && (digits.size() - i) % 3 == 0)
            out += ' ';
        out += digits[i];
    }
    return out;
}

class SailEmblemScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-sail-emblem";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const std::vector<const Widget *> emblems = admitted.All("SCROLL_GERALD");
        const Widget &marker = *admitted.One("ANDROID_SAILS_GERALD");
        if (!marker.unavailable_reason.empty())
            bindings.ErrorOnce(marker.unavailable_reason, "sail emblem: " + marker.unavailable_reason);
        const std::string applied = Context(marker, "applied");
        const std::string price = Context(marker, "price"), money = Context(marker, "money");
        size_t current = 0;
        for (size_t i = 0; i < emblems.size(); ++i)
            if (emblems[i]->value == "1")
                current = i;

        Json items = Json::array();
        for (const Widget *emblem : emblems)
        {
            items.push_back(Json{{"texture", Context(*emblem, "texture")},
                                 {"label", emblem->label},
                                 {"selected", emblem->value == "1"},
                                 {"applied", !applied.empty() && emblem->row_id == applied},
                                 {"disabled", !emblem->Offers("select")}});
        }
        const auto choose = [&bindings, emblems](size_t index) {
            if (index < emblems.size() && emblems[index]->Offers("select") && emblems[index]->value != "1")
                bindings.Send(*emblems[index], "select", emblems[index]->row_id);
        };
        bindings.On("emblems", "emblem", [&bindings, choose](const Event &event) {
            size_t index = 0;
            if (EventNumber(bindings, event, index))
                choose(index);
        });
        bindings.On("emblem-prev", "", [choose, current](const Event &) { choose(current - 1); });
        bindings.On("emblem-next", "", [choose, current](const Event &) { choose(current + 1); });

        Json view = Json::object();
        const std::string caption = admitted.Label("INFO_CAPTION");
        view["title"] = caption.empty() ? bindings.Text("sails.title") : caption;
        view["emblems"] = items;
        view["appliedLabel"] = bindings.Text("sails.applied");
        view["prev"] = current > 0;
        view["next"] = current + 1 < emblems.size();
        view["stats"] = money.empty()
                            ? Json::array()
                            : Json::array({Json{{"icon", "money"}, {"value", Spaced(money)}, {"tone", "gold"}}});

        // One primary: the game makes one of its two buttons selectable for the chosen emblem (SailsGerald.c
        // CheckChangeSailStatus); it charges the price without asking for the money.
        const Widget *paint = admitted.One("B_OK");
        const Widget *remove = admitted.One("B_REMOVE");
        const bool removing = remove && remove->Offers("press") && !(paint && paint->Offers("press"));
        const Widget *button = removing ? remove : paint;
        view["primary"] = Json();
        if (button)
        {
            view["primary"] = Json{{"label", bindings.Text(removing ? "sails.remove" : "sails.apply")},
                                   {"sub", price.empty() ? std::string() : Spaced(price)},
                                   {"disabled", !button->Offers("press")},
                                   {"reason", button->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.Forward("primary", *button, "press");
        }

        const Widget *exit = admitted.One("EXIT_BTN") ? admitted.One("EXIT_BTN") : admitted.One("B_CANCEL");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
};

} // namespace

std::unique_ptr<Screen> CreateSailEmblemScreen()
{
    return std::make_unique<SailEmblemScreen>();
}

} // namespace storm::ui::screens
