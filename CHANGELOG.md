# Changelog / Список изменений

## 1.0.0-alpha.2 — unreleased

**Русский**

- Выстрел с палубы по вражескому кораблю больше не считается нападением на корабль с неизвестными отношениями, а
  выстрел по нейтральному — выстрелом по врагу.
- Блок больше не спасает от удара в спину: удар сзади (дальше 150° от взгляда защищающегося) пробивает блок.
- Знак квеста над персонажем скрывается, пока тот дерётся, мёртв или уходит из локации.
- Небо одно для всех игр: вместо нарисованных небесных текстур — небо, рассчитанное так же, как в Caribbean Legend,
  со сменой времени суток, тёплым закатом, дымкой у горизонта, солнцем, луной, звёздами и облаками, которые медленно
  плывут по небу. Облачность каждого часа взята из погоды игры. Игре без облачных текстур небо при загрузке строит
  объёмные кучевые облака разной величины с плоским основанием и круглыми вершинами, освещённой и теневой стороной,
  меньше и плотнее к горизонту; они рисуются и освещаются тем же путём, что облачные слои Caribbean Legend (тёплые на
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
  небо и рассеянный свет на миг приглушаются. В «Каждому своё» каждая молния ночью на миг высвечивает облака и всё
  вокруг, а днём вспышкой остаётся её собственное сияние.
- Caribbean Legend: пока час дождливый и скрипты показывают дождь, вокруг камеры идёт дождь — эффект самой игры.
- Сырость после дождя одна для всех игр и считается, как в движке Caribbean Legend: в дождь улица мокрая по силе
  дождя, а после него сохнет около трёх игровых часов (на треть за час), а не за четыре минуты. В «Каждому своё»
  улица остаётся мокрой и после выхода из таверны или дома. На «Средней» и «Высокой» в Caribbean Legend материалы
  под открытым небом в дождь и после него становятся глаже (шероховатость до половины), как в её движке.
- Caribbean Legend: арифметика над числом, переданным в функцию по ссылке, больше не записывает результат в само это
  число, — так считались, например, скидки бюджета эскадры и цены торговых сделок.
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
  ячеек для амулетов столько, сколько их у персонажа в игре. Кнопка действия называется так, как её называет игра
  («Взять», «Снять», «Удалить»), и доступна, когда игра её разрешает. Ячейки пистолета и мушкета показывают всё
  огнестрельное оружие.
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
- Магазин: деньги в шапке — те, что у вас по игре; они меняются, когда сделка совершена, а не предсказываются
  экраном, пока выбирается количество.
- Контрабандисты: экран торговли с ними — скриптовый. Сколько можно купить или продать, решает окно игры (раньше
  экран сам ограничивал только покупку деньгами и трюмом); у товаров больше нет пометок «Импорт» и «Экспорт» и
  фильтра «Выгодно»: цвет названия у контрабандистов значит не тип торговли.
- Орудия и торговля предметами: экраны — скриптовые. Сколько можно купить или продать, решает окно игры: продажа
  тоже упирается в то, что игра принимает (место у торговца, его деньги, квестовые предметы не продаются), а не
  только покупка в наши деньги и трюм.
- Найм команды, ремонт своими силами и символика на парусах теперь построены скриптами интерфейса. В ремонте
  каждое перемещение ползунка сразу отдаётся стрелкам самой игры, и строка показывает, где игра остановилась; слово
  морали в найме пишется так, как его пишет игра.
- Сундук: вес одного предмета — вес самого предмета в игре, а не общий вес из таблицы, делённый на число предметов.
- Грабёж захваченного города: экран — скриптовый. «Забрать самое ценное» по-прежнему берёт сначала самое дорогое на
  центнер трюма, но сколько каждого товара поместится и сколько он весит, считает игра; окно количества показывает
  вес сделки, как его пишет игра.
- Магазин: пометки «Экспорт», «Импорт», «Спрос» и «Контрабанда» берутся из типа торговли товара в магазине игры,
  а не из цвета названия; квестовый товар, который игра выделяет своим цветом, тоже получает свою пометку.
- Верфь построена скриптами интерфейса. Покупка и продажа — обычные кнопки: игра задаёт свой вопрос с ценой сделки
  (при обмене — с разницей, как её считает игра), и он показывается листом. На кнопке покупки при обмене стоят обе
  цены игры: корабля и того, что верфь платит за наш. Кнопка описания корабля убрана: описание и так на карточке.
- Кости и карты в таверне построены скриптами интерфейса. Очки карт и комбинации костей — те, что считает игра, имя
  соперника — как его пишет игра. После кона итог говорит строка самой игры: отдельной плашки «выигрыш» и «забрал»
  больше нет. Кубик, карта и бросок больше не запрещаются экраном за нехватку денег на ставку — игра сама отвечает,
  хватает ли.
- Обмен с другим кораблём (добыча после абордажа, компаньон, встречный корабль): экран — скриптовый. Ползунок товара и
  команды сразу двигает стрелки самой игры и показывает, где игра остановилась (полный трюм, предел команды), а не
  предел, посчитанный заранее; у «Перегрузить всё» больше нет подписи с весом, которая могла превышать то, что
  поместится.
- Экран корабля построен скриптами интерфейса. Вопросы игры (поднять мораль, обмен с кораблём компаньона) показываются
  её словами, с её ценой и её отказом; доли команды — строки самой игры. Пушки по борту ставятся стрелками игры по
  одной, и борт показывает, где игра остановилась. Слова опыта команды и морали пишутся так, как их пишет игра.
- Персонаж и способности: экран — скриптовый. Состояние здоровья и роли пишутся так, как их пишет игра; разбор
  значения навыка показывает шаги самой игры без нашей сверки.
- Экран алхимии построен скриптами интерфейса. Сколько партий можно изготовить, сколько минут они займут и само
  изготовление — функции самой игры (исправление скрипта для «Каждому своё» 1.7.3): число партий по-прежнему
  выбирается ползунком, игра сама кладёт в котёл нужное и изготавливает. На кнопке «Изготовить» больше нет числа
  предметов. Без этого исправления (другая версия игры) окно алхимии остаётся окном самой игры.

**English**

- A shot aimed from the deck at an enemy ship no longer counts as an attack on a ship of unknown relation, nor a shot
  at a neutral one as a shot at an enemy.
- A block no longer stops a blow from behind: a strike from more than 150° off the defender's facing breaks it.
- The quest sign over a character is hidden while that character fights, is dead or leaves the location.
- One sky for every game: instead of the painted sky textures, a sky computed as Caribbean Legend computes it, that
  follows the time of day, with a warm sunset, haze at the horizon, the sun, the moon, stars and clouds that slowly
  drift across the sky. Each hour's cloud cover comes from the game's weather. For a game without cloud textures the
  sky makes cumulus at load: of many sizes, with flat bases and round tops, a lit and a shadowed side, smaller and
  closer together towards the horizon; they are drawn and lit by the same path as Caribbean Legend's cloud layers
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
  lightning bolts lights the clouds and everything around for a moment at night; by day its own glow is the flash.
- Caribbean Legend: while the hour rains and the scripts show the rain, the game's own rain effect falls around the
  camera.
- Wetness after a rain is one for every game and is computed as Caribbean Legend's engine computes it: in the rain the
  street is wet by the rain's strength, and after it dries over about three game hours (a third per hour) instead of
  four minutes. In «Каждому своё» the street stays wet after leaving a tavern or a house too. On «Средняя» and
  «Высокая» Caribbean Legend's materials under the open sky get smoother in and after the rain (roughness down to
  half), as in its engine.
- Caribbean Legend: arithmetic on a number passed to a function by reference no longer writes its result into that
  number; the squadron budget's discounts and trade deal prices, among others, were computed so.
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
  as many amulet slots as the character has in the game. The action button carries the game's own caption (equip,
  remove, delete) and is enabled when the game allows it. The pistol and musket slots both list every firearm.
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
- Store: the money in the header is what the game says you have; it changes when the deal is made instead of being
  predicted by the screen while the amount is chosen.
- Smugglers: their trade screen is a script screen. How much can be bought or sold is the game's window's to decide
  (the screen limited only a purchase, by the purse and the hold); goods no longer carry «Import» and «Export» tags
  or a «Profitable» filter: with smugglers the colour of a name is not a trade type.
- Cannons and items trade: script screens. How much can be bought or sold is the game's window's to decide: a sale
  too stops where the game stops it (the merchant's room and money; quest items are not sold), not only a purchase
  by our purse and hold.
- Crew hire, self repair and the sail emblem are now built by the interface scripts. In the repair every move of a
  slider goes to the game's own arrows at once, and the row shows where the game stopped; the morale word of the hire
  is written as the game writes it.
- Chest: the weight of one item is the game's weight of that item, not the table's total divided by the count.
- Looting a captured town: a script screen. «Take the most valuable» still takes what is worth most per cwt of the
  hold first, but how much of each good fits and what it weighs is the game's to count; the amount window shows the
  deal's weight as the game writes it.
- Store: the «Export», «Import», «Demand» and «Contraband» tags come from the good's trade type in the game's store,
  not from the colour of its name; a quest good, which the game colours as its own, gets its tag too.
- The shipyard is built by the interface scripts. Buying and selling are plain buttons: the game asks its question
  with the deal's price (for a trade-in with the difference as the game works it out), shown as a sheet. When a ship
  is traded in, the buy button names both of the game's prices: the ship's and what the yard pays for ours. The ship
  description button is gone: the card already shows the description.
- Tavern dice and cards are built by the interface scripts. Card points and dice combinations are the ones the game
  counts, and the opponent's name is written as the game writes it. After a round the game's own line tells the
  outcome: the separate «winnings» and «pot taken by» badge is gone. A die, a card or a throw is no longer refused by
  the screen for want of money for the stake: the game answers whether there is enough.
- Exchange with another ship (a boarded prize, a companion, a ship met at sea): a script screen. The goods and crew
  sliders move the game's own arrows at once and show where the game stopped (a full hold, the crew's limit) instead
  of a limit worked out beforehand; «Take all cargo» no longer has a weight line, which could exceed what fits.
- The ship screen is built by the interface scripts. The game's questions (raising morale, the exchange with a
  companion's ship) are shown in its words, with its price and its refusal; the crew's shares are the game's own
  lines. The guns of a side are set by the game's arrows one at a time, and the side shows where the game stopped.
  The crew's experience and morale words are written as the game writes them.
- Character and abilities: a script screen. The health word and the roles are written as the game writes them; a
  skill's breakdown shows the game's own steps without our cross-check.
- The alchemy screen is built by the interface scripts. How many batches can be made, how many minutes they take and
  the making itself are the game's own functions (a script fix for TEHO 1.7.3): the number of batches is still set
  with the slider, and the game lays what they take into its pot and makes them. The «Craft» button no longer shows a
  count of items. Without that fix (another game version) the alchemy window stays the game's own.

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
