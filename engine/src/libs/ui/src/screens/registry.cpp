// Which implementation draws which game interface: a ported screen by interface id, the
// generic layout for every other one.
#include "screen.h"

#include "../hud/hud.h"

namespace storm::ui::screens
{

std::unique_ptr<Screen> CreateDialogueScreen();
std::unique_ptr<StateConsumer> CreateDialogueConsumer();
std::unique_ptr<Screen> CreateGameMenuScreen();
std::unique_ptr<Screen> CreateSavesScreen();
std::unique_ptr<Screen> CreateSettingsScreen();
std::unique_ptr<StateConsumer> CreateAppSettingsConsumer();
std::unique_ptr<StateConsumer> CreateRenderCapsConsumer();
std::unique_ptr<Screen> CreateTitleScreen();
std::unique_ptr<Screen> CreateNewGameScreen();
std::unique_ptr<Screen> CreateDiceScreen();
std::unique_ptr<Screen> CreateCardsScreen();
std::unique_ptr<Screen> CreateTransferScreen();
std::unique_ptr<Screen> CreateAlchemyScreen();
std::unique_ptr<Screen> CreateGoodsOrderScreen();
std::unique_ptr<Screen> CreateGameOverScreen();
std::unique_ptr<StateConsumer> CreateGameOverConsumer();
std::unique_ptr<StateConsumer> CreateLoadFailureConsumer();
std::unique_ptr<StateConsumer> CreateSaveFailureConsumer();
std::unique_ptr<StateConsumer> CreateTitleIntentConsumer();
std::unique_ptr<Screen> CreateHeroScreen();
std::unique_ptr<Screen> CreateShipScreen();
std::unique_ptr<Screen> CreateLauncherScreen();
std::unique_ptr<Screen> CreateDownloadsScreen();
std::unique_ptr<Screen> CreateAboutScreen();
std::unique_ptr<Screen> CreateQuickSaveScreen();
std::unique_ptr<StateConsumer> CreateLauncherConsumer();

namespace
{

const ScreenEntry kPorted[] = {
    {"goodstransfer", CreateGoodsOrderScreen},
    {"dialogue", CreateDialogueScreen},
    {"game_menu", CreateGameMenuScreen},
    {"save_load", CreateSavesScreen},
    {"option_screen", CreateSettingsScreen},
    {"mainmenu", CreateTitleScreen},
    {"selectcharacter", CreateNewGameScreen},
    {"boal_dice", CreateDiceScreen},
    {"boal_cards", CreateCardsScreen},
    {"transfer_main", CreateTransferScreen},
    {"alchemy", CreateAlchemyScreen},
    {"game_over", CreateGameOverScreen}, // state channel gameOver, not a game interface
    {"quick_save", CreateQuickSaveScreen},
    {"lscchar_all", CreateHeroScreen},
    {"character", CreateHeroScreen},
    {"ship", CreateShipScreen},
    // The application's own screens, routed from the platform host's "launcher" channel; the
    // "app:" prefix keeps them apart from game interfaces (ids are ini stems, e.g. "about").
    {"app:launcher", CreateLauncherScreen},
    {"app:downloads", CreateDownloadsScreen},
    {"app:about", CreateAboutScreen},
};

// TEHO interfaces with no screen of their own, on purpose (the generic layout draws them if
// they ever open): passengers and selectnation are commented out of BaseInterface.c and have
// no files; disease_alert has no disease_alert.c / .ini in the game, so it never loads;
// ransack_main loads transfer_main.ini (id "transfer_main", ported); pshero opens only from
// the beta-test debug key (Debuger.c, MOD_BETTATESTMODE).

} // namespace

std::span<const ScreenEntry> PortedScreens()
{
    return kPorted;
}

std::vector<std::unique_ptr<StateConsumer>> CreateStateConsumers()
{
    std::vector<std::unique_ptr<StateConsumer>> consumers;
    consumers.push_back(CreateDialogueConsumer());
    consumers.push_back(CreateAppSettingsConsumer());
    consumers.push_back(CreateRenderCapsConsumer());
    consumers.push_back(CreateGameOverConsumer());
    consumers.push_back(CreateLoadFailureConsumer());
    consumers.push_back(CreateSaveFailureConsumer());
    consumers.push_back(CreateTitleIntentConsumer());
    consumers.push_back(CreateLauncherConsumer());
    // gameplay, combat, vitals, worldInfo, contextAction, reloadPlace, landCommands, ship, seaOrders, seaQuickAction,
    // presentation, spyglass
    hud::AddStateConsumers(consumers);
    return consumers;
}

} // namespace storm::ui::screens
