// The menus' state channels: the engine's death picture as the game-over interface the menus' script draws
// ("gameOver"), the host's «Продолжить» at the title ("titleIntent"), and the loads and saves that failed
// ("loadFailure", "saveFailure").
#include "format.h"

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
            router.Pass("title", ViewTree{"", "", {{"intent", "Continue", {}}}});
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
        model.engine = true;
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

// The records the save failure names as an autosave: the rotation's (xinterface save_autosave.hpp names their files)
// and the one a game writes itself before a boarding ("<profile> AutoSave", interface.c AutoSaveContinue).
bool IsAutosave(const std::string &label)
{
    static const std::string prefix = "Автосохранение ", own = " AutoSave";
    if (label.size() > own.size() && label.compare(label.size() - own.size(), own.size(), own) == 0)
        return true;
    return label.size() > prefix.size() && label.compare(0, prefix.size(), prefix) == 0 &&
           label.find_first_not_of("0123456789", prefix.size()) == std::string::npos;
}

// Channel "loadFailure" {record, reason}: a load the player asked for was refused or never
// started (the engine's load watchdog, core load_watch.hpp). The engine has logged the reason
// and its scripts bring the menu back; the player is told which record did not load.
class LoadFailureConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "loadFailure";
    }
    void OnState(const Json &state, Router &router) override
    {
        if (state.is_null())
            return;
        if (!state.is_object() || !state.contains("record") || !state["record"].is_string() ||
            !state.contains("reason") || !state["reason"].is_string())
        {
            router.ReportError("state channel loadFailure: expected {record, reason}, got " + state.dump());
            return;
        }
        // "SAVE\<profile>\<file>": the player knows the record by its file name.
        const std::string record = state["record"].get<std::string>();
        const size_t slash = record.find_last_of("\\/");
        router.Alert(StringKey("save.load_failed"), slash == std::string::npos ? record : record.substr(slash + 1));
    }
};

// Channel "saveFailure" {record, reason, space}: a save could not be written (core
// CoreImpl::ReportFailedSave, one frame after it: a background autosave is told back in the
// foreground). The previous record is unchanged. The scripts bring the save screen back after a
// save of the player; the player is told here, the full storage said as such.
class SaveFailureConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "saveFailure";
    }
    void OnState(const Json &state, Router &router) override
    {
        if (state.is_null())
            return;
        if (!state.is_object() || !state.contains("record") || !state["record"].is_string() ||
            !state.contains("reason") || !state["reason"].is_string() || !state.contains("space") ||
            !state["space"].is_boolean())
        {
            router.ReportError("state channel saveFailure: expected {record, reason, space}, got " + state.dump());
            return;
        }
        const std::string record = state["record"].get<std::string>();
        const size_t slash = record.find_last_of("\\/");
        const std::string file = slash == std::string::npos ? record : record.substr(slash + 1);
        const bool space = state["space"].get<bool>();
        if (IsAutosave(file))
            router.Alert(space ? StringKey("save.autosave_failed_space") : StringKey("save.autosave_failed"), "");
        else
            router.Alert(space ? StringKey("save.save_failed_space") : StringKey("save.save_failed"),
                         space ? "" : file);
    }
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

std::unique_ptr<StateConsumer> CreateLoadFailureConsumer()
{
    return std::make_unique<LoadFailureConsumer>();
}

std::unique_ptr<StateConsumer> CreateSaveFailureConsumer()
{
    return std::make_unique<SaveFailureConsumer>();
}

} // namespace storm::ui::screens
