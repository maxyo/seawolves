# Changelog / Список изменений

Each release is built from its tag `v<version>`; its section below is the text of the release notes.
Каждый выпуск собирается из своего тега `v<версия>`; его раздел ниже — это текст описания выпуска.

## 1.0.0-alpha.1 — unreleased

**Русский**

Первая публичная альфа Seawolves — неофициального фанатского порта «Корсаров: Каждому своё» на Android. Игра идёт
на нашем Android-форке открытого движка Storm Engine. **Файлов игры в приложении нет:** нужна своя легально
полученная копия версии 1.7.3, приложение копирует её с телефона.

- **Импорт игры.** Папка с установленной игрой, архив ZIP, 7z или RAR либо установщик `setup.exe` со всеми
  `setup-*.bin` — через системный выбор файлов. Экран «Скачать» загружает копию, на которую у вас есть право, по
  вашему torrent-файлу или magnet-ссылке; ссылок на игру в приложении нет. Копию другой версии игры импорт помечает
  как непроверенную: выбрать её можно, но исправления скриптов к ней не применяются.
- **Сенсорное управление.** Плавающий стик, обзор свайпом, кнопки действий на суше; в море — штурвал, который
  поднимается к пальцу по высокой полосе у левого края и сразу поворачивает в ту сторону, где его коснулись,
  рычаг парусов на своём месте, боезапас, приказы эскадре и подзорная труба; на глобальной карте — те же штурвал и рычаг парусов. Способности героя и корабля — ряд
  кнопок на экране. Нажатия в интерфейсе отзываются сразу, в том же кадре. Вибрация при попаданиях и абордаже, раскладка
  «Под левую руку».
- **Новый интерфейс.** Переработанные экраны меню, сохранений, настроек, героя, корабля, торговли, верфи,
  журнала и диалогов на шести языках (русский, английский, немецкий, испанский, французский, итальянский).
  На экране героя описание навыка или характеристики показывает, из чего сложилось её значение: базовое
  значение, штрафы и бонусы по шагам.
- **Настройки.** Одни и те же настройки на титульном экране и в игре: изменённое в одном месте действует везде.
- **Графика.** Vulkan по умолчанию и OpenGL ES там, где драйвер Vulkan не подходит; пресеты качества, в том
  числе автоматический; карты рельефа, нормалей и глянца и ×2 увеличение текстур, которые создаются на телефоне
  из ваших файлов игры; параллакс, переработанные свет и тени, мокрые поверхности в дождь, дальние LOD островов,
  кораблей и локаций. Частота кадров и разрешение сцены всегда такие, как выбраны в настройках: игра не снижает их
  сама ни при нехватке скорости, ни при нагреве. Когда телефон греется, фоновая подготовка текстур сначала
  замедляется, а если нагрев не спадает — останавливается, пока телефон не остынет.
- **Видео.** Вступительные и сюжетные ролики игры воспроизводятся.
- **Сохранения.** Автосохранения в трёх чередующихся записях — в том числе перед абордажем и при уходе
  приложения в фон. На экране «О программе»: «Отправить сохранение» (одно или «Все сохранения» сразу) — для
  резервной копии или сообщения об ошибке, и «Восстановить сохранение» — после переустановки или на новом
  телефоне.
- **Исправления скриптов игры** в виде небольших патчей, которые применяются при запуске к вашей копии 1.7.3;
  полный список — «О программе» → «Лицензии» → «Правки скриптов игры».
- **Пираты на глобальной карте встречаются реже, чем в исходной игре.** По умолчанию случайные встречи с
  пиратами происходят с частотой 20 % от игровой. Это настройка баланса, а не исправление: «Настройки» →
  «Частота пиратов на карте» («Редко» — 20 %, «Иногда» — 50 %, «Часто» — как в исходной игре).
- **Сообщения об ошибках.** «О программе» → «Поделиться отчётом»: файл с логами и сведениями об устройстве,
  который прикладывается к сообщению об ошибке.

Ограничения альфы: исправления скриптов рассчитаны на версию игры 1.7.3 (`ver. 1.7.3 (26.11.21) x64`), на другой
версии они не применяются, и другие версии не проверялись; полное прохождение и все экраны ещё не проверены на
разных телефонах; производительность и совместимость зависят от GPU и драйвера.

Минимальные требования: Android 7.0+, 64-битный ARM (arm64-v8a), 4 ГБ оперативной памяти, 15 ГБ свободного места,
GPU с Vulkan 1.1 или OpenGL ES 3.2 среднего класса 2021 года и новее (Adreno 642L, Mali-G68 MC4, Xclipse 530 и
выше). Рекомендуемые: Android 12+, 8 ГБ, 20 ГБ, флагманский GPU 2023 года и новее (Adreno 740, Immortalis-G715,
Xclipse 940 и выше). Телефоны с GPU уровня PowerVR GE8320 (Galaxy A12) не поддерживаются. Подробности и что из
этого проверено — в README.

**English**

The first public alpha of Seawolves, an unofficial fan port of Sea Dogs: To Each His Own («Корсары: Каждому
своё») to Android. The game runs on our Android fork of the open-source Storm Engine. **No game files are
included:** you need your own legally obtained copy of version 1.7.3, which the app copies from your phone.

- **Game import.** An installed game folder, a ZIP, 7z or RAR archive, or the installer `setup.exe` with all its
  `setup-*.bin` files, through the system file picker. The Download screen fetches a copy you are entitled to
  from your own torrent file or magnet link; the app contains no links to the game. The import marks a copy of another game
  version as untested: you can still choose it, but the script fixes are not applied to it.
- **Touch controls.** A floating movement stick, camera swipes and action buttons on land; at sea a ship's wheel
  helm that rises to the thumb anywhere in a tall band along the left edge and turns at once to the side it is
  touched on, a sail lever that stays in its place, ammunition, fleet orders and the spyglass; the world map has the same helm and sail lever. The hero's and the
  ship's abilities are a row of buttons on the screen. Taps in the interface answer at once, in the same frame. Haptic feedback for hits
  and boarding, a left-handed layout.
- **A new interface.** Redesigned menus, saves, settings, hero, ship, trade, shipyard, journal and dialogue
  screens in six languages (Russian, English, German, Spanish, French, Italian). On the hero screen the
  description of a skill or characteristic shows what its value is made of: the base value, then each penalty
  and bonus.
- **Settings.** The title screen and the game share one set of settings: a change made in one place applies
  everywhere.
- **Graphics.** Vulkan by default and OpenGL ES where the Vulkan driver is not fit for it; quality presets,
  including an automatic one; relief, normal and gloss maps and a ×2 texture upscale generated on the phone from
  your own game files; parallax, reworked lighting and shadows, wet surfaces in the rain, far LODs for islands,
  ships and locations. The frame rate and the scene resolution are always the ones chosen in the settings: the game
  never lowers them by itself, neither when frames run late nor when the phone heats up. When the phone warms up, the background
  texture preparation first slows down and, if the heat goes on, stops until the phone has cooled.
- **Video.** The game's intro and story videos play.
- **Saves.** Autosaves in three rotating records, including one before a boarding and one when the app goes to
  the background. On the About screen: Send a save (one, or All saves at once) for a backup or a bug report, and
  Restore a save after reinstalling the app or on a new phone.
- **Fixes to the game's own scripts** as small patches applied at start-up to your copy of 1.7.3; the full list
  is under About → Licenses → Game script patches.
- **Pirates on the world map are rarer than in the original game.** By default random pirate encounters happen
  at 20 % of the game's frequency. This is a balance setting, not a fix: Settings → Pirates on the world map
  (Rare is 20 %, Sometimes 50 %, Often is the original game).
- **Bug reports.** About → Share a report: a file with the logs and device details to attach to a bug report.

Alpha limitations: the script fixes are made for game version 1.7.3 (`ver. 1.7.3 (26.11.21) x64`) and are not
applied to any other, and other versions were not tested; a full playthrough and every screen have not been
checked on many phones yet; performance and compatibility depend on the GPU and its driver.

Minimum requirements: Android 7.0+, 64-bit ARM (arm64-v8a), 4 GB of RAM, 15 GB of free storage, a GPU with Vulkan 1.1
or OpenGL ES 3.2 of the 2021 mid-range or newer (Adreno 642L, Mali-G68 MC4, Xclipse 530 and up). Recommended:
Android 12+, 8 GB, 20 GB, a flagship GPU of 2023 or newer (Adreno 740, Immortalis-G715, Xclipse 940 and up). Phones
with a GPU of the PowerVR GE8320 class (Galaxy A12) are not supported. The details, and which of it was tested, are
in the README.
