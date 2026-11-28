# Changelog / Список изменений

## 1.0.0-alpha.2 — unreleased

**Русский**

- Загрузка записи и «Продолжить» оставляют на экране меню, пока не появится экран загрузки: раньше между ними на
  долю секунды мелькали кнопки игры поверх старой сцены.
- Автосохранение при сворачивании игры, сделанное, пока открыт экран сохранений или другое меню, получает картинку
  последней показанной сцены: раньше его картинка могла быть чёрной.
- Сохранение, прерванное закрытием игры, не оставляет в папке сохранений скрытого недописанного файла: при следующем
  запуске он удаляется, а полностью записанная новая запись, которой не успели дать имя, появляется в списке.
- Когда памяти мало и Android удаляет часть файлов кэша приложения, недостающие файлы интерфейса ставятся
  заново при запуске игры, при возвращении в неё и в тот момент, когда игре понадобился удалённый файл: раньше игра
  замечала только полностью удалённый кэш и могла закончиться ложным сообщением, что скрипт интерфейса «не
  компилируется», а удаление посреди игры завершало её при следующей загрузке.
- Сообщение об ошибке, на которой игра остановилась, написано на языке игры — и в игре, и при запуске (нет файлов
  игры, повреждены исправления скриптов, недоступны журналы): раньше его заголовок и пояснение были по-английски.
- Экран интерфейса, который не смог открыться или сломался посреди работы, больше не закрывает окно игры: до его
  закрытия окно показывается раскладкой по умолчанию с коротким сообщением, а при следующем открытии экран пробует
  снова. Окно, к которому экран не подходит в этой игре, сразу показывается раскладкой по умолчанию. Так же
  поступает экран, который нарисовался бы неверно (в его данных нет нужного поля или значение не того вида): раньше
  он показывался с пустыми местами.
- Список загрузки, где у двух игр (профилей) есть записи с одинаковым именем, загружает и удаляет именно
  выбранную запись: раньше «Загрузить» и корзина у второй такой записи действовали на первую.
- Повреждённая запись в списке загрузки объясняет, почему её нельзя загрузить: причина написана над неактивной
  кнопкой «Загрузить».
- Экран сохранений после неудачного сохранения (например, когда память телефона заполнена) возвращается к списку
  с сообщением о причине, прежняя запись цела и выбирается: раньше на нём оставался вопрос «Перезаписать?», кнопки
  которого не отвечали, и выйти можно было только кнопкой «Назад». Закрытый игрой вопрос (в том числе после «Отмены»)
  больше не остаётся на экране.
- Обмен с другим кораблём: ползунок груза и команды только выставляет количество, а перегружает кнопка «Взять N» /
  «Отдать N» — случайное касание больше не перекладывает груз; столбцы товаров снова сортируются касанием по
  заголовку.
- Верфь: покупка и продажа корабля снова делаются удержанием кнопки — удержание отвечает «да» на вопрос игры о цене
  одним жестом; любой другой вопрос игры показывается как раньше.
- Закупка товаров казначеем рисуется скриптом интерфейса: заказ, трюм и корабль берутся из самой игры, а меню «⋯»
  больше не перечисляет картинки товаров.
- Окно игры без своего экрана (раскладка по умолчанию) теперь рисуется скриптом интерфейса: кнопки «‹ ›»
  листают, а не занимают место кнопок окна (на выборе героя кнопки начала игры больше не прячутся под «⋯»),
  вкладки-картинки нажимаются, таблица рядом с другими блоками не теряет строк после касания, а пустое окно
  говорит, что в нём нечего нажать.
- Верфь: при показанном предложении верфи свой корабль снова выбирается касанием — карточка возвращается к нему и к
  «Ремонту», а отмеченной остаётся одна строка; раньше касание ничего не делало, и отмеченными выглядели две строки.
- Тени панелей, кнопок и окон рисуются сразу, без заготовки текстур: экран с тенями появляется в первом же кадре
  (раньше мог запаздывать на несколько кадров), а тень шириной во весь экран больше не обрезается. Тени чуть мягче
  прежних; внутренняя тень нажатой золотой кнопки и пергаментных карточек теперь видна поверх их заливки.
- Значки интерфейса больше не рисуются на телефоне при первом показе: они заранее собраны в одну текстуру и
  окрашиваются при выводе. Первое открытие экрана с новыми значками не подтормаживает; края значков могут быть на
  долю пикселя мягче или резче.
- Выход из моря на карту мира или на берег больше не читает освобождённую память корабля: раньше
  после конца морской сцены боевой интерфейс касаний читал данные уже удалённого корабля, что могло закончиться
  вылетом.
- Кнопка «Причалить» у острова больше не читает несуществующее значение точки причала: раньше её цель
  собиралась с чтением мимо данных, и кнопка могла пропадать у причала или, реже, приводить к вылету.
- Корабль на карте мира в первом кадре после появления рисуется моделью: раньше признак следа за ним был не задан,
  и вместо корабля мог мелькнуть след из незаполненных точек.
- Пена у борта корабля, у которого верхняя точка левого борта уже под водой (сильный крен, тонущий корабль), больше
  не читает память перед массивом точек корпуса.
- Паруса упавшей мачты начинают с заданных значений: раньше признаки спуска парусов были не заданы, и могло
  прозвучать лишнее движение парусов.
- Картинка интерфейса, которой скрипт передал «нет текстуры» (-1), больше не портит память отрисовщика.
- Строка товара в магазине и трюме, флаг в «Нациях», корабль на верфи, предмет, ячейка и вкладка в «Предметах»,
  отмеченные касанием, остаются отмеченными,
  если список успел измениться до ответа игры: раньше отметка могла перескочить на соседнюю строку, а касание —
  открыть не тот товар.
- Выбор в боевом интерфейсе (приказ в море, строка действий на суше, команда на карте, положение парусов)
  остаётся нажатым, пока игра не ответила: раньше он гас через полторы секунды, даже если игра ещё выполняла
  команду, и висел до полутора секунд после её отказа.
- Ответ в диалоге остаётся нажатым, пока игра его не приняла или не отказала: раньше отметка гасла через полторы
  секунды. Ответ, который игрок вводит сам (имя, ставка), открывает обычный ввод текста с предложением игры;
  сумма больше не ограничивается деньгами героя — её проверяет сама игра.
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
- Лужи одни для всех игр, как в Caribbean Legend: растут с сыростью от самых глубоких мест, вокруг воды — тёмная
  мокрая кайма, вода почти чёрная и гладкая, в дождь по ней расходятся круги от капель. Caribbean Legend: лужи
  самой игры (Бриджтаун, Гваделупа) больше не лежат тёмными пятнами в сухую погоду — их не видно, пока не
  намокнет. В «Каждому своё» на «Высокой» лужи по-прежнему собираются в углублениях рельефа, но тем же видом.
- Ветер один для всех игр: направление, сила и порывы (в дождь — как в движке Caribbean Legend) берутся из погоды.
  Caribbean Legend: пальмы, деревья, кусты и навесы, размеченные самой игрой, качаются на ветру — в сильный ветер
  пальмы клонятся и раскачиваются чаще, листья трепещут, края навесов колышутся. В «Каждому своё» трава берёт тот же
  ветер. Для игры без такой разметки есть настройка «Качание растений (догадка)», по умолчанию выключена: растения
  узнаются по текстурам и качаются тем сильнее, чем выше.
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
- На экране персонажа ключевой навык должности берётся из самой игры: у канонира — лучший из «Орудий» и «Меткости»,
  которые игра берёт у него для героя.
- Экран наций: когда игра отказывает в смене флага (смена ещё идёт, на суше, компаньон под ударом, эскадра не может
  выйти на карту), под кнопкой «Сменить флаг» пишется почему, а не только звучит стук.
- Экран предметов: под местом пистолета перечисляются только пистолеты, под местом мушкета — только мушкеты, как
  их различает сама игра.
- Перебежчики после абордажа: окно считает тех, кого игра уже приняла, из всех желающих, и «Принять» берёт столько,
  сколько выбрано; раньше кнопка показывала желающих, а принимала никого. В карточке корабля компаньона и сдавшегося
  капитана его имя теперь видно и в окне команды.
- Экраны интерфейса обновляют только то, что изменилось: строка списка сохраняет свой элемент, пока она в списке.
  Кнопка покупки и всё под списком в сделке больше не пропадают после смены фильтра, а значок предмета в ячейке
  больше не остаётся цвета прежнего значка.
- Переход из окна корабля в другой раздел (журнал, предметы и другие) больше не закрывает окно с ошибкой: экран
  раздела, который игра уже закрыла, остаётся как есть до открытия следующего и больше не перестраивается.
- В окне алхимии больше нет меню «Прочее» с сырыми строками таблицы компонентов и кнопками под именами узлов.
- В торговле орудиями, контрабандой, предметами и в сундуках больше нет меню «Прочее» с кнопками «+ max», «+», «−»,
  «− max»: количество задаётся ползунком экрана.
- Сохранение записывается в файл целиком, одной записью: состояние игры вместе с описанием и картинкой. Раньше это
  были две записи подряд, и если игру закрывали или место кончалось между ними, прежнее сохранение уже было удалено,
  а новое не загружалось. Теперь, если запись не удалась или игру закрыли посреди неё, в слоте остаётся прежнее
  сохранение; о неудаче на любом шаге игра сообщает. Caribbean Legend теперь узнаёт от движка, что сохранение
  записано.
- Перезаписываемое сохранение больше не удаляется окончательно по времени (через 10 секунд после нажатия), а только
  когда новое сохранение записано. Раньше, если игру сворачивали сразу после перезаписи слота на экране сохранений,
  она стояла в фоне дольше 10 секунд и запись потом не удавалась (кончилось место), пропадали оба сохранения.
- Загрузка проверяет сохранение целиком до того, как стирает текущую игру: сохранение, сделанное другой программой
  скриптов (другой каталог скриптов или файл скрипта, которого у этой игры нет — чужая игра, изменённый файл),
  отклоняется с сообщением, а игра продолжается. Раньше такое сохранение оставляло движок без скриптов и без меню.
- Скрипт игры, который не завершается (бесконечный цикл), теперь останавливает игру с сообщением, и после этого
  ничего не сохраняется. Раньше такой вызов обрывался посередине через 60 секунд, игра шла дальше с наполовину
  выполненной логикой, и это состояние попадало в автосохранение. Предел считается в выполненных инструкциях
  скрипта, а не в секундах: медленный диск, пауза в фоне и долгая загрузка его не задевают. Настройка
  `call_time_limit` в engine.ini больше не читается.
- Описание сохранения с повреждённым байтом больше не превращается в строку вопросительных знаков: заменяется только
  сам повреждённый байт (знаком «�»), остальной текст, в том числе кириллица, остаётся как был.
- Последний шаг появления подсказки и других плавных переходов интерфейса теперь рисуется сразу: раньше он мог
  остаться не показанным до следующего изменения на экране.
- Два морских приказа — команда спутнику и мгновенное уничтожение — получили свои значки вместо общего: раньше
  интерфейс не узнавал их по именам команд игры.
- Здоровье, которое игра пишет заглавными буквами («ОТЛИЧНОЕ»), на экране персонажа снова показано строчными.
- Список сохранений называет запись так, как её назвала игра (быстрые сохранения — «<профиль> QuickSave N»), а под
  именем пишет героя и место: раньше несколько быстрых сохранений в одном месте выглядели одинаково.
- Обмен с кораблём говорит, когда игра перенесла меньше выставленного (у другого корабля нет места для команды или
  груза): «Перенесено только 0 из 20». Раньше число просто оставалось прежним без объяснения.
- Удар, начатый в тот миг, когда бой на палубе закончился, больше не выводит ошибку интерфейса, а колесо приёмов
  в следующем бою открывается закрытым.
- Карта колонии показывает флаг её нации так, как его рисует игра: колония, чей флаг интерфейс не прочёл, больше не
  выдаётся за пиратскую. Строка товара, предмет или корабль без картинки показывают пустую рамку, а не сундук или
  люггер.
- Грабёж: на вопрос «Потопить корабль?» вторая кнопка называется «Вернуться к грабежу» — она и возвращает к
  грабежу; раньше она называлась «Забрать корабль», хотя корабль без капитана так не забирается.
- Отличная карта: касание другой колонии, пока открыт лист одной, сразу открывает лист новой; раньше карта не
  отвечала, пока лист не закрыть крестиком.
- Диалог: реплика, которую игрок вводит сам (сумма, ставка, имя корабля), выглядит как поле: её отмечает перо,
  предложение игры написано текстом, под ним — «Введите ответ»; раньше она выглядела как обычная реплика.

**English**

- Loading a record and «Continue» keep the menu on the screen until the loading screen is up: the game's buttons
  flashed over the old scene for a moment between them.
- An autosave made as the game goes to the background while the save screen or another menu is open shows the
  last scene that was on screen: its picture could be black.
- A save cut off by the game being closed no longer leaves a hidden unfinished file in the saves folder: the next
  start removes it, and a fully written new record that had not yet taken its name appears in the list.
- When storage runs low and Android removes some of the app's cache files, the missing interface files are
  installed again when the game starts, when it comes back and when the game needs a file that was removed: only a
  wholly cleared cache used to be noticed, the game could end with a false message that an interface script "does
  not compile", and a removal during play ended the game at the next load.
- The message of an error the game stopped on is in the game's language, in play and at start-up (no game files,
  damaged script fixes, logs unavailable): its title and explanation used to be in English.
- A screen of the interface that cannot open or breaks while in use no longer closes the game's window: until it is
  closed the window is shown in the default layout with a short note, and the screen is tried again the next time.
  A window a screen does not fit in this game is shown in the default layout at once. So is a screen that would be
  drawn wrong (a field missing from its data, a value of the wrong kind): it used to show with blanks.
- In a load list where two games (profiles) have records of the same name, «Load» and the trash act on the record
  chosen: on the second such record they used to act on the first.
- A damaged record in the load list says why it cannot be loaded: the reason is written above the disabled
  «Load» button.
- After a save fails (for instance, the phone's storage is full) the save screen comes back to the list with the
  reason shown, and the previous record is intact and selectable: the "Overwrite?" question used to stay on it with
  buttons that did nothing, and only the Back key got out. A question the game closed (also after «Cancel») no longer
  stays on the screen.
- Ship exchange: the cargo and crew sliders only set the amount and the «Take N» / «Give N» button moves it, so a
  stray touch no longer moves cargo; the goods columns sort again by a tap on their head.
- Shipyard: buying and selling a ship is again a hold of its button, which answers the game's price question with
  yes in one gesture; any other question of the game is shown as before.
- The treasurer's purchase list is drawn by the interface's script: the orders, the hold and the ship come from the
  game itself, and the «⋯» menu no longer lists the goods' pictures.
- A game window without a screen of its own (the default layout) is drawn by the interface's script now: its ‹ ›
  buttons switch rather than take the window's buttons' places (the hero choice's start buttons no longer hide under
  «⋯»), picture tabs can be pressed, a table beside other blocks keeps its rows after a tap, and an empty window says
  there is nothing to press.
- Shipyard: with a yard offer shown, tapping your own ship again returns the card to it and to «Repair», and only
  one row is marked; the tap used to do nothing and two rows looked selected.
- Shadows of panels, buttons and windows are drawn at once, without preparing textures: a screen with shadows
  appears in its very first frame (it could be several frames late before), and a shadow as wide as the screen is no
  longer cut. Shadows are slightly softer; the inner shadow of a pressed gold button and of parchment cards now shows
  over their fill.
- Interface icons are no longer drawn on the phone the first time they appear: they are baked into one texture and
  coloured as they are drawn. A screen's first opening with new icons no longer stalls; icon edges may be a fraction of
  a pixel softer or sharper.
- Leaving the sea for the world map or the shore no longer reads a ship's freed memory: the touch HUD
  read the data of a ship already deleted at the end of the sea scene, which could end in a crash.
- The «Moor» button at an island no longer reads a value the mooring point does not have: its target was
  built by a read past the data, and the button could be missing at a mooring point or, more rarely, crash.
- A ship on the world map is drawn as its model in its first frame: its trail flag was unset, and a trail of unfilled
  points could flash instead of the ship.
- The foam at a ship's side whose top left-side point is already under water (a heavy roll, a sinking ship) no
  longer reads memory before the hull's point array.
- The sails of a fallen mast start from set values: their lowering flags were unset, and a stray sail sound could
  play.
- An interface picture a script sets to «no texture» (-1) no longer corrupts the renderer's memory.
- A goods row of the store and of the hold, a flag of the nations screen, a ship of the shipyard and an item, slot
  or category of the items screen marked by a tap
  stay marked when the list changes before the game answers: the mark could jump to a neighbouring row, and the
  tap open other goods.
- A choice in the HUD (a sea order, a row of land actions, a world map command, the sails' position) stays
  pressed until the game answers: it used to go out after a second and a half even while the game was still
  carrying the command out, and to stay for up to a second and a half after a refusal.
- A dialogue reply stays pressed until the game takes or refuses it: the mark used to go out after a second and a
  half. A reply the player types (a name, a bet) opens the text editor with the game's proposal; an amount is no
  longer capped at the hero's money: the game checks it itself.
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
- Puddles are one for every game, as in Caribbean Legend: they grow with the wetness from the deepest places, a dark
  wet rim lies around the water, the water is near black and smooth, and in the rain the drops' rings spread on it.
  Caribbean Legend: the game's own puddles (Bridgetown, Guadeloupe) no longer lie as dark patches in dry weather;
  they show only when it is wet. In «Каждому своё» on «Высокая» puddles still gather in the relief's hollows, with
  the same look.
- The wind is one for every game: its direction, strength and gusts (in the rain, as in Caribbean Legend's engine)
  come from the weather. Caribbean Legend: the palms, trees, shrubs and awnings the game marks sway in the wind; in a
  strong wind the palms lean and swing faster, the leaves flutter, the awnings' edges ripple. In «Каждому своё» the
  grass takes the same wind. A game without such marks has the option «Качание растений (догадка)» (Plant sway, a
  guess), off by default: plants are told by their textures and sway the more the higher they are.
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
- The character screen takes a post's key skill from the game: a cannoner shows the better of Cannons and Accuracy,
  both of which the game takes from him for the hero.
- The nations screen: when the game refuses to change the flag (a change still under way, on land, a companion under
  attack, the squadron unable to go to the map), the «Change flag» button says why, not only with a knock.
- The items screen: the pistol's place lists only pistols and the musket's place only muskets, as the game tells
  them apart.
- Defectors after a boarding: the window counts the men the game has already taken out of all who are willing, and
  «Accept» takes as many as chosen; before, the button showed the willing and took nobody. The card of a companion's
  or a surrendered captain's ship now names him in the crew window too.
- Interface screens update only what changed: a list row keeps its element while it is in the list. The buy button
  and everything under the list in a deal no longer vanish after a filter change, and an item's placeholder icon in
  a slot no longer keeps the colour of the icon shown there before.
- Going from the ship window to another section (the journal, items and others) no longer closes the window with an
  error: the screen of a section the game has already closed stays as it is until the next opens and is not rebuilt.
- The alchemy window no longer has an «Other» menu with raw rows of the component table and buttons named by their
  node ids.
- Trading cannons, contraband and items and the chests no longer have an «Other» menu with the «+ max», «+», «−»,
  «− max» buttons: the screen's slider sets the amount.
- A save is written to its file whole, in one write: the game state with the description and the picture. It used to
  be two writes in a row, and when the game was closed or the storage ran out between them, the previous save was
  already deleted and the new one did not load. Now, when the write fails or the game is closed in the middle of it,
  the slot keeps the previous save; a failure at any step is reported. Caribbean Legend is now told by the engine
  that a save was written.
- A save being overwritten is no longer removed for good by time (10 seconds after the tap), only once the new save
  is written. Before, when the game was sent to the background right after a slot was overwritten in the save
  screen, stayed there for more than 10 seconds and the write then failed (no space left), both saves were gone.
- A load checks the whole save before it erases the running game: a save made by another script program (another
  script folder, or a script file this game does not have — another game, an edited file) is refused with a message
  and the game goes on. Such a save used to leave the engine with no scripts and no menu.
- A game script that does not finish (an endless loop) now stops the game with a message, and nothing is saved
  after it. Such a call used to be cut midway after 60 seconds, the game went on with half-run logic, and that state
  reached the autosave. The limit is counted in executed script instructions, not in seconds: a slow disk, a pause
  in the background or a long load do not touch it. The engine.ini setting `call_time_limit` is no longer read.
- A save description with a damaged byte no longer turns into a row of question marks: only the damaged byte is
  replaced (with «�»), the rest of the text, Cyrillic included, stays as it was.
- The last step of a hint's fade-in and of other interface transitions is now drawn at once: it could stay unshown
  until something else on the screen changed.
- Two sea orders, the companion command and the instant kill, have their own icons instead of the plain one: the
  interface did not recognise them by the game's command names.
- Health, which the game writes in capitals («EXCELLENT»), is shown in lower case again on the character screen.
- The save list names a record as the game named it (quick saves are «<profile> QuickSave N») with the hero and the
  place under the name: several quick saves made in one place used to look the same.
- The ship exchange says when the game moved less than the amount set (the other ship has no room for the men or
  the cargo): «Only 0 of 20 moved». The number used to stay as it was with no word why.
- A strike begun just as the deck fight ends no longer raises an interface error, and the move wheel of the next
  fight starts closed.
- A colony on the map shows the flag the game draws for it: a colony whose flag the interface could not read is no
  longer shown as pirate. A good, item or ship without a picture shows an empty frame instead of a chest or a lugger.
- Plunder: the second answer to «Sink the ship?» reads «Back to the plunder», which is what it does; it read «Keep the
  ship», though a ship left without a captain is not kept that way.
- The excellent map: a tap on another colony while one colony's sheet is open opens the new colony's sheet at once;
  the map used to ignore it until the sheet was closed with its cross.
- Dialogue: a reply the player types (a sum, a bet, a ship's name) looks like a field: the quill marks it, the game's
  proposal is its text with «Enter your answer» under it; it used to look like any other reply.

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
