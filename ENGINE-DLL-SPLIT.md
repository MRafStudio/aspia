# Вынос движков в C++-библиотеки и обвязка на .NET — разбор

Разбор задачи «тяжёлые движки один раз компилируем в DLL с плоским ABI, обвязку пишем на .NET».
Все цифры сняты с дерева `D:\.VS\Projects\aspia-extreem`, ветка `extreme`, на момент разбора.

## 1. Главное: резать почти нечего, движок уже отделён

Граница «движок / UI» в этом коде уже проведена руками автора, и проведена по правильному месту —
по сигналам `QObject`. Движок — это `Worker`-классы (`base/threading/worker.h`): каждый живёт в своём
потоке, получает/отдаёт `QByteArray` и `SharedFrame`, никаких виджетов внутри нет.

| Слой | Где | Файлов | Строк |
|---|---|---:|---:|
| Транспорт (сеть, пиры, крипта, потоки, IPC) | `base/net`, `base/peer`, `base/crypto`, `base/threading`, `base/ipc` | 136 | 27 217 |
| Протокол | `proto/*.proto` (33 схемы, `LITE_RUNTIME`) | 35 | 128 (только зерно) |
| Движок клиента | всё `client/` без виджетов: `workers/`, `session_state`, `file_transfer`, `audio_*`, `router`, `database`, `config` | 85 | 17 188 |
| UI клиента | 143 файла с виджетами, из них `client/desktop/*` — 107 файлов / 28 557 строк | 143 | 38 181 |
| Движок хоста | всё `host/` без `host/ui` (клиенты, агенты, захват, инъекция, воркеры) | 224 | 50 209 |
| UI хоста | `host/ui/*` | 24 | 7 327 |
| Общая UI-обвязка | `common/` (диалоги, ресурсы, переводы) | 190 | 28 232 |

Проверка на виджеты (`QWidget|QDialog|QVBoxLayout|QTreeWidget|QFileDialog|…`):

- клиент: 143 файла, 38 181 строка из 55 369 (**69 %** — UI, его и переписываем на .NET)
- хост: 34 файла, 7 327 строк из 55 196 (**13 %** — UI)
- `base`: ровно два файла — `base/gui_application.{h,cc}` (837 строк). Всё остальное от виджетов не зависит.

Транспортный движок вообще уже самостоятельная единица: `aspia_base` — статическая библиотека, которая
не знает про `host` и `client` и линкуется всеми четырьмя exe (`client`, `host`, `router`, `relay`).

**Вывод:** проект не надо архитектить заново. Надо поверх существующего слоя воркеров поставить
C-фасад и вынести `source/client`, `source/host`, `source/base` в отдельные модули сборки.

## 2. Что реально тянет движок (и что можно не тащить)

Из `CMakeLists.txt` верхнего уровня: `QT_COMMON_LIBS` = Core, Gui, Network, Widgets, Svg, Xml +
приватные плагины, `THIRD_PARTY_LIBS` = curl, protobuf-lite, zstd, OpenSSL(SSL+Crypto), Opus,
openh264, libwebm, libvpx, enet, miniupnpc, nayuki-qr, sqlite3, vterm, yuv.

Фактическое потребление по коду, а не по линковке:

| Зависимость | Где реально нужна |
|---|---|
| **QtCore** (QObject, QByteArray, QString, QThread, QTimer, QRect/QSize) | везде, включая транспорт — 47 файлов на `QByteArray`, 24 на `QObject` |
| QtGui (QImage/QPainter) | только `base/gui_application.cc` + `host/workers/tools_worker_win.cc` (иконки) |
| QtWidgets | только UI-слои |
| QtNetwork | **нигде** (в движке только `QTcpSocket` в юнит-тесте диспетчера) — сеть на asio |
| QtSql | **нигде** — `base/sql` работает с `unofficial::sqlite3` напрямую |
| OpenSSL | SRP-аутентификация, AES-GCM, Argon2id |
| protobuf-lite | протокол (`base/serialization.h`, `Parser<>`) |
| libvpx / openh264 / opus / libyuv / libwebm | только движки хоста (кодирование) и клиента (декодирование) |
| asio | сеть + **свой цикл событий** (`base/threading/asio_event_dispatcher.cc` — реализация `QAbstractEventDispatcher` на asio) |

Ключевой факт для .NET: движок уже умеет жить **без GUI-приложения**. `base/tests_main.cc` поднимает
`QCoreApplication` + `AsioEventDispatcher` и гоняет 626 тестов, включая воркеры. То есть `QApplication`
и любой Qt-виджет движку не нужны — нужен только `QCoreApplication` (синглтон на процесс) и его
диспетчер событий.

## 3. Три DLL или одна — упирается в статический Qt

Сейчас сборка: триплет `x64-windows-static`, `CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded` (/MT),
Qt линкуется статически в каждый exe (`aspia_client.exe` — 36,9 МБ, `aspia_host.exe` — 36,3 МБ).

Следствия, которые определяют архитектуру:

1. **Статический Qt нельзя размножать по DLL.** Каждая DLL со статическим QtCore получает свою копию
   глобалов: свой `QCoreApplication::self`, свой реестр `QThreadData`, свои `qt_metacall`. Объект,
   созданный в модуле A, не увидит диспетчер событий, установленный в модуле B, — `QueuedConnection`
   между модулями перестаёт работать, а `QCoreApplication::sendPostedEvents()` в модуле без своего
   инстанса просто разыменует `nullptr`. Это не «неаккуратно», это неопределённое поведение.
2. **/MT — свой CRT на модуль.** Память, выделенная в DLL, не может быть освобождена в .NET и наоборот.
   Значит в ABI обязателен «кто выделил — тот и освобождает» (функция `free` внутри DLL).
3. Межмодульная передача `std::string`/`QByteArray`/`shared_ptr` по границе невозможна тем более.

Отсюда три честных варианта:

| Вариант | Что делаем | Цена | Что получаем |
|---|---|---|---|
| **A. Одна engine-DLL** | `aspia_engine.dll` = транспорт + движок клиента + движок хоста, статический Qt внутри, наружу C-ABI | 0 изменений в сборке, ~40 МБ DLL, нет установки рантаймов | всё работает уже сегодня, но «три библиотеки» — нет |
| **B. Три DLL + динамический Qt** | новый preset на триплет `x64-windows` (`/MD`, Qt6Core.dll рядом), `aspia_core.dll` / `aspia_host.dll` / `aspia_client.dll` | переустановка зависимостей под новый триплет (~1 час), +Qt6Core.dll (~7 МБ) в поставке, две конфигурации сборки (старая GUI-сборка остаётся на static) | именно то, что просили: три библиотеки, один общий Qt, межмодульные QObject живы |
| **C. Три DLL без Qt вообще** | переписать `Worker`/сигналы на asio+std, выпилить QtCore из движка | самый дорогой путь: переписать 3,5 тыс. строк threading и все связи воркеров, потерять наработанные тесты | «чистый» C++, но переписывание того, что работает |

Рекомендация: **B**, если три отдельных DLL принципиальны; иначе **A** — она даёт всю выгоду
«компилируем один раз, обвязку пишем на .NET» за один вечер работы по сборке.

## 4. Где проходит шов и что по нему ходит

Ничего изобретать не нужно — воркеры уже разговаривают ровно тем, что удобно гонять через ABI:

- **Транспорт**: 13 каналов `CHANNEL_ID_*` (0 legacy … 12 tools) как `(uint8 channel_id, bytes)` +
  статусы (`NetworkWorker::Status`: STARTED / HOST_CONNECTING / HOST_CONNECTED / … / RELAY_ERROR) +
  метрики (байты, скорости, метод UDP) + коды ошибок `TcpChannel::ErrorCode` (14 значений).
- **Видео**: `SharedFrame` — буфер BGRA (`QImage::Format_RGB32`), размер и `stride`. В UI кадра
  передаётся указателем, живёт только на время колбэка.
- **Курсор**: `MouseCursor` — BGRA + hotspot + размер + DPI.
- **Звук**: PCM из opus-декодера.
- **Служебное**: буфер обмена (текст/файлы), чат, терминал (vterm), системная информация, диспетчер
  задач — всё это protobuf-сообщения внутри C++, наружу должны выйти уже типизированными событиями.

**Protobuf на сторону .NET выносить не нужно** для основного сценария: протокол остаётся внутри
движка, .NET видит типизированные события. Но `protoc.exe` в дереве есть
(`builds/local-win-x64/vcpkg_installed/x64-windows-static/tools/protobuf/protoc.exe`), поэтому C#-модели
из тех же `.proto` можно сгенерировать там, где это выгодно: импорт/экспорт адресной книги
(`address_book.proto`), будущий IPC-протокол хоста.

## 5. Контракт ABI

Правила, без которых интероп с .NET начинает падать невоспроизводимо:

1. Только C99 в публичном заголовке: `int32_t`, `uint8_t*`, `const char*` (UTF-8), `void*`. Ни `std::`,
   ни Qt, ни `bool`, ни исключений через границу.
2. Непрозрачные хэндлы (`typedef struct aspia_session aspia_session;`) — вся память внутри DLL.
3. Каждая публичная структура начинается с `uint32_t struct_size` — версионирование без пересборки.
4. Колбэки — таблица указателей + `void* user` (в .NET это `GCHandle`), передаётся при создании.
5. Владение: кадры и буферы валидны **только внутри колбэка**; строки, которые DLL отдаёт «наружу»
   дольше, освобождаются её же функцией `aspia_free()`. .NET никогда не вызывает `Marshal.FreeHGlobal`
   на памяти движка.
6. Коды ошибок — `int32_t` с теми же значениями, что `TcpChannel::ErrorCode` (публикуем как enum в
   заголовке, чтобы .NET и C++ не разъезжались).
7. Потоки: колбэки приходят из потоков движка. Документируем это в заголовке и в .NET сразу
   маршалим в UI-поток (`Dispatcher.BeginInvoke`), иначе получим падения WPF «cross-thread».
8. `aspia_init()` вызывается один раз из **главного потока процесса** и создаёт `QCoreApplication`
   + `AsioEventDispatcher`. Это единственное настоящее «неизвестно» — требует PoC (этап 0).

Скелет (сокращённо, реальный файл — предмет этапа 1):

```c
// aspia_client_api.h — C99, без Qt
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

Файлы, буфер обмена, чат, терминал, диспетчер задач добавляются такими же группами функций —
у каждой свой колбэк и свои `*_request/*_cancel/*_reply`.

## 6. Обвязка на .NET: 4.8 или 10

Факт из сборки: `-D_WIN32_WINNT=0x0601` — проект целится в **Windows 7**. Это и решает вопрос:

| | .NET Framework 4.8 | .NET 10 |
|---|---|---|
| Наличие на целевой машине | есть в Windows 7 SP1+ / 10 / 11 | нужен рантайм (Win10 1607+), либо self-contained ~70 МБ |
| Windows 7 | да | нет |
| P/Invoke | `DllImport` | `DllImport` + source-gen `LibraryImport` |
| Современный C#, `Span<T>` | с оговорками (LangVersion, System.Memory) | из коробки |
| Скорость старта/работы | ниже | существенно выше |

Практичный ответ: **интероп-библиотека с двумя целями** (`net48;net10.0-windows`), один и тот же
код P/Invoke, два UI-шелла. Решение «какой UI поставляем» принимается не сейчас, а по парку машин.
Для 4.8 никакой установки не нужно — это ровно тот аргумент, который был в задаче.

Что переезжает в .NET и на чём это делается:

| Куска обвязки | Как в .NET | Подводные камни |
|---|---|---|
| Настройки | обычные ini/JSON/`app.config` | текущий формат держать совместимым, чтобы старая и новая версии читали друг друга |
| Адресная книга | тот же файл SQLite (`groups`, `hosts`, `routers`, `settings`), `Microsoft.Data.Sqlite` | схема простая, BLOB-поля — уже не protobuf, а DPAPI-блоки |
| Пароли | `ProtectedData` (DPAPI, **без entropy** — проверено в `os_crypt_win.cc`) | читается/пишется из .NET один в один |
| Мастер-пароль | Argon2id: память 128 МиБ, итераций 4, lanes 1, hash 32 + AES-256-GCM | в .NET Argon2id только пакетом (Konscious.Security.Cryptography), параметры должны совпасть бит в бит |
| Переводы | 24 языка в `source/translations/*.ts` | `.ts` придётся конвертировать (.resx или свой формат); это механическая, но объёмная работа |
| Видео | WPF `WriteableBitmap` с `PixelFormats.Bgra32` (stride совпадает с кадром) или `D3DImage` для 4К/60 fps | кадр 1920×1080×4 = 8 МБ, на 30 fps это ~240 МБ/с копирования — терпимо, но в 4К лучше общая D3D11-текстура |

## 7. Хост — особый случай: там DLL может не понадобиться

Хост уже разделён на процессы: служба (`aspia_host.exe --service`) — движок, агенты в сессиях
пользователя (`--agent desktop|file|terminal`), UI хоста — отдельный процесс, который общается со
службой **по именованному каналу** `org.aspia.host.ui` (`host/host_constants.cc`), протокол —
protobuf, транспорт — `asio::windows::stream_handle` (`base/ipc/ipc_channel.cc`).

То есть .NET-приложению в трее **не нужна C++-DLL вообще**: `NamedPipeClientStream` + C#-модели из
`proto/control*` и всё. UI хоста — 7 327 строк, это самое дешёвое место, чтобы начать получать выгоду
от .NET, ничего не ломая в движке.

Движок хоста при этом остаётся процессом (служба SYSTEM + агенты в сессиях) — это не «недоделка»,
а требование безопасности: инъекция ввода и захват экрана живут там, где живёт сессия.

## 8. Клиент — вот тут переписывание и происходит

Клиент монолитный: сессия, декодирование видео, файлы, звук и UI в одном процессе. 69 % кода —
виджеты. Схема после разреза:

```
aspia_net_test.dll (PoC) → aspia_core.dll (транспорт) ← aspia_client.dll (сессия, видео, аудио, файлы)
                                                        ↑ C-ABI
                                              Aspia.Client.Interop (net48;net10)
                                                        ↑
                                              Aspia.Client (WPF): вкладки, адресная книга,
                                              настройки, файловый менеджер, чат, терминал
```

## 9. Риски, которые надо снять опытом, а не рассуждением

1. **`QCoreApplication` внутри DLL, загруженной из .NET.** Qt требует создания приложения в главном
   потоке; .NET-поток главный для процесса — но проверить надо руками (таймеры, `postEvent` между
   потоками воркеров, чистое завершение). Это этап 0 и он стоит полдня.
2. **Статический Qt в нескольких DLL** — см. п. 3. Если делать три DLL, то только с динамическим Qt.
3. **`/MT` и освобождение памяти через границу** — либо строгое правило «free внутри DLL», либо `/MD`.
4. **DPI awareness.** Сейчас его ставит сам движок в `main()` (`SetProcessDpiAwarenessContext`,
   Per-Monitor V2). В .NET-хосте это делает манифест/WPF; если промахнуться — координаты мыши и
   масштаб кадра разъедутся. Место стыка надо описать явно.
5. **Порядок и потоки сообщений.** UI не должен сам разбирать каналы: одно сообщение вне очереди —
   и рассинхрон с хостом. Всё через один API движка.
6. **Обновления/установщик/служба** — остаются как есть; .NET-часть добавляет второй рантайм в
   поставку (или не добавляет, если 4.8).
7. **Тесты.** 626 тестов на `aspia_base` остаются на C++-стороне; для C-ABI нужен отдельный
   харнесс (маленький exe или C-тест, который дёргает DLL как .NET — без Qt-типов).

## 10. План с проверяемыми вехами

| Этап | Работа | Приёмка (что должно реально работать) | Оценка |
|---|---|---|---|
| 0 | PoC: DLL с `QCoreApplication` + QtCore + `AsioEventDispatcher`, вызов из .NET 4.8 консоли | таймер движка тикает, `postEvent` между потоками доходит, выход чистый | 0.5 дня |
| 1 | `aspia_core.dll`: транспорт (`base/net`, `base/peer`, `base/crypto`, `proto`) + C-ABI | .NET-консоль подключается к `aspia_host.exe`, проходит SRP-аутентификацию, шлёт/принимает сообщение канала 0 | 2-3 дня |
| 2 | `aspia_client.dll`: сессия + видео + ввод | .NET-окно показывает удалённый экран и управляет мышью/клавиатурой | 3-5 дней |
| 3 | Остальные подсистемы клиента по одной: файлы, буфер обмена, чат, терминал, звук, системная информация | каждая — отдельная приёмка на живом хосте | 5-8 дней |
| 4 | Хост: либо .NET-трей по IPC (без DLL), либо `aspia_host.dll` | трей показывает сессии, отключает клиентов, меняет настройки | 2-4 дня |
| 5 | Обвязка: настройки, адресная книга, мастер-пароль, переводы | паритет с Qt-клиентом по функциям обвязки | 5-8 дней |

Итого грубо: 3-4 недели на клиент целиком, из них первая неделя — ABI и транспорт, то есть уже после
неё видно, живая ли затея.

## 11. Ответ на вопрос

Да, вынести можно, и код к этому ближе, чем кажется: движок уже отделён от UI слоем воркеров, транспорт
уже самостоятельная библиотека, а `QCoreApplication` без GUI движку достаточно (это доказано 626
тестами). Два ограничения, которые нельзя обойти рассуждением:

1. **Qt должен быть в процессе один.** Либо одна engine-DLL со статическим Qt (быстро, работает
   сегодня), либо три DLL, но с динамическим Qt и `/MD` (переустановка зависимостей, +Qt6Core.dll).
2. **Windows 7 в целях** (`_WIN32_WINNT=0x0601`) → .NET Framework 4.8 как основной вариант обвязки;
   .NET 10 — если парк машин это позволяет и есть чем ставить рантайм.

Первый шаг — этап 0 (полдня): DLL + `QCoreApplication` + вызов из .NET. Он снимает единственное
настоящее «неизвестно» и после него решение о трёх DLL принимается по факту, а не по вере.
