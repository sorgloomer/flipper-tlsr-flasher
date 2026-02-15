#include <furi.h>
#include "timerpool.h"

struct TimerPool {
    FuriEventLoop* event_loop;
    struct TimerListItem* head;
};

struct TimerListItem {
    struct TimerListItem* prev;
    struct TimerListItem* next;
    TimerPool* pool;
    FuriEventLoopTimerType type;
    FuriEventLoopTimer* timer;
    FuriEventLoopTimerCallback callback;
    void* context;
};

static void timerpool_handle_timer(void* context);

TimerPool* timerpool_alloc(FuriEventLoop* event_loop) {
    TimerPool* pool = malloc(sizeof(TimerPool));
    furi_check(pool);
    pool->event_loop = event_loop;
    TimerHandle* item = malloc(sizeof(TimerHandle));
    furi_check(item);
    item->timer = NULL;
    item->next = item;
    item->prev = item;
    pool->head = item;
    return pool;
}

void timerpool_free(TimerPool* pool) {
    if(pool == NULL) return;
    TimerHandle* item = pool->head->next;
    int i = 0;
    while(item != pool->head) {
        i++;
        TimerHandle* next = item->next;
        furi_event_loop_timer_free(item->timer);
        free(item);
        item = next;
    }
    FURI_LOG_I("swire", "timerpool_free items %d", i);
    free(pool->head);
    free(pool);
}

void timerpool_submit(
    TimerPool* pool,
    uint32_t timeout_ms,
    FuriEventLoopTimerType type,
    FuriEventLoopTimerCallback callback,
    void* context) {
    TimerHandle* item = malloc(sizeof(TimerHandle));
    furi_check(item);
    item->pool = pool;
    item->type = type;
    item->callback = callback;
    item->context = context;
    item->timer =
        furi_event_loop_timer_alloc(pool->event_loop, timerpool_handle_timer, type, item);

    item->prev = pool->head->prev;
    item->next = pool->head;
    item->next->prev = item;
    item->prev->next = item;
    furi_event_loop_timer_start(item->timer, timeout_ms);
}

FuriEventLoopTimer* timerpool_get_timer(TimerHandle* item) {
    return item->timer;
}

void timerpool_cancel(TimerHandle* item) {
    if(item == NULL) return;
    TimerHandle* prev = item->prev;
    TimerHandle* next = item->next;
    prev->next = next;
    next->prev = prev;
    furi_event_loop_timer_free(item->timer);
    free(item);
}

static void timerpool_handle_timer(void* context) {
    TimerHandle* item = (TimerHandle*)context;
    item->callback(item->context);
    if(item->type == FuriEventLoopTimerTypeOnce) {
        furi_event_loop_pend_callback( // TODO check if delay is needed
            item->pool->event_loop,
            (FuriEventLoopPendingCallback)timerpool_cancel,
            item);
    }
}
