# Вынос движков в C++-библиотеки и обвязка на .NET - разбор

Разбор задачи «тяжёлые движки один раз компилируем в DLL с плоским ABI, обвязку пишем на .NET».
Все цифры сняты с дерева `D:\.VS\Projects\aspia-extreem`, ветка `extreme`, **версия 3.4.3**
(синхронизирована с апстримом 18.11.2026, см. раздел 0).

## 0. Что дала синхронизация с апстримом

Форк отставал на 993 коммита (3.0.0 -> 3.4.3). Три изменения прямо влияют на этот разбор:

1. **Автор сам выделил ядровые библиотеки.** В `client/CMakeLists.txt` и `host/CMakeLists.txt`
   появились статические `aspia_client_core` и `aspia_host_core` (а также `aspia_router_core`,
   `aspia_relay_core`, `aspia_common`). Exe стали тонкими: в `aspia_client.exe` остались
   `main`/`application`/окно, вся начинка - в core-библиотеке. То есть граница, которую мы
   собирались искать, уже проведена - но по линии «библиотека против exe», а не «движок против UI»:
   в `aspia_client_core` лежат и движок, и виджеты вкладок.
2. **openh264 выпилен.** В дереве только `video_encoder_vpx` / `video_decoder_vpx` (VP8/VP9 на
   libvpx), пакета openh264 в `vcpkg.json` больше нет. Кодек-часть движка стала меньше и однороднее.
3. **Android.** Появились `source/client/android` (15 542 строки) и `host/android`. Qt-клиент теперь
   кроссплатформенный. Для .NET-обвязки это означает: Android остаётся на Qt, .NET - только Windows.

Плюс мелочи, важные для стыка: свой INI-парсер `base/ini_file.{cc,h}` вместо `inipp`, новый
`client/storage/` (адресная книга разбита на `database_local_hosts`, `database_profiles`,
`database_credentials`, `database_router`, `database_settings`, `master_password`, `backup`),
`client/mcp` (MCP-сервер с tools), `base/update`, тесты на каждый модуль.

## 1. Главное: движок уже отделён от UI слоем воркеров

Граница «движок / UI» проведена по сигналам `QObject`. Движок - это `Worker`-классы
(`base/threading/worker.h`): каждый живёт в своём потоке, получает и отдаёт `QByteArray`
и `SharedFrame`, виджетов внутри нет. В клиенте это `client/workers` (13 файлов / 2 775 строк),
в хосте - `host/workers` (32 файла / 10 524 строки).

| Слой | Где | Файлов | Строк |
|---|---|---:|---:|
| Транспорт (сеть, пиры, крипта, потоки, IPC) | `base/net`, `base/peer`, `base/crypto`, `base/threading`, `base/ipc` | 134 | 27 345 |
| Протокол | `proto/*.proto` (33 схемы, `LITE_RUNTIME`) | 33 | 4 528 |
| База всего | `base/` (в т.ч. `codec` 8 075, `win` 5 578, `desktop` 3 767, `update` 2 381, `sql` 1 372) | 401 | 87 334 |
| Клиент всего | `client/` | 394 | 96 732 |
| - виджеты и модели | `client/desktop/*` | 166 | 45 383 |
| - Android UI | `client/android/*` | 68 | 15 542 |
| - хранилище (БД, настройки, бэкапы) | `client/storage/*` | 29 | 12 350 |
| - MCP-сервер | `client/mcp/*` | 43 | 6 086 |
| - маршрутизатор | `client/router/*` | 18 | 5 024 |
| - воркеры (движок) | `client/workers/*` | 13 | 2 775 |
| Хост всего | `host/` | 323 | 74 647 |
| - UI хоста | `host/ui/*` | 24 | 5 494 |
| - воркеры | `host/workers/*` | 32 | 10 524 |
| - Windows-часть (DXGI, инъекция, D3D) | `host/win/*` | 46 | 7 323 |
| Общая UI-обвязка | `common/` (диалоги, ресурсы, переводы) | 205 | 31 975 |

Проверка по маркерам виджетов (`QWidget|QDialog|QVBoxLayout|QTreeWidget|QFileDialog|…`):

- клиент: 183 файла, 50 332 строки из 96 732 (**52 %**; из них Android-интерфейс - 15 542)
- хост: 39 файлов, 9 283 строки из 74 647 (**13 %**)
- `base`: ровно один файл - `base/gui_application.cc` (1 044 строки)
- `common`: 136 файлов, 21 877 строк - общие Qt-диалоги, при переходе на .NET не нужны

Транспорт и база - одна самостоятельная библиотека: `aspia_base` не знает про `host` и `client`
и линкуется всеми модулями.

**Вывод:** проект не надо архитектить заново. Поверх слоя воркеров ставится C-фасад, а модулями
сборки становятся уже существующие core-библиотеки.

## 2. Что реально тянет движок (и что можно не тащить)

Из `source/CMakeLists.txt`: `QT_COMMON_LIBS` = Core, Gui, Network, Widgets, Svg, Xml + приватные
плагины; `THIRD_PARTY_LIBS` = curl, protobuf-lite, zstd, OpenSSL (SSL+Crypto), Opus, libwebm,
libvpx, enet, miniupnpc, nayuki-qr, sqlite3, vterm, yuv (openh264 больше нет).

Фактическое потребление по коду, а не по линковке:

| Зависимость | Где реально нужна |
|---|---|
| **QtCore** (QObject, QByteArray, QString, QThread, QTimer, QRect/QSize) | везде, включая транспорт |
| QtGui (QImage/QPainter) | только `base/gui_application.cc` + иконки в нескольких воркерах |
| QtWidgets | только UI-слои |
| Qt6::Test | только тестовые бинарники |
| QtNetwork | **нигде** - сеть на asio |
| QtSql | **нигде** - `base/sql` работает с `unofficial::sqlite3` напрямую |
| OpenSSL | SRP-аутентификация, AES-GCM, Argon2id |
| protobuf-lite | протокол (`base/serialization.h`, `Parser<>`) |
| libvpx / opus / libyuv / libwebm | кодирование в хосте, декодирование в клиенте |
| asio | сеть + **свой цикл событий** (`base/threading/asio_event_dispatcher.cc` - реализация `QAbstractEventDispatcher` на asio) |

Ключевой факт для .NET: движок уже умеет жить **без GUI-приложения**. Все тестовые бинарники
(`aspia_base_tests`, `aspia_client_tests`, `aspia_host_tests`, `aspia_common_tests`,
`aspia_relay_tests`, `aspia_router_tests`) поднимают `QCoreApplication` + `AsioEventDispatcher`
и прогоняют тесты модуля. `QApplication` и виджеты движку не нужны - нужен только
`QCoreApplication` (синглтон на процесс) и его диспетчер событий.

## 3. Три DLL или одна - упирается в статический Qt

Сборка: триплет `x64-windows-static`, `CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded` (/MT),
Qt линкуется статически в каждый exe (`aspia_client.exe` на 3.0.0 весил 36,9 МБ).

Следствия, которые определяют архитектуру:

1. **Статический Qt нельзя размножать по DLL.** Каждая DLL со статическим QtCore получает свою
   копию глобалов: свой `QCoreApplication::self`, свой реестр `QThreadData`, свои `qt_metacall`.
   Объект из модуля A не увидит диспетчер событий, установленный в модуле B, - `QueuedConnection`
   между модулями перестаёт работать, а `QCoreApplication::sendPostedEvents()` в модуле без своего
   инстанса разыменует `nullptr`. Это не «неаккуратно», это неопределённое поведение.
2. **/MT - свой CRT на модуль.** Память, выделенная в DLL, не может быть освобождена в .NET
   и наоборот. Значит в ABI обязателен принцип «кто выделил - тот и освобождает».
3. Межмодульная передача `std::string` / `QByteArray` / `shared_ptr` по границе невозможна.

Отсюда три честных варианта:

| Вариант | Что делаем | Цена | Что получаем |
|---|---|---|---|
| **A. Одна engine-DLL** | `aspia_engine.dll` = транспорт + движок клиента + движок хоста, статический Qt внутри, наружу C-ABI | 0 изменений в сборке, ~40 МБ DLL, нет установки рантаймов | работает уже сегодня, но «три библиотеки» - нет |
| **B. Три DLL + динамический Qt** | новый preset на триплет `x64-windows` (`/MD`, Qt6Core.dll рядом), `aspia_core.dll` / `aspia_host.dll` / `aspia_client.dll` | переустановка зависимостей под новый триплет (~1 час), +Qt6Core.dll (~7 МБ) в поставке, две конфигурации сборки | именно то, что просили: три библиотеки, один общий Qt, межмодульные QObject живы |
| **C. Три DLL без Qt вообще** | переписать `Worker` / сигналы на asio+std, выпилить QtCore из движка | переписать 4,2 тыс. строк threading и связи всех воркеров, потерять готовые тесты | «чистый» C++, но переписывание того, что работает |

Рекомендация: **B**, если три отдельных DLL принципиальны; иначе **A** - она даёт всю выгоду
«компилируем один раз, обвязку пишем на .NET» за один вечер работы по сборке.

## 4. Где проходит шов и что по нему ходит

Воркеры уже разговаривают ровно тем, что удобно гонять через ABI:

- **Транспорт**: 13 каналов `CHANNEL_ID_*` (0 legacy … 12 tools) как `(uint8 channel_id, bytes)` +
  статусы (`NetworkWorker::Status`: STARTED / HOST_CONNECTING / HOST_CONNECTED / … / RELAY_ERROR) +
  метрики (байты, скорости, метод UDP) + коды ошибок `TcpChannel::ErrorCode` (14 значений).
- **Видео**: `SharedFrame` - буфер BGRA (`QImage::Format_RGB32`), размер и `stride`. В UI кадра
  передаётся указателем и живёт только на время колбэка.
- **Курсор**: `MouseCursor` - BGRA + hotspot + размер + DPI.
- **Звук**: PCM из opus-декодера.
- **Служебное**: буфер обмена, чат, терминал (vterm), системная информация, диспетчер задач -
  это protobuf-сообщения внутри C++, наружу должны выйти уже типизированными событиями.

**Protobuf на сторону .NET выносить не нужно** для основного сценария: протокол остаётся внутри
движка, .NET видит типизированные события. Но `protoc.exe` в дереве есть
(`builds/local-win-x64/vcpkg_installed/x64-windows-static/tools/protobuf/protoc.exe`), поэтому
C#-модели из тех же `.proto` можно генерировать там, где выгодно: импорт/экспорт адресной книги
(`address_book.proto`), протокол IPC хоста.

## 5. Контракт ABI

Правила, без которых интероп с .NET начинает падать невоспроизводимо:

1. Только C99 в публичном заголовке: `int32_t`, `uint8_t*`, `const char*` (UTF-8), `void*`.
   Ни `std::`, ни Qt, ни исключений через границу.
2. Непрозрачные хэндлы (`typedef struct aspia_session aspia_session;`) - вся память внутри DLL.
3. Каждая публичная структура начинается с `uint32_t struct_size` - версионирование без пересборки.
4. Колбэки - таблица указателей + `void* user` (в .NET это `GCHandle`), передаётся при создании.
5. Владение: кадры и буферы валидны **только внутри колбэка**; строки, которые DLL отдаёт дольше,
   освобождаются её же функцией `aspia_free()`. .NET никогда не вызывает `Marshal.FreeHGlobal`
   на памяти движка.
6. Коды ошибок - `int32_t` с теми же значениями, что `TcpChannel::ErrorCode` (публикуем как enum
   в заголовке, чтобы .NET и C++ не разъезжались).
7. Потоки: колбэки приходят из потоков движка. Документируем это в заголовке и в .NET сразу
   маршалим в UI-поток (`Dispatcher.BeginInvoke`), иначе получим падения WPF «cross-thread».
8. `aspia_init()` вызывается один раз из **главного потока процесса** и создаёт `QCoreApplication`
   + `AsioEventDispatcher`. Это единственное настоящее «неизвестно» - требует PoC (этап 0).

Скелет (сокращённо, реальный файл - предмет этапа 1):

```c
// aspia_client_api.h - C99, без Qt
#define ASPIA_API __declspec(dllimport)
#define ASPIA_ABI_VERSION 1u

typedef struct aspia_session aspia_session;

typedef struct {           // BGRA, валиден только внутри колбэка
    uint32_t struct_size;
    const uint8_t* data;
    int32_t  width, height, stride;
    const int32_t* dirty_rects;   // x, y, w, h подряд
    uint32_t dirty_rect_count;
} aspia_frame;

typedef struct {           // параметры сессии: то, что сегодня собирает SessionState
    uint32_t struct_size;
    const char* host_address;   // UTF-8, "\0"-терминированный
    uint16_t host_port;
    const char* router_address;
    uint16_t router_port;
    const char* user_name;
    const char* password;
    const char* display_name;
    int32_t  session_type;      // proto::peer::SessionType
    int32_t  udp_methods;       // битовая маска
    int32_t  flags;             // авто-переподключение и пр.
} aspia_session_params;

typedef struct {
    uint32_t struct_size;
    void* user;
    void (*on_state)  (void* user, int32_t state, int32_t error_code);
    void (*on_frame)  (void* user, const aspia_frame* frame);
    void (*on_cursor) (void* user, const uint8_t* bgra, int32_t w, int32_t h,
                       int32_t hx, int32_t hy, int32_t dpi_x, int32_t dpi_y);
    void (*on_audio)  (void* user, const int16_t* pcm, uint32_t frames,
                       uint32_t sample_rate, uint32_t channels);
    void (*on_clipboard)(void* user, int32_t type, const char* utf8, uint32_t length);
    void (*on_metrics)(void* user, const aspia_metrics* metrics);
} aspia_listener;

ASPIA_API int32_t aspia_init(const wchar_t* log_dir, uint32_t flags);
ASPIA_API void    aspia_shutdown(void);
ASPIA_API aspia_session* aspia_session_create(const aspia_session_params*,
                                              const aspia_listener*);
ASPIA_API void    aspia_session_start(aspia_session*);
ASPIA_API void    aspia_session_stop(aspia_session*);
ASPIA_API void    aspia_session_destroy(aspia_session*);
ASPIA_API void    aspia_session_send_input(aspia_session*, const uint8_t* proto, uint32_t len);
ASPIA_API void    aspia_free(void* p);
```

Файлы, буфер обмена, чат, терминал, диспетчер задач добавляются такими же группами функций -
у каждой свой колбэк и свои `*_request` / `*_cancel` / `*_reply`.

## 6. Обвязка на .NET: 4.8 или 10

Факт из сборки: `-D_WIN32_WINNT=0x0601` - проект целится в **Windows 7**. Это и решает вопрос:

| | .NET Framework 4.8 | .NET 10 |
|---|---|---|
| Наличие на целевой машине | есть в Windows 7 SP1+ / 10 / 11 | нужен рантайм (Win10 1607+), либо self-contained ~70 МБ |
| Windows 7 | да | нет |
| P/Invoke | `DllImport` | `DllImport` + source-gen `LibraryImport` |
| Современный C#, `Span<T>` | с оговорками (LangVersion, System.Memory) | из коробки |
| Скорость старта и работы | ниже | существенно выше |

Практичный ответ: **интероп-библиотека с двумя целями** (`net48;net10.0-windows`), один и тот же
код P/Invoke, два UI-шелла. Решение «какой UI поставляем» принимается не сейчас, а по парку машин.
Для 4.8 никакой установки не нужно - это ровно тот аргумент, который был в задаче.

Что переезжает в .NET и на чём это делается:

| Куска обвязки | Как в .NET | Подводные камни |
|---|---|---|
| Настройки | INI (`client/storage/instance_settings`) или JSON - формат простой | держать совместимость со старым файлом, иначе две версии не поделят настройки |
| Адресная книга | тот же файл SQLite, `Microsoft.Data.Sqlite` | схема разбита по файлам `database_local_hosts` / `database_profiles` / `database_credentials` / `database_router` / `database_settings`; BLOB-поля - не protobuf, а DPAPI-блоки |
| Пароли | `ProtectedData` (DPAPI, **без entropy** - проверено в `os_crypt_win.cc`) | читается и пишется из .NET один в один |
| Мастер-пароль | Argon2id: память 128 МиБ, итераций 4, lanes 1, hash 32 + AES-256-GCM | в .NET Argon2id только пакетом (Konscious.Security.Cryptography), параметры должны совпасть бит в бит |
| Бэкапы адресной книги | `client/storage/backup.cc` - свой формат на protobuf + Argon2id | либо повторять формат в C#, либо отдать бэкап движку |
| Переводы | 24 языка в `source/translations/*.ts` | `.ts` придётся конвертировать (.resx или свой формат); механическая, но объёмная работа |
| Видео | WPF `WriteableBitmap` с `PixelFormats.Bgra32` (stride совпадает с кадром) или `D3DImage` для 4К/60 fps | кадр 1920x1080x4 = 8 МБ, на 30 fps это ~240 МБ/с копирования - терпимо, но в 4К лучше общая D3D11-текстура |

## 7. Хост - особый случай: там DLL может не понадобиться

Хост уже разделён на процессы: служба (`aspia_host.exe --service`) - движок, агенты в сессиях
пользователя (`--agent desktop|file|terminal`), UI хоста - отдельный процесс, который общается со
службой **по именованному каналу** `org.aspia.host.ui` (`host/host_constants.cc`), протокол -
protobuf, транспорт - `asio::windows::stream_handle` (`base/ipc/ipc_channel.cc`).

То есть .NET-приложению в трее **не нужна C++-DLL вообще**: `NamedPipeClientStream` + C#-модели из
`proto/control*` и всё. UI хоста - 5 494 строки в `host/ui`, это самое дешёвое место, чтобы начать
получать выгоду от .NET, ничего не ломая в движке.

Движок хоста при этом остаётся процессом (служба SYSTEM + агенты в сессиях) - это не «недоделка»,
а требование безопасности: инъекция ввода и захват экрана живут там, где живёт сессия.

Отдельно: `host/credentials` собирается как `aspia_credentials.dll` - это COM credential provider
для входа в систему через Aspia. К ABI движка отношения не имеет, но живёт в этом же дереве.

## 8. Клиент - вот тут переписывание и происходит

Клиент монолитный: сессия, декодирование видео, файлы, звук, MCP-сервер и UI в одном процессе.
В 3.4.3 половина кода - виджеты и модели (`client/desktop`, 166 файлов / 45 383 строки), плюс
Android-интерфейс (15 542), который на .NET не переписывается вообще. Схема после разреза:

```
aspia_core.dll (транспорт, QtCore внутри)  <-  aspia_client.dll (сессия, видео, аудио, файлы, MCP)
                                                        ^ C-ABI
                                              Aspia.Client.Interop (net48;net10)
                                                        ^
                                              Aspia.Client (WPF): вкладки, адресная книга,
                                              настройки, файловый менеджер, чат, терминал
```

## 9. Риски, которые надо снять опытом, а не рассуждением

1. **`QCoreApplication` внутри DLL, загруженной из .NET.** Qt требует создания приложения в главном
   потоке; .NET-поток главный для процесса - но проверить надо руками (таймеры, `postEvent` между
   потоками воркеров, чистое завершение). Это этап 0 и он стоит полдня.
2. **Статический Qt в нескольких DLL** - см. п. 3. Если делать три DLL, то только с динамическим Qt.
3. **`/MT` и освобождение памяти через границу** - либо строгое правило «free внутри DLL», либо `/MD`.
4. **DPI awareness.** Сейчас его ставит сам движок в `main()` (`SetProcessDpiAwarenessContext`,
   Per-Monitor V2). В .NET-хосте это делает манифест/WPF; если промахнуться - координаты мыши
   и масштаб кадра разъедутся. Место стыка надо описать явно.
5. **Порядок и потоки сообщений.** UI не должен сам разбирать каналы: одно сообщение вне очереди -
   и рассинхрон с хостом. Всё через один API движка.
6. **Обновления, установщик, служба** - остаются как есть; .NET-часть добавляет второй рантайм
   в поставку (или не добавляет, если 4.8).
7. **Тесты.** Шесть тестовых бинарников остаются на C++-стороне; для C-ABI нужен отдельный
   харнесс (маленький exe или C-тест, который дёргает DLL как .NET - без Qt-типов).

## 10. План с проверяемыми вехами

| Этап | Работа | Приёмка (что должно реально работать) | Оценка |
|---|---|---|---|
| 0 | PoC: DLL с `QCoreApplication` + QtCore + `AsioEventDispatcher`, вызов из .NET 4.8 консоли | таймер движка тикает, `postEvent` между потоками доходит, выход чистый | 0.5 дня |
| 1 | `aspia_core.dll`: транспорт (`base/net`, `base/peer`, `base/crypto`, `proto`) + C-ABI | .NET-консоль подключается к `aspia_host.exe`, проходит SRP-аутентификацию, шлёт и принимает сообщение канала 0 | 2-3 дня |
| 2 | `aspia_client.dll`: сессия + видео + ввод (обёртка над `aspia_client_core`) | .NET-окно показывает удалённый экран и управляет мышью и клавиатурой | 3-5 дней |
| 3 | Остальные подсистемы клиента по одной: файлы, буфер обмена, чат, терминал, звук, системная информация | каждая - отдельная приёмка на живом хосте | 5-8 дней |
| 4 | Хост: либо .NET-трей по IPC (без DLL), либо `aspia_host.dll` | трей показывает сессии, отключает клиентов, меняет настройки | 2-4 дня |
| 5 | Обвязка: настройки, адресная книга, мастер-пароль, бэкапы, переводы | паритет с Qt-клиентом по функциям обвязки | 5-8 дней |

Итого грубо: 3-4 недели на клиент целиком, из них первая неделя - ABI и транспорт, то есть уже
после неё видно, живая ли затея.

## 11. Ответ на вопрос

Да, вынести можно, и код к этому ближе, чем кажется: движок отделён от UI слоем воркеров, транспорт
уже самостоятельная библиотека, автор сам выделил `aspia_client_core` / `aspia_host_core`,
а `QCoreApplication` без GUI движку достаточно (это доказано тестами всех модулей). Два ограничения,
которые нельзя обойти рассуждением:

1. **Qt должен быть в процессе один.** Либо одна engine-DLL со статическим Qt (быстро, работает
   сегодня), либо три DLL, но с динамическим Qt и `/MD` (переустановка зависимостей, +Qt6Core.dll).
2. **Windows 7 в целях** (`_WIN32_WINNT=0x0601`) -> .NET Framework 4.8 как основной вариант обвязки;
   .NET 10 - если парк машин это позволяет и есть чем ставить рантайм.

Первый шаг - этап 0 (полдня): DLL + `QCoreApplication` + вызов из .NET. Он снимает единственное
настоящее «неизвестно», и после него решение о трёх DLL принимается по факту, а не по вере.
