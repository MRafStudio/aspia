//
// RadNET probe - этап 0.
//
// Проверяет единственное настоящее неизвестное: живёт ли цикл событий Qt внутри DLL,
// загруженной из .NET, доходят ли до него queued-вызовы из чужих потоков и корректно ли
// он останавливается (в том числе повторно).
//
// Это fork-local код в форке Aspia, к апстриму отношения не имеет.
//
#ifndef ENGINE_PROBE_PROBE_H
#define ENGINE_PROBE_PROBE_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(ASPIA_PROBE_EXPORTS)
#define PROBE_API __declspec(dllexport)
#else
#define PROBE_API __declspec(dllimport)
#endif
#else
#define PROBE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Состояние пробы. Плоская структура, без указателей - годится и для C#, и для C++.
typedef struct ProbeState
{
    int32_t ticks;    // сколько раз сработал таймер
    int32_t queued;   // сколько queued-вызовов дошло до потока цикла
    int32_t starts;   // сколько раз цикл запускался
    int32_t running;  // 1, если цикл сейчас крутится
} ProbeState;

// Запускает цикл событий в собственном потоке DLL. Возвращает 0 при успехе.
PROBE_API int32_t probe_start(int32_t interval_ms);

// Из отдельного потока ставит queued-вызов в поток цикла и ждёт его выполнения.
// Возвращает количество дошедших вызовов или -1 при ошибке.
PROBE_API int32_t probe_post_queued(int32_t timeout_ms);

// Останавливает цикл и дожидается его потока. Возвращает 0 при успехе.
PROBE_API int32_t probe_stop(void);

// Снимок счётчиков. out не должен быть NULL.
PROBE_API void probe_state(ProbeState* out);

// Текст последней ошибки (пустая строка, если ошибок не было). Указатель живёт до
// следующей ошибки - в .NET копировать сразу.
PROBE_API const char* probe_last_error(void);

// Версия Qt, с которой собрана DLL - доказательство, что Qt действительно внутри.
PROBE_API const char* probe_qt_version(void);

#ifdef __cplusplus
}
#endif

#endif // ENGINE_PROBE_PROBE_H
