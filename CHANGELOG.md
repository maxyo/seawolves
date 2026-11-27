# Changelog / Список изменений

## 1.0.0-alpha.2 — unreleased

**Русский**

- Выстрел с палубы по вражескому кораблю больше не считается нападением на корабль с неизвестными отношениями, а
  выстрел по нейтральному — выстрелом по врагу.
- Блок больше не спасает от удара в спину: удар сзади (дальше 150° от взгляда защищающегося) пробивает блок.
- Знак квеста над персонажем скрывается, пока тот дерётся, мёртв или уходит из локации.
- Небо одно для всех игр: вместо нарисованных небесных текстур — небо, рассчитанное так же, как в Caribbean Legend,
  со сменой времени суток, тёплым закатом, дымкой у горизонта, солнцем, луной, звёздами и облаками, которые плывут по
  ветру. Облачность каждого часа взята из погоды игры; облака освещены так же, как облака Caribbean Legend (тёплые на
  закате, лунные ночью); в дождь и шторм небо затягивают тёмные тучи, и горизонт с морем темнеют вместе с ними.
  Звёзды строятся заново (каталог звёзд игры и планеты больше не рисуются), солнце и луна — диски неба с картинками
  самой игры (луна — в фазе по дате).
- Caribbean Legend: облака неба — три слоя облачных текстур самой игры (дождевые тучи, основные облака, перистые),
  освещённые и медленно вращающиеся, как в её движке; экспозиция и цветокоррекция камеры берутся из её периодов суток,
  а заполняющий свет сцены — от нарисованного неба без облаков, поэтому ночь больше не почти чёрная.
- Caribbean Legend: в дождливый час свет солнца и луны на сцене, на облаках и на дисках берётся из дождевого
  пресета игры, как в её движке, — днём под дождём пасмурно: свет тусклее и ровнее, тени слабые.
- В «Каждому своё» тени от солнца теперь падают по направлению света самого часа, как его задаёт погода игры.
- Под дождевыми тучами солнце не даёт ореола, а в «Каждому своё» и не отбрасывает теней.
- Молния освещает небо и сцену вспышкой, как в Caribbean Legend: ночью облака и всё вокруг на миг высвечиваются, днём
  небо и рассеянный свет на миг приглушаются. В «Каждому своё» вспышку даёт каждая молния игры.
- Caribbean Legend: пока час дождливый и скрипты показывают дождь, вокруг камеры идёт дождь — эффект самой игры.
- Скрипты: тело цикла или условия без фигурных скобок — ровно один оператор, как в C; следующий оператор больше не
  попадает внутрь. В «Каждому своё» это меняет одно место: дата предыдущего платежа в окне долгов корабля
  обновляется при каждой оплате.
- Экран склада показывал неверный вес товара и сделки: теперь вес пачки и вес перевозимого товара считаются верно.
- ПК-сборка: графика больше не начинается с телефонного пресета «Низкая». Каждая возможность движка — своя
  настройка (сглаживание, тени солнца и ламп, блум, море, рельеф и другие), по умолчанию полное качество, сцена в
  размер окна, а создание карт текстур, как на телефоне, выключено; настройки хранятся отдельно для каждой игры. В «Каждому своё» они на экране настроек игры: флажок
  «Графика движка» рядом с «Всегда бежать» показывает их списком, двойной щелчок или Enter меняет значение.
- Верфь: «Все» в окне ремонта чинила сначала корпус, а сама игра чинит сначала паруса, поэтому при нехватке денег
  ремонт выходил другим; ползунки ремонта останавливались не там, где игра, а цена ремонта и доплата при обмене
  корабля считались экраном. Теперь «Все» — кнопка игры, ползунки встают там, где их остановила игра, цена и доплата
  — числа игры.
- Герб на парусах: экран не давал нанести или снять герб, если денег меньше цены, хотя игра это позволяет.
- Доли команды: на кнопке «Заплатить» стояла сумма, посчитанная экраном, и при одном лишь долге перед государством
  она не совпадала с тем, что платит игра; причина недоступной кнопки тоже была догадкой экрана. Теперь кнопка — кнопка
  игры, без своей суммы.
- Ремонт своими силами: число досок и парусины на кнопке «Починить» было прикидкой экрана, а предел ползунков
  вычитывался из русских подписей окна. Теперь материалы — те, что считает игра, а ползунок встаёт там, где игра
  остановила починку.
- Найм команды: сколько можно нанять и во что это обойдётся, экран считал сам и показывал деньги «после найма» по
  своему расчёту. Теперь число отправляется игре сразу, а экран показывает, сколько людей она поставила и какую
  стоимость назвала.
- Способности: «Изучить» доступна для любой неизученной способности, решает игра. Если игра отказывает, кнопка
  гаснет, а недостающее (нужные способности или очки) подсвечивается.
- Предметы: причина недоступной кнопки больше не придумывается (раньше называлась только одна из причин игры);
  ячеек для амулетов столько, сколько их у персонажа в игре.
- Нации: флаги больше не гасятся интерфейсом заранее. Флаг, который игра не даёт поднять, гаснет после
  нажатия, а выбранный флаг остаётся прежним. Оттенок репутации берётся из слова самой игры. Подсказка игры к
  репутации и флагу открывается листом для чтения.
- Карты: очки считаются так, как их считает игра (колода мода с другими очками показывает верную сумму); имя
  соперника полное, как в игре.
- Компас карты мира больше не предсказывает дождь: игра не строит погоду моря, пока корабль на карте, и прогноз мог не
  сбыться. Компас показывает только зону шторма или смерча, в которой стоит корабль.
- Карта мира: команды карты видны и тогда, когда игра запрещает выход с карты, — на нажатие отвечает сама игра своим
  сообщением; команда мода или другой игры показывается с простой иконкой, а не убирает все кнопки. Число дней
  провизии на компасе больше не краснеет по своему порогу: об остатке предупреждает сама игра.
- Меню действий на суше: команда зелий доступна тогда же, когда в меню самой игры; одно противоядие при полном
  здоровье больше не открывает список зелий.
- Абордаж: кнопка «Дальше» показывается всё время, пока игра ждёт клавишу действия, чтобы перейти дальше; когда
  квест не пускает дальше, на нажатие отвечает сама игра.
- Число на кнопке зелья считает и противоядия: клавиша зелья игры пьёт и их.
- Значок «только шагом» под полосками больше не гадает о причине (перегруз или квест): игра её не сообщает.
- Выбор зарядов на море предлагает и те, которых нет в трюме (с красным 0), как клавиши 1–4 игры: игра переключает
  заряд и сама не заряжает пушки без ядер.
- Подзорная труба: если у капитана мушкетный залп, а тип пушек не показан, надпись «Мушкетный залп» больше не
  выдаётся за тип пушек.

**English**

- A shot aimed from the deck at an enemy ship no longer counts as an attack on a ship of unknown relation, nor a shot
  at a neutral one as a shot at an enemy.
- A block no longer stops a blow from behind: a strike from more than 150° off the defender's facing breaks it.
- The quest sign over a character is hidden while that character fights, is dead or leaves the location.
- One sky for every game: instead of the painted sky textures, a sky computed as Caribbean Legend computes it, that
  follows the time of day, with a warm sunset, haze at the horizon, the sun, the moon, stars and clouds that drift
  with the wind. Each hour's cloud cover comes from the game's weather; the clouds are lit as Caribbean Legend's are
  (warm at sunset, moonlit at night); rain and storms close the sky with dark clouds, and the horizon and the sea
  darken with them. The stars are made anew (the game's star catalogue and planets are no longer drawn), and the sun and the
  moon are discs of the sky with the game's own pictures (the moon in its phase by the date).
- Caribbean Legend: the sky's clouds are the game's own three cloud texture layers (rain clouds, the main clouds,
  cirrus), lit and slowly turning as its engine draws them; the camera's exposure and grade come from its periods of
  the day, and the scene's fill light from the drawn sky without its clouds, so the night is no longer nearly black.
- Caribbean Legend: at a rainy hour the sun's and the moon's light on the scene, on the clouds and on the discs comes
  from the game's rain preset, as in its engine: a rainy day is overcast, with dimmer, flatter light and faint
  shadows.
- In «Каждому своё» the sun's shadows now follow the hour's own light direction, as the game's weather sets it.
- Under rain clouds the sun has no glow, and in «Каждому своё» it casts no shadows either.
- Lightning flashes light the sky and the scene as in Caribbean Legend: at night the clouds and everything around
  light up for a moment, by day the sky and the fill light dim for a moment. In «Каждому своё» each of the game's
  lightning bolts gives a flash.
- Caribbean Legend: while the hour rains and the scripts show the rain, the game's own rain effect falls around the
  camera.
- Scripts: an unbraced loop or if body is exactly one statement, as in C; the next statement no longer falls into it.
  In «Каждому своё» this changes one place: the date of the previous payment in the ship's debts window is updated on
  every payment.
- The storage screen showed a wrong weight for goods and for the deal; a pack and the goods moved now weigh what they
  should.
- Desktop build: graphics no longer start on the phone preset «Низкая» (Low). Every renderer feature is an option of
  its own (anti-aliasing, sun and lamp shadows, bloom, the sea, relief and more), full quality by default with the
  scene at the window's size and, as on phones, no texture-map generation; the options are kept per game. In «Каждому своё» they are on the game's options screen:
  the check box «Графика движка» beside «Всегда бежать» lists them, a double click or Enter changes a value.
- Shipyard: «Все» in the repair window repaired the hull first, while the game repairs the sails first, so with too
  little money the repair came out different; the repair sliders stopped where the game did not, and the repair price
  and the trade-in difference were the screen's own sums. «Все» is now the game's button, the sliders stop where the
  game stopped them, and the price and the difference are the game's.
- Sail emblem: the screen refused to paint or remove an emblem with less money than the price, which the game
  allows.
- Crew shares: the «Заплатить» button carried a sum the screen worked out, which with only a debt to the state was not
  what the game pays, and the reason given for a disabled button was the screen's guess. It is now the game's button,
  without a sum of its own.
- Self repair: the planks and sailcloth on the «Починить» button were the screen's estimate, and the sliders' limits
  were read from the window's Russian captions. The materials are now the game's figures, and a slider stops where the
  game stopped the repair.
- Crew hire: the screen worked out how many men could be hired and what they would cost, and showed the purse
  «after the hire» by its own sum. The number now goes to the game at once, and the screen shows how many men the
  game set and the cost it named.
- Abilities: "Learn" is offered for every ability not yet learned; the game decides. When the game refuses, the button
  goes off and what is missing (the abilities needed, or points) pulses.
- Items: a disabled button no longer gives a reason we made up (it named only one of the game's reasons); there are
  as many amulet slots as the character has in the game.
- Nations: flags are no longer greyed out in advance by the interface. A flag the game will not raise goes off
  after a tap, and the chosen flag stays as it was. The tone of a reputation follows the game's own word. The game's
  help on reputation and flags opens as a reading sheet.
- Cards: points are counted as the game counts them (a mod's deck with other values shows the right total); the
  opponent's name is the game's full name.
- The world map's compass no longer forecasts rain: the game builds no sea weather while the ship is on the map, and
  the forecast could be wrong. The compass shows only the storm or tornado zone the ship is in.
- World map: the map's commands show also while the game forbids leaving the map, and the game answers a tap with its
  own message; a command of a mod or another game shows with a plain icon instead of removing every button. The
  provision days on the compass no longer turn red by a threshold of our own: the game itself warns when provisions
  run low.
- Land action menu: the potion command is available exactly when the game's own menu offers it; an antidote alone at
  full health no longer opens the potion list.
- Boarding: the «Next» button shows the whole time the game waits for the action key to go on; when a quest holds the
  boarding, the game itself answers the tap.
- The count on the potion button includes antidotes: the game's potion key drinks them too.
- The «walking only» icon under the bars no longer guesses the reason (overload or a quest): the game does not tell it.
- The charge picker at sea also offers the kinds the hold lacks (with a red 0), as the game's keys 1–4 do: the game
  switches the charge and itself does not load guns without shot.
- Spyglass: when the captain has the musket volley and the cannon type is not shown, «Musket volley» is no longer shown
  as the cannon type.

## 1.0.0-alpha.1 — 2026-11-26

**Русский**

Первая публичная альфа Seawolves — неофициального фанатского порта «Корсаров: Каждому своё» на Android.
**Файлов игры в приложении нет:** нужна своя легально полученная копия версии 1.7.3.

- Импорт игры из папки, архива ZIP, 7z или RAR либо установщика `setup.exe`; загрузка по вашему torrent-файлу.
- Сенсорное управление на суше, в море и на глобальной карте.
- Новый интерфейс на шести языках.
- Vulkan и OpenGL ES, три пресета качества, переработанные свет и тени.
- Ролики, автосохранения, перенос сохранений между установками.
- Исправления скриптов игры; пираты на глобальной карте встречаются реже (меняется в настройках).

Требования: Android 8.0+, arm64, 4 ГБ оперативной памяти, 15 ГБ свободного места. Подробности и ограничения альфы —
в README.

**English**

The first public alpha of Seawolves, an unofficial fan port of Sea Dogs: To Each His Own to Android.
**No game files are included:** you need your own legally obtained copy of version 1.7.3.

- Game import from a folder, a ZIP, 7z or RAR archive or the `setup.exe` installer; download from your own torrent
  file.
- Touch controls on land, at sea and on the world map.
- A new interface in six languages.
- Vulkan and OpenGL ES, three quality presets, reworked lighting and shadows.
- Videos, autosaves, moving saves between installations.
- Fixes to the game's scripts; pirates on the world map are rarer (a setting).

Requirements: Android 8.0+, arm64, 4 GB of RAM, 15 GB of free storage. Details and the alpha's limitations are in the
README.
