[English](README.md) | **Русский**

# engine/

Нативный движок [Seawolves](../README.ru.md): форк [Storm Engine](https://github.com/storm-devs/storm-engine), движка
серии «Корсары» (Sea Dogs), перенесённый на Android. Он собирает `libengine.so`, который приложение из
[`../app`](../app) упаковывает в APK, и сборку того же движка под Linux для разработки и проверок.

Что порт добавляет к оригиналу:

- **Рендер на bgfx** (`src/libs/renderer_bgfx`): OpenGL ES 3.0 или Vulkan 1.1 через
  [bgfx](https://github.com/bkaradzic/bgfx) вместо DirectX 9. Старый код рисует через прослойку с интерфейсом D3D9
  (`src/shim`); в `fx/` освещение, тени, рельеф, море и постэффекты. Форматы текстур и эффекты следуют
  возможностям GPU, всё отключённое пишется в лог.
- **Сенсорный интерфейс** (`src/libs/ui`, `src/libs/ui_host`): экраны и HUD игры заново на
  [RmlUi](https://github.com/mikke89/RmlUi), под управлением собственных интерфейсных скриптов игры.
- **Платформенный слой Android** (`src/apps/engine`, `src/libs/util/include/platform`, `android/`): жизненный цикл и
  автосохранение, сенсорный ввод, звук, импорт архивов, загрузки, диагностика и логи.
- **Графика и производительность для телефонов**: карты рельефа, нормалей и глянца из текстур игры
  (`src/libs/relief`), нейросетевое увеличение текстур ×2, дальние LOD, фоновая подготовка ресурсов с кэшем,
  учёт нагрева, пресеты качества.

Поддерживается и проверена только «Корсары: Каждому своё». Другим играм на этом движке могут понадобиться отличия
в скриптах и ресурсах, которых порт не покрывает. Файлов игры здесь нет: движок читает установленную копию игрока.

**Имена.** `storm-engine`, `StormEngine`, пространства имён `storm::` и переменные `STORM_*` — имена оригинала, они
сохранены, чтобы изменения оригинала по-прежнему вливались. Seawolves — имя приложения и репозитория.

## Каталоги

| Путь | Что |
| --- | --- |
| `src/apps/engine` | исполняемый файл движка / точка входа `libengine.so` |
| `src/libs/*` | библиотеки движка (оригинальные и порта) |
| `src/techniques` | техники рендера, которые называют скрипты игры |
| `android/` | нативные части только для Android: чтение архивов, торрент-сессия, звук |
| `cmake/` | настройка сборки: тулчейны host и Android, закреплённые зависимости, ccache |
| `tests/` | проверки на хосте, по каталогу на проверку |
| `tools/` | инструменты сборки, CI и разработки ([tools/ci/README.md](tools/ci/README.md)) |
| `third_party/licenses` | лицензии и происхождение стороннего кода и моделей |

## Сборка

Библиотека для Android (из `engine/`; подробности в [tools/android/README.md](tools/android/README.md)):

```sh
bash tools/android/ci_dependencies.sh   # закреплённые NDK, инструменты SDK и зависимости в build/android-ci
bash tools/android/ci_engine.sh         # build/android-engine: libengine.so для arm64-v8a
```

APK собирает приложение: `bash app/tools/android/build_debug.sh` из корня репозитория.

Интерфейс на хосте (библиотека интерфейса с тестами; без игры и GPU):

```sh
cmake -S src/libs/ui -B build/ui-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ui-host
xvfb-run -a ctest --test-dir build/ui-host --output-on-failure   # без xvfb-run, если есть дисплей
```

Весь движок под Linux собирается из тех же закреплённых исходников, что и для Android, плюс SDL2 и zlib системы
(`cmake/host.cmake`). `tools/ci/boot_stage.py` настраивает и собирает его (с санитайзерами) в `build/boot-asan`.

## Проверки

Перед pull request запустите `tools/ci/gate.py` ([tools/ci/README.md](tools/ci/README.md)): он собирает и проверяет
то, на что может повлиять изменение. Проверки лежат в пяти местах:

| Где | Что | Как запускается |
| --- | --- | --- |
| `tests/<name>/run.py` | проверки на хосте: каждая компилирует охраняемый код движка с небольшими заглушками (без игры, устройства и GPU, если не сказано иное) | `python3 tools/ci/run_host_tests.py` (все или `--only NAME`); CI на каждый push и pull request |
| `src/libs/ui/tests` | интерфейс: экраны, HUD, ввод, вёрстка, цена кадра | ctest сборки интерфейса (выше); CI |
| `src/libs/renderer_bgfx/fx/tests` | проверки CPU-части пакета эффектов | собирает и запускает `tests/renderer_bgfx/run.py` (проверка на хосте) |
| `tools/game_checks/<name>` | проверки на настоящих данных игры | `gate.py` (нужен `STORM_GAME_DIR`) или `python3 tools/game_checks/<name>/run.py <папка игры>` |
| `tools/scene_sweep` | обход сцен: движок на хосте загружает сцены игры и проверяет каждый кадр | по запросу перед выпуском ([tools/scene_sweep/README.md](tools/scene_sweep/README.md)); `tools/ci/boot_stage.py` проходит три его сцены |

## Переменные окружения

| Переменная | Где | Что |
| --- | --- | --- |
| `STORM_GAME_DIR` | проверки на данных игры, boot stage, обход сцен, `tests/video` (видео игры) | установленная копия игры |
| `STORM_BOOT_SAVE` | boot stage | сохранение, в котором герой стоит в городе на Мартинике |
| `STORM_VIRTUALGL` | boot stage, обход сцен, `tests/renderer_bgfx` (проверки с мультисэмплингом) | распакованный VirtualGL, чтобы рисовать на GPU хоста |
| `STORM_CI_WORK` | сборка Android, gate, boot stage | каталог зависимостей (по умолчанию `build/android-ci`) |
| `STORM_ALLOW_COLD_BUILD` | ccache-обёртка | разрешить сборку в каталоге без прогретого кэша ([tools/ccache/README.md](tools/ccache/README.md)) |

Данные игры, сохранения и всё, что из них сделано, остаются вне репозитория.

## Лицензия

GPL-3.0-only, как её выпустил оригинал ([LICENSE.txt](LICENSE.txt)). Лицензии и происхождение стороннего кода и
моделей: [third_party/licenses](third_party/licenses), [android/archive/licenses](android/archive/licenses). Точка
форка и лицензии остального репозитория: [../NOTICE](../NOTICE).

Спасибо авторам Storm Engine за открытый движок и сообществу [storm-devs](https://github.com/storm-devs/storm-engine),
которое его поддерживает.
