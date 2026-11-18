---
name: aspia-extreme
description: Use when building Aspia fork or porting it to .NET.
version: 1.0.0
author: MRafStudio / Hermes
license: GPL-3.0 (наследуется от Aspia)
metadata:
  hermes:
    tags: [aspia, cpp, qt6, vcpkg, dotnet, remote-desktop, build]
    related_skills: [agent-context-files, github]
---

# Aspia extreme fork — сборка и реструктуризация

## When to Use

- Нужно собрать форк Aspia (`D:\.VS\Projects\aspia-extreem`, ветка `extreme`) на этой машине.
- Падает сборка на `afxres.h`, `wudfwdm.h`, `contains symlinks` или на `qtprotoc`/`sha256`.
- Планируется вынос транспорта/хоста/клиента в C++-библиотеки с обвязкой на .NET.

Форк `MRafStudio/aspia` (GitHub), рабочая ветка `extreme`. Апстрим: `dchapyshev/aspia`.
Цель форка: эксперименты, которые автор не берёт в апстрим; текущий лейтмотив — вынести
транспорт/хост/клиент в C++-библиотеки (DLL) и сделать обвязку на .NET
(настройки, адресные книги, параметры передачи/управления) — один раз компилируем тяжёлые
C++-библиотеки, а UI/логику быстро пишем в .NET 10 или .NET Framework 4.8
(4.8 хорош тем, что уже есть в Windows и не требует установки рантайма).

## Расположение (строго)

- Проект: `D:\.VS\Projects\aspia-extreem`
- `vcpkg4aspia` — РЕАЛЬНАЯ папка внутри проекта (не junction!)
- Собранное: `builds\local-win-x64\Release\`
- Журнал форка: `HERMES.md` в корне проекта (что сделано, чем проверено, план этапа)
- Все проекты пользователя живут в `D:\.VS\Projects\<name>`; ничего не разбрасывать по
  `D:\NEURO` или `C:\Temp`; временные файлы убирать после работы

## Сборка (рабочая последовательность)

Готовый скрипт: `scripts/build-aspia.bat` (без аргументов — вся сборка; с именами таргетов —
только они; `SKIP_CONFIGURE=1` — переиспользовать кеш). Ниже — что он делает, если нужно руками:

```bat
set "PATH=C:\Program Files\CMake\bin;<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cd /d D:\.VS\Projects\aspia-extreem
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset local-win-x64
cmake --build builds\local-win-x64 --config Release -j 8
```

Хронометраж на этой машине: конфигурация 40 с (при готовом кеше vcpkg) / 19 мин (первый раз);
полная сборка с нуля 9–10 мин. Зависимости пересобирать не нужно — лежат в
`vcpkg4aspia\installed\x64-windows-static` + бинарный кеш `%LOCALAPPDATA%\vcpkg\archives`.

Артефакты: `aspia_client.exe`, `aspia_host.exe`, `aspia_router.exe`, `aspia_relay.exe`,
`aspia_virtual_display.dll`, `aspia_base_tests.exe` (626 тестов), `aspia_sha256.exe`, `qtprotoc.exe`.

## Грабли (все проверены на практике)

1. **MFC/ATL должен стоять в той VS, которую реально берёт vcpkg.** vcpkg выбирает САМУЮ НОВУЮ VS
   (здесь — каталог `18`, VS 2026, MSVC 14.51), а не ту, что в `vcvars64.bat`/`VCToolsInstallDir`.
   Симптом: `atlmfc` падает с `Unable to locate 'afxres.h'`. Проверка:
   `dir /s /b "C:\Program Files\Microsoft Visual Studio\18\**\afxres.h"`.
   В `atlmfc\include` должны быть файлы `afx*.h` (270 шт.), а не только `atl*.h`.
2. **Проект на Qt6, а не Qt5.** `vcpkg.json` требует `qtbase/qtsvg/qttools` (Qt6 6.11+),
   `CMakeLists.txt` — `find_package(Qt6 ...)`. Документация на aspia.org описывает старую
   сборку с `qt5-base` — не следовать ей. Зависимости не ставить руками — `cmake --preset`
   в manifest mode сам поставит нужное.
3. **`vcpkg4aspia` не должен быть junction/симлинком.** Qt6 падает:
   `The path "..." contains symlinks. This is not supported.` Папка должна быть реальной
   внутри проекта; обратный junction (если нужен) делать ИЗ внешнего пути В проект.
4. **Драйвер `virtual_display` требует WDK** (`wudfwdm.h`, `wdf.h`, `IddCx.h`). Без WDK CMake
   конфигурируется (FindWDK ищет лишь `avrt.h` из обычного SDK), но компиляция падает на
   `wudfwdm.h`. WDK ставится `wdksetup.exe /features + /quiet /norestart`; заголовки появятся в
   `C:\Program Files (x86)\Windows Kits\10\Include\wdf\umdf\2.27\` (драйвер просит UMDF 2.27).
5. **Нельзя запускать две ninja-сборки в одной папке одновременно** — падают на разных таргетах
   (`qtprotoc`, `sha256`) с невнятными ошибками. Всегда дожидаться завершения предыдущей.
6. **CMake — системный 4.4.3** в `C:\Program Files\CMake` (уже в системном PATH). В git-bash
   может не находиться — вызывать по полному пути или через cmd/bat.
7. Вывод `cmd.exe` в git-bash приходит в cp866 — читать логи с декодированием `cp866`, либо
   писать их в файл из bat с `chcp 65001`.

## Git и коммиты в этом репозитории

- `vcpkg4aspia/` обязана быть в `.gitignore`. Это отдельный git-репозиторий с деревом сборки
  (~90 ГБ): без записи в ignore любой `git status` обходит его целиком и виснет по таймауту без
  вывода. Признак — команда молчит минутами, а не падает.
- Прерванный по таймауту git оставляет `.git/index.lock`, и следующий `git add` падает с
  `Unable to create '.git/index.lock': File exists`. Удалить lock перед повторной попыткой.
- Терминал работает под служебной учёткой, а репозиторий принадлежит пользователю, поэтому git
  отказывается: `detected dubious ownership in repository`. Разовая настройка:
  `git config --global --add safe.directory D:/.VS/Projects/aspia-extreem`.
- Пуш по https требует токен в форме `https://USER:TOKEN@github.com/...`; при `Bad credentials`
  или `Invalid username or token` токен отозван — просить новый, локальные коммиты при этом целы.
- Журнал форка — `HERMES.md` в корне, НЕ `AGENTS.md`: в форке апстрима файл агентских правил не
  переписывают, иначе конфликты при каждом мерже. `HERMES.md` апстрим не создаёт, Hermes его
  подхватывает сам.

## Проверка результата

```
aspia_client.exe --version         # Client 3.0.0.<build>
aspia_host.exe --version           # Host 3.0.0.<build>
aspia_router.exe --create-config   # создаёт C:/ProgramData/aspia/router.conf + router.db3
aspia_relay.exe --create-config
aspia_base_tests.exe               # 626 тестов, все PASSED
```

Host при запуске без сервиса пишет `Failed to connect to the named pipe "org.aspia.host.ui"` — это
НОРМА для неустановленного хоста (UI-часть не поднята), не считать ошибкой сборки.

## Карта кода (для выделения библиотек)

- `source/base/` — транспорт и инфраструктура: `net/` (asio, сокеты, SRP, шифрование),
  `peer/`, `crypto/`, `codec/`, `desktop/`, `ipc/`, `sql/`, `threading/`
- `source/proto/` — protobuf-схемы сообщений (контракт между движками)
- `source/host/` — движок хоста (захват экрана, инъекция ввода, файлы, терминал, чат)
- `source/client/` — движок клиента (соединение, приём видео/аудио, UI-виджеты Qt)
- `source/router/`, `source/relay/` — серверная инфраструктура
- `source/common/` — общие утилиты и ресурсы
- UI на Qt (виджеты) — именно его планируется заменить обвязкой на .NET

## Правила работы с пользователем

- НИКАКИХ обходов и правок апстрим-исходников ради «чтобы собралось» (он отверг исключение atlmfc).
- Действовать напрямую, без лекций и повторных предупреждений об очевидном.
- Все артефакты и проекты — только в `D:\.VS\Projects`, мусор за собой убирать.

## Git в этом проекте

- Терминал работает от `NT AUTHORITY\СИСТЕМА`, а папка принадлежит `rafst`, поэтому любой `git`
  падает с `detected dubious ownership`. Лечится один раз:
  `git config --global --add safe.directory "D:/.VS/Projects/aspia-extreem"`.
- `vcpkg4aspia` — отдельный git-репозиторий с деревом сборки (~90 ГБ). Без строки
  `/vcpkg4aspia/` в `.gitignore` любой `git status` виснет на сканировании гигабайтов.
  Строка добавлена; если появится снова — проверить `.gitignore` первым делом.
- Если `git status` всё же оборвался по таймауту, остаётся `.git/index.lock` и следующие
  команды падают с `Unable to create index.lock`. Удалить: `rm -f .git/index.lock`.
- Пуш требует токен: `GITHUB_TOKEN` лежит в `D:\NEURO\Hermes\data\hermes\.env`.
  Токен, засвеченный в переписке, GitHub отзывает (401 Bad credentials), поэтому брать только из `.env`:
  `TOKEN=$(grep '^GITHUB_TOKEN=' /d/NEURO/Hermes/data/hermes/.env | cut -d= -f2- | tr -d '"'\''
\n ')`
  и затем `git push "https://MRafStudio:${TOKEN}@github.com/MRafStudio/aspia.git" extreme`.
  Ручной ввод токена в командной строке не нужен, а `gh auth login` требует scope `read:org`.

