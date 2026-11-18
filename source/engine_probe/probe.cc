//
// RadNET probe - этап 0. Реализация.
//
#include "engine_probe/probe.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "base/threading/asio_event_dispatcher.h"

namespace {

std::mutex g_error_mutex;
std::string g_last_error;

std::thread g_loop_thread;
std::atomic<bool> g_loop_ready{ false };
std::atomic<bool> g_loop_running{ false };
std::atomic<int32_t> g_ticks{ 0 };
std::atomic<int32_t> g_queued{ 0 };
std::atomic<int32_t> g_starts{ 0 };

void setError(const char* text)
{
    std::lock_guard<std::mutex> lock(g_error_mutex);
    g_last_error = text ? text : "";
}

// Тело потока цикла. Порядок тот же, что в tests_main.cc: диспетчер ставится до создания
// приложения, иначе QCoreApplication создаст свой и asio-таймеры работать не будут.
void loopMain(int32_t interval_ms)
{
    int argc = 1;
    char app_name[] = "aspia_probe";
    char* argv[] = { app_name, nullptr };

    QCoreApplication::setEventDispatcher(new AsioEventDispatcher());

    QCoreApplication app(argc, argv);

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, []() { g_ticks.fetch_add(1); });
    timer.start(interval_ms);

    g_loop_running.store(true);
    g_loop_ready.store(true);

    const int exit_code = app.exec();

    g_loop_running.store(false);
    (void)exit_code;
}

} // namespace

extern "C" PROBE_API int32_t probe_start(int32_t interval_ms)
{
    if (g_loop_thread.joinable())
    {
        setError("event loop is already running");
        return -1;
    }

    if (interval_ms <= 0)
        interval_ms = 10;

    g_loop_ready.store(false);
    g_ticks.store(0);
    g_queued.store(0);

    g_loop_thread = std::thread(loopMain, interval_ms);

    // Ждём готовности цикла: в .NET мы вызываем из чужого потока и не хотим гонок.
    for (int i = 0; i < 500 && !g_loop_ready.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (!g_loop_ready.load())
    {
        setError("event loop failed to start in 5 seconds");
        return -2;
    }

    g_starts.fetch_add(1);
    return 0;
}

extern "C" PROBE_API int32_t probe_post_queued(int32_t timeout_ms)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app)
    {
        setError("no event loop");
        return -1;
    }

    const int32_t before = g_queued.load();

    // Вызов ставится из отдельного потока - именно так .NET будет отдавать команды движку.
    std::thread poster([app]()
    {
        QMetaObject::invokeMethod(app, []() { g_queued.fetch_add(1); }, Qt::QueuedConnection);
    });
    poster.join();

    if (timeout_ms <= 0)
        timeout_ms = 2000;

    for (int32_t waited = 0; waited < timeout_ms; waited += 5)
    {
        if (g_queued.load() > before)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return g_queued.load();
}

extern "C" PROBE_API int32_t probe_stop(void)
{
    if (!g_loop_thread.joinable())
        return 0;

    if (QCoreApplication* app = QCoreApplication::instance())
        app->quit();

    g_loop_thread.join();
    return 0;
}

extern "C" PROBE_API void probe_state(ProbeState* out)
{
    if (!out)
        return;

    out->ticks = g_ticks.load();
    out->queued = g_queued.load();
    out->starts = g_starts.load();
    out->running = g_loop_running.load() ? 1 : 0;
}

extern "C" PROBE_API const char* probe_last_error(void)
{
    std::lock_guard<std::mutex> lock(g_error_mutex);
    return g_last_error.c_str();
}

extern "C" PROBE_API const char* probe_qt_version(void)
{
    return qVersion();
}
