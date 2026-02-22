#include <furi.h>
#include "./timerpool.hpp"
#include "src/furi/duration.hpp"
#include "src/buildconf.hpp"

static void timerpool_handle_timer(void* context);
static void timerhandle_cancel_and_delete(void* context);

TimerPool::TimerPool(FuriEventLoop* event_loop) {
    this->event_loop = event_loop;
    TimerHandle* item = new TimerHandle();
    furi_check(item);
    item->timer = nullptr;
    item->next = item;
    item->prev = item;
    this->head = item;
}

TimerPool::~TimerPool() {
    FURI_LOG_I(TAG, "TimerPool::~TimerPool checkpoint 1");
    TimerHandle* item = this->head->next;
    int i = 0;
    while(item != this->head) {
        i++;
        TimerHandle* next = item->next;
        furi_event_loop_timer_free(item->timer);
        delete item;
        item = next;
    }
    FURI_LOG_I(TAG, "timerpool_free items %d", i);
    delete this->head;
}

void TimerPool::submit(
    furi::u32ms timeout,
    FuriEventLoopTimerType type,
    FuriEventLoopTimerCallback callback,
    void* context) {
    TimerHandle* item = new TimerHandle();
    furi_check(item);
    item->pool = this;
    item->type = type;
    item->callback = callback;
    item->context = context;
    item->timer =
        furi_event_loop_timer_alloc(this->event_loop, timerpool_handle_timer, type, item);

    item->prev = this->head->prev;
    item->next = this->head;
    item->next->prev = item;
    item->prev->next = item;
    furi_event_loop_timer_start(item->timer, timeout.count());
}

FuriEventLoopTimer* TimerHandle::get_raw_timer() {
    return this->timer;
}

void TimerHandle::cancel() {
    TimerHandle* prev = this->prev;
    TimerHandle* next = this->next;
    prev->next = next;
    next->prev = prev;
    furi_event_loop_timer_free(this->timer);
    this->timer = nullptr;
    this->callback = nullptr;
    this->context = nullptr;
    this->prev = nullptr;
    this->next = nullptr;
}

static void timerpool_handle_timer(void* context) {
    TimerHandle* item = static_cast<TimerHandle*>(context);
    if(item->callback != nullptr) {
        item->callback(item->context);
    }
    if(item->type == FuriEventLoopTimerTypeOnce) {
        furi_event_loop_pend_callback( // TODO check if delay is needed. Yes, it is needed, because the furi kernel is not prepared for this
            item->pool->get_raw_event_loop(),
            timerhandle_cancel_and_delete,
            item);
    }
}

static void timerhandle_cancel_and_delete(void* context) {
    auto handle = static_cast<TimerHandle*>(context);
    handle->cancel();
    delete handle;
}
