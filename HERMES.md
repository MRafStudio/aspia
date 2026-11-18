# HERMES.md - журнал форка Aspia extreme

Fork-local заметки. Апстрим этот файл не создает, поэтому мержи остаются чистыми.
Апстрим: `dchapyshev/aspia`. Форк: `MRafStudio/aspia`, рабочая ветка `extreme`.

## Текущий статус (проверено)

Проект полностью собирается и работает на этой машине.
Расположение: `D:\.VS\Projects\aspia-extreem`.

**Версия: 3.4.3** (синхронизировано с апстримом 18.11.2026, см. раздел «Синхронизация»).

### Где что лежит

| Что | Путь |
|---|---|
| C++-форк Aspia (эта репа, ветка `extreme`) | `D:\.VS\Projects\aspia-extreem` |
| Проект обвязки на .NET (RadNET) | `E:\11.R-Keeper\3.RadNET` |

`E:\11.R-Keeper\3.RadNET` - отныне постоянный дом .NET-проекта. Там пока пусто, работа
в нём начнётся после решения по этапу 0 (см. `ENGINE-DLL-SPLIT.md`).

Собранные артефакты в `builds\local-win-x64\Release\` (сборка 3.4.3, 18.11.2026, все свежие):

- `aspia_client.exe` (37,9 МБ), `aspia_host.exe` (36,6 МБ), `aspia_router.exe` (29,3 МБ), `aspia_relay.exe` (27,3 МБ)
- `aspia_virtual_display.dll` - драйвер виртуального дисплея (Indirect Display Driver на базе Microsoft IddSampleDriver)
- `aspia_credentials.dll` - COM credential provider (новое в 3.4.x)
- шесть тестовых бинарников: `aspia_base_tests` (705 тестов), `aspia_host_tests` (533),
  `aspia_client_tests` (452), `aspia_router_tests` (360), `aspia_relay_tests` (43), `aspia_common_tests` (22) - все проходят
- `aspia_sha256.exe`, `aspia_signer.exe`, `qtprotoc.exe`

Проверено запуском (18.11.2026):

- client и host поднимают окна в сессии пользователя и живут (проверено tasklist через 6 с), затем закрыты
- версии всех модулей: 3.4.3.8691
- router по `--create-config` создаёт INI-конфиг, ключи, `router.db3` и пользователя admin (пароль печатается один раз)
- relay по `--create-config` создаёт `relay.conf`

**Внимание:** формат конфигов сменился с XML на INI. Старые `router.conf`/`relay.conf` от 3.0.x
новая версия не читает (`IniFile::parse ... Invalid line 2`). При пересоздании router нужен ещё
и чистый `host.pub`, иначе `Host public key file already exists. Continuation is impossible.`
Бэкап старого каталога `C:\ProgramData\aspia` лежал в scratch на время проверки.

Хронометраж на 3.4.3 (замерено 18.11.2026):

- переустановка зависимостей vcpkg под новый `vcpkg.json` (78 пакетов, включая qtbase 6.11.2): **71 минута**
- конфигурация CMake после этого: секунды
- полная сборка (1579 шагов, `-j 8`): **18 минут**
- прогон всех шести тестовых бинарников: ~3 минуты

Первый холодный прогон в августе (с нуля, другой набор пакетов) занял около 2 часов.

## Синхронизация с апстримом (18.11.2026) - версия 3.4.3

Форк отставал на 993 коммита: апстрим ушёл с 3.0.0 до 3.4.3 и добавил Android-клиент и Android-хост.

Процедура (remote `upstream` появился впервые, раньше его в клоне не было):

```bash
git remote add upstream https://github.com/dchapyshev/aspia.git
git fetch upstream --prune --tags
git checkout master && git merge --ff-only upstream/master && git push origin master
git checkout extreme && git rebase master && git push --force-with-lease origin extreme
git submodule update --init
```

- `master` форка - чистый апстрим (0 своих коммитов), поэтому обновляется `--ff-only`.
- `extreme` = master + наши доки (`HERMES.md`, `ENGINE-DLL-SPLIT.md`, `.hermes/`). Rebase прошёл
  без конфликтов: апстрим эти файлы не создаёт, в `.gitignore` он добавил только `/.claude/`.
- Бэкап перед rebase: ветка `extreme-backup-YYYYMMDD`.
- `git push --force-with-lease` может ругнуться `stale info` - это значит, что локальное знание об
  `origin/extreme` устарело; достаточно `git fetch origin` и повторить push.
- Подмодуль `vcpkg4aspia` обязателен к обновлению, иначе дерево грязное и зависимости не те:
  апстрим перевёл его с `b804ff8b6d` на `3bfb0ca094` (vcpkg `2020.04-20648-g3bfb0ca094`).

**Что изменилось в коде за эти 993 коммита (важно для дальнейшей работы):**

- Версия 3.4.3. Появился Android: `source/client/android` (68 файлов / 15 542 строки), `host/android`,
  пресеты `android-base` / `ci-android-base`, опция `ASPIA_GOOGLE_PLAY`.
- **Автор сам выделил ядровые библиотеки**: `aspia_client_core` (static, почти весь клиент вместе
  с виджетами вкладок), `aspia_host_core` (static, включая `host/ui`), `aspia_router_core`,
  `aspia_relay_core`, `aspia_common`. Exe стали тонкими - в них остались main/application.
- В проекте появилась настоящая DLL: `host/credentials/CMakeLists.txt` собирает `aspia_credentials`
  SHARED (COM credential provider для входа в систему через Aspia). Это COM, не C-ABI движка -
  как образец для нашего ABI не годится, но как факт сборки DLL в этом дереве полезно.
- **openh264 выпилен полностью**: остались только `video_encoder_vpx` / `video_decoder_vpx`
  (VP8/VP9 через libvpx). В `vcpkg.json` добавлен feature `realtime` у libvpx, убраны `openh264`
  и `inipp`, у curl появились фичи `ssl`/`sspi`, тестовая сборка требует `qtbase[testlib]`.
- Вместо `inipp` свой парсер: `base/ini_file.{cc,h}`. Настройки клиента - `client/storage/instance_settings`
  (INI), настройки службы хоста пишутся в JSON (`host/settings_util.cc`).
- Адресная книга переехала в `client/storage/` и разбита по файлам: `database_local_hosts`,
  `database_profiles`, `database_credentials`, `database_router`, `database_settings`,
  `master_password`, `backup` / `auto_backup`, `credential_file`. Схема по-прежнему SQLite,
  пароли - DPAPI, мастер-пароль - Argon2id (параметры те же).
- Появились `client/mcp` (MCP-сервер с набором tools, 43 файла) и `base/update` (2 381 строка).
- Тесты теперь на каждый модуль: `aspia_base_tests`, `aspia_client_tests`, `aspia_host_tests`,
  `aspia_common_tests`, `aspia_relay_tests`, `aspia_router_tests` (544 макроса TEST/TEST_F суммарно;
  UI-тесты клиента требуют `Qt6::QOffscreenIntegrationPlugin`).
- Артефакты в `builds/local-win-x64` - от 3.0.0. Кеш CMake и vcpkg от другого набора зависимостей,
  поэтому после синхронизации нужна переконфигурация и переустановка пакетов vcpkg (по времени -
  как холодный прогон, до пары часов), затем полная пересборка.

**Сборка 3.4.3 выполнена 18.11.2026** - что пришлось сделать по ходу (всё внесено в скилл):

1. `vcpkg.exe` в подмодуле был от мая, порты от ноября -> `vcpkg-tools.json: schema version 2 is
   not supported`. Лечение: `bootstrap-vcpkg.bat -disableMetrics` в `vcpkg4aspia`.
2. Конфигурация падала с `vswhere failed with exit code (2147942408)` / `0x80070008: Недостаточно
   ресурсов памяти`: терминал Hermes работает под SYSTEM в session 0, а vswhere 3.1.7 там не
   стартует. Лечение: запускать сборку в интерактивной сессии пользователя (скрипт
   `.hermes/skills/aspia-extreme/scripts/launch.cs` -> `launch.exe`).
3. Сборка падала на `desktop_file.pb.h: #error "Protobuf C++ gencode is built with an incompatible
   version of"` - в каталоге сборки остались .pb.h/.pb.cc от прошлого protoc. Лечение: удалить
   `builds/local-win-x64/source/proto/*.pb.h|*.pb.cc` (перегенерируются, полной пересборки не требуют).

Итог: 78 пакетов vcpkg переустановлены за 71 минуту, полная сборка 1579 шагов за 18 минут,
все шесть тестовых бинарников проходят (2115 тестов).

## Окружение, которое выяснилось по ходу

- Проект использует **Qt6** (`qtbase`, `qtsvg`, `qttools` в `vcpkg.json`), а не Qt5.
  Документация на aspia.org описывает старую сборку с qt5-base и для этой ветки не подходит.
- Зависимости ставятся автоматически в manifest-режиме при `cmake --preset local-win-x64`;
  вручную перечислять пакеты не нужно.
- vcpkg выбирает самую новую установленную Visual Studio (здесь - каталог `18`, VS 2026, MSVC 14.51).
  Компонент MFC/ATL должен быть установлен именно в нее, иначе порт `atlmfc` падает
  на отсутствии `afxres.h`.
- `vcpkg4aspia` должна быть реальной папкой внутри проекта. Qt6 отказывается собираться,
  если путь сборки содержит симлинк или junction.
- Драйвер `virtual_display` требует Windows Driver Kit (заголовки `wudfwdm.h`, `wdf.h`, `IddCx.h`,
  версия UMDF 2.27). WDK 10.0.26100 установлен.
- CMake 4.4.3 установлен в `C:\Program Files\CMake` и присутствует в системном PATH;
  из git-bash он может не находиться - тогда используется полный путь.
- Одновременно в одной папке сборки нельзя держать два запущенных ninja-процесса:
  падают на произвольных таргетах (`qtprotoc`, `sha256`) с невнятными ошибками.

Подробная процедура сборки и разбор всех грабель вынесены в скилл `aspia-extreme`,
чтобы не дублировать их здесь.

## Журнал решений

- Форк создан через GitHub API под учетной записью MRafStudio, ветка `extreme` запушена.
- Исключать порт `atlmfc` из зависимостей или править исходники ради сборки - отклонено владельцем.
  Правильный путь - установить MFC/ATL в ту Visual Studio, которую реально использует vcpkg.
- Junction `vcpkg4aspia` на внешнюю папку был ошибкой: Qt6 падает на симлинках в пути сборки.
  Папка перенесена внутрь проекта как реальная.
- Временные скрипты и установщики после работы удалены; все проекты живут в `D:\.VS\Projects`.

## Следующий этап: вынос движков в библиотеки и обвязка на .NET

Лейтмотив новой сессии. Идея владельца: один раз скомпилировать тяжелые C++-части,
а всю обвязку писать быстро на .NET.

### Разбор выполнен - подробности в `ENGINE-DLL-SPLIT.md`

Коротко, что выяснилось (все цифры замерены по дереву, не на глаз):

- Движок уже отделён от UI слоем `Worker` (`base/threading/worker.h`): свой поток, обмен
  `QByteArray` по 13 каналам и `SharedFrame` (BGRA + stride). Новую архитектуру строить не надо,
  нужен C-фасад поверх воркеров.
- Без GUI движок работает: тестовые бинарники поднимают только `QCoreApplication` +
  `AsioEventDispatcher` (свой цикл событий на asio) и прогоняют тесты модуля.
- Автор сам провёл границу библиотек: `aspia_client_core`, `aspia_host_core`, `aspia_router_core`,
  `aspia_relay_core` - статические библиотеки, exe тонкие (main/application).
- Размеры на 3.4.3: транспорт (`base/net`+`peer`+`crypto`+`threading`+`ipc`) 134 файла / 27 345 строк;
  клиент всего 96 732 (UI 50 332 - 52 %, из них `client/desktop` 45 383 и `client/android` 15 542);
  хост всего 74 647 (UI 9 283 - 13 %); `base` 87 334, из кодека 8 075. Подробная таблица - в
  `ENGINE-DLL-SPLIT.md`.
- Три отдельных DLL со статическим Qt невозможны: в процессе окажется несколько копий QtCore
  (свой `QCoreApplication::self`, свой `QThreadData`), межмодульные `QObject` ломаются. Либо
  динамический Qt (`x64-windows`, `/MD`, `Qt6Core.dll`) в отдельном preset, либо одна engine-DLL.
- Win7 в целях (`_WIN32_WINNT=0x0601`) - значит обвязка на .NET Framework 4.8, интероп-проект
  мультитаргетный (`net48;net10.0-windows`).
- Хост: UI уже отдельный процесс на канале `org.aspia.host.ui` (protobuf, именованный канал) -
  .NET-трею C++-DLL не нужна вообще.
- Первый шаг - этап 0 (полдня): DLL с `QCoreApplication` + `AsioEventDispatcher`, вызов из .NET 4.8.
  Он снимает единственное настоящее "неизвестно" (живёт ли Qt-цикл внутри DLL под .NET).

Планируемое разделение:

- **Транспортный движок** - `source/base/net`, `source/base/peer`, `source/base/crypto`,
  `source/proto`. Соединение, шифрование, протокол сообщений, NAT traversal.
- **Движок хоста** - `source/host`: захват экрана, инъекция ввода, файлы, терминал, чат,
  аудио, системная информация. Сейчас содержит и Qt-UI хоста, его нужно отделить.
- **Движок клиента** - `source/client`: установление сессии, прием видео и аудио, ввод,
  файловые операции. Сейчас UI на Qt-виджетах - именно он заменяется обвязкой на .NET.

Цель разделения: C++-библиотеки с C-совместимым или плоским C++ ABI (экспорт через DLL,
без Qt-типов в публичном интерфейсе), поверх них - обвязка на .NET.

Выбор платформы обвязки: .NET 10 или .NET Framework 4.8. Аргумент в пользу 4.8 - он уже
присутствует в Windows и не требует установки рантайма на целевые компьютеры.

Открытые вопросы к проработке в новой сессии:

- где проходит граница между Qt-виджетами и логикой в `source/client` и `source/host`,
  что придется вырезать или обернуть
- как отдавать кадры и ввод через границу DLL: callback-и, кольцевые буферы или общая память
- нужен ли protobuf на стороне .NET (интероп) или достаточно своих структур
- как быть с зависимостями библиотек от Qt Core (сигналы, цикл событий) при экспорте наружу
- порядок работ: сначала транспорт (минимум зависимостей), затем хост, затем клиент
