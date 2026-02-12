#include <furi.h>

typedef struct TimerPool TimerPool;
typedef struct TimerListItem TimerHandle;

FuriEventLoopTimer* timerpool_get_timer(TimerHandle* item);
void timerpool_cancel(TimerHandle* item);
TimerPool* timerpool_alloc(FuriEventLoop* event_loop);
void timerpool_free(TimerPool* pool);
void timerpool_submit(
    TimerPool* pool,
    uint32_t interval_ms,
    FuriEventLoopTimerType type,
    FuriEventLoopTimerCallback callback,
    void* context);
