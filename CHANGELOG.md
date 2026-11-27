# Changelog / Список изменений

## 1.0.0-alpha.2 — unreleased

**Русский**

- Выстрел с палубы по вражескому кораблю больше не считается нападением на корабль с неизвестными отношениями, а
  выстрел по нейтральному — выстрелом по врагу.
- Блок больше не спасает от удара в спину: удар сзади (дальше 150° от взгляда защищающегося) пробивает блок.
- Знак квеста над персонажем скрывается, пока тот дерётся, мёртв или уходит из локации.
- Небо одно для всех игр: вместо нарисованных небесных текстур — рассчитанное небо со сменой времени суток, солнцем,
  луной, звёздами и облаками, которые плывут по ветру. Облачность каждого часа взята из погоды игры; в дождь и шторм
  небо затягивают серые тучи. Звёзды строятся заново (каталог звёзд игры и планеты больше не рисуются), солнце и луна
  — диски неба вместо спрайтов.
- Под дождевыми тучами солнце не отбрасывает теней и не даёт ореола.
- Скрипты: тело цикла или условия без фигурных скобок — ровно один оператор, как в C; следующий оператор больше не
  попадает внутрь. В «Каждому своё» это меняет одно место: дата предыдущего платежа в окне долгов корабля
  обновляется при каждой оплате.

**English**

- A shot aimed from the deck at an enemy ship no longer counts as an attack on a ship of unknown relation, nor a shot
  at a neutral one as a shot at an enemy.
- A block no longer stops a blow from behind: a strike from more than 150° off the defender's facing breaks it.
- The quest sign over a character is hidden while that character fights, is dead or leaves the location.
- One sky for every game: instead of the painted sky textures, a computed sky that follows the time of day, with the
  sun, the moon, stars and clouds that drift with the wind. Each hour's cloud cover comes from the game's weather;
  rain and storms close the sky with grey clouds. The stars are made anew (the game's star catalogue and planets are
  no longer drawn), and the sun and the moon are discs of the sky instead of sprites.
- Under rain clouds the sun casts no shadows and has no glow.
- Scripts: an unbraced loop or if body is exactly one statement, as in C; the next statement no longer falls into it.
  In «Каждому своё» this changes one place: the date of the previous payment in the ship's debts window is updated on
  every payment.

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
