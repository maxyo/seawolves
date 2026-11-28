// The dialogue's state channel: the game's dialogue (Host::SetDialogue, storm_ui/dialogue_state.h; the "dialogue"
// state channel in fixtures) becomes the interface model "dialogue" the dialogue's script screen draws
// (app/mobile-ui touch/screens/dialogue.c). The model: SPEAKER, TEXT, the REPLY rows (index: the reply's link in the
// node, offering "reply", or "edit" for the one the player types), INPUT (that reply as an edit field, its text the
// proposal) and EDIT_ERROR (the engine's refusal of a typed answer: too_long, invalid_text, unavailable). The opening
// is the dialogue's generation: a reply goes back as a widget action of it, answered by the engine (libs/dialog
// takeDialogueChoice).
#include "format.h"

#include "../hud/hud.h"
#include "storm_ui/dialogue_state.h"

namespace storm::ui::screens
{

namespace
{

bool KnownRefusal(const std::string &code)
{
    return code == "too_long" || code == "invalid_text" || code == "unavailable";
}

// The game's texts may carry escaped line breaks ("\n" as two characters).
std::string GameText(std::string text)
{
    for (size_t at = 0; (at = text.find('\\', at)) != std::string::npos;)
    {
        if (at + 1 < text.size() && text[at + 1] == 'n')
            text.replace(at, 2, "\n");
        else if (at + 1 < text.size() && text[at + 1] == 'r')
            text.erase(at, 2);
        else
            ++at;
    }
    return text;
}

// The fixture format of channel "dialogue" as the struct the engine hands over.
Dialogue ReadDialogue(const Json &state, Router &router)
{
    Dialogue dialogue;
    if (!state.is_object() || !state.value("active", false))
        return dialogue;
    const auto text = [](const Json &object, const char *key) {
        const auto found = object.find(key);
        return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
    };
    dialogue.active = true;
    dialogue.generation = state.value("generation", uint64_t{0});
    dialogue.language = text(state, "language");
    dialogue.speaker = text(state, "speaker");
    dialogue.text = text(state, "text");
    const auto choices = state.find("choices");
    if (choices != state.end() && choices->is_array())
        for (const Json &choice : *choices)
        {
            if (!choice.is_object() || !choice.contains("index") || !choice["index"].is_number_integer())
            {
                router.ReportError("dialogue: a choice without an index: " + choice.dump());
                continue;
            }
            Dialogue::Choice reply;
            reply.index = choice["index"].get<int>();
            reply.text = text(choice, "text");
            reply.editable = choice.value("editable", false);
            dialogue.choices.push_back(std::move(reply));
        }
    const auto refused = state.find("editError");
    if (refused != state.end() && refused->is_string())
        dialogue.refusal = Dialogue::Refusal{state.value("editIndex", -1), refused->get<std::string>()};
    return dialogue;
}

class DialogueScreenConsumer final : public DialogueConsumer
{
  public:
    const char *Channel() const override
    {
        return "dialogue";
    }
    void OnState(const Json &state, Router &router) override
    {
        Show(ReadDialogue(state, router), router);
    }
    void Show(const Dialogue &dialogue, Router &router) override
    {
        if (!dialogue.active)
        {
            shown_.reset();
            if (router.Using() == "dialogue")
                router.CloseInterface();
            return;
        }
        shown_ = dialogue;
        shown_->refusal.reset(); // a refusal answers one submission: it is not shown again
        InterfaceModel model;
        model.screen = "dialogue";
        model.opening = dialogue.generation;
        model.revision = ++revision_;
        // The game's language as the engine sends it with the dialogue (the gameplay state
        // reports none while a dialogue holds the game).
        model.language = GameText(dialogue.language);
        if (model.language.empty() && router.Hud())
            model.language = router.Hud()->Current().gameplay.language;
        Widget speaker;
        speaker.node = "SPEAKER";
        speaker.type = "heading";
        speaker.label = GameText(dialogue.speaker);
        model.widgets.push_back(speaker);
        Widget body;
        body.node = "TEXT";
        body.type = "text";
        body.label = GameText(dialogue.text);
        model.widgets.push_back(body);
        for (const Dialogue::Choice &choice : dialogue.choices)
        {
            Widget reply;
            reply.node = "REPLY";
            reply.type = "choice";
            reply.index = choice.index;
            reply.label = GameText(choice.text);
            reply.row_id = std::to_string(reply.index);
            reply.actions = {choice.editable ? "edit" : "reply"};
            if (choice.editable)
            {
                Widget input; // the field the typed answer goes to; it proposes the reply's text
                input.node = "INPUT";
                input.type = "edit";
                input.index = reply.index;
                input.value = reply.label;
                input.actions = {"edit"};
                model.widgets.push_back(std::move(input));
            }
            model.widgets.push_back(std::move(reply));
        }
        if (dialogue.refusal)
        {
            Widget error;
            error.node = "EDIT_ERROR";
            error.type = "text";
            error.index = dialogue.refusal->index;
            error.label = dialogue.refusal->error;
            error.context["revision"] = std::to_string(model.revision); // every refusal counts, even a repeated one
            if (!KnownRefusal(error.label))
                router.ReportError("dialogue: unknown answer refusal '" + error.label + "'");
            model.widgets.push_back(std::move(error));
        }
        for (size_t i = 0; i < model.widgets.size(); ++i)
            model.widgets[i].order = static_cast<int>(i);
        router.SetInterface(model);
    }

    // The engine sends a dialogue node once: a game interface the dialogue's script opened over
    // it (or one still closing as the dialogue began) must not take it away for good.
    void Resume(Router &router) override
    {
        if (shown_)
            Show(Dialogue(*shown_), router); // a copy: Show stores what it shows
    }

  private:
    uint64_t revision_ = 0;
    std::optional<Dialogue> shown_; // the active dialogue last shown, without its refusal
};

} // namespace

std::unique_ptr<StateConsumer> CreateDialogueConsumer()
{
    return std::make_unique<DialogueScreenConsumer>();
}

} // namespace storm::ui::screens
