// The menus' state channels: the engine's death picture as the game-over interface the menus' script draws
// ("gameOver"), and the host's «Продолжить» at the title ("titleIntent").
#include "controls.h"

namespace storm::ui::screens
{

namespace
{

// Channel "titleIntent" {intent: "Continue"}: the platform host asks the title screen for «Продолжить»
// as if tapped, once: at once when the title is up, else when it next builds (the app's Game Loop test intent sets it
// at the game's start, android/game/testloop). Anything else is an error.
class TitleIntentConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "titleIntent";
    }
    void OnState(const Json &state, Router &router) override
    {
        if (state.is_object() && state.contains("intent") && state["intent"] == "Continue")
        {
            router.Shared().title_intent = "Continue";
            // Asked while the title is up (after the automatic preset measured it): act now, not at its next build.
            if (router.Using() == "mainmenu")
                router.RebuildActive();
        }
        else
            router.ReportError("state channel titleIntent: expected {intent: Continue}, got " + state.dump());
    }
};

// Channel "gameOver" (xinterface info_handler.cpp): {kind: sea|land|defeat, language} while the game shows its death
// picture, null after. The model made of it (KIND text, END button: the game's own end of the picture) is the
// interface game_over.
class GameOverConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "gameOver";
    }
    void OnState(const Json &state, Router &router) override
    {
        const std::string kind = state.is_object() && state.contains("kind") && state["kind"].is_string()
                                     ? state["kind"].get<std::string>()
                                     : "";
        if (state.is_null() || (kind != "sea" && kind != "land" && kind != "defeat"))
        {
            if (!state.is_null())
                router.ReportError("state channel gameOver: expected {kind: sea|land|defeat, language} or null, got " +
                                   state.dump());
            if (router.Using() == "game_over")
                router.CloseInterface();
            return;
        }
        InterfaceModel model;
        model.screen = "game_over";
        model.language = state.contains("language") && state["language"].is_string()
                             ? state["language"].get<std::string>()
                             : std::string();
        model.opening = ++opening_;
        model.revision = opening_;
        Widget text;
        text.node = "KIND";
        text.type = "text";
        text.label = kind;
        model.widgets.push_back(text);
        Widget end; // ends the death picture now (engine: the game's own end of it)
        end.node = "END";
        end.type = "button";
        end.actions = {"press"};
        model.widgets.push_back(end);
        router.SetInterface(model);
    }

  private:
    uint64_t opening_ = 0;
};

} // namespace

std::unique_ptr<StateConsumer> CreateTitleIntentConsumer()
{
    return std::make_unique<TitleIntentConsumer>();
}

std::unique_ptr<StateConsumer> CreateGameOverConsumer()
{
    return std::make_unique<GameOverConsumer>();
}

} // namespace storm::ui::screens
