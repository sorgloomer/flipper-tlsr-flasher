#pragma once

#include <coroutine>
#include "src/utils/timerpool.hpp"
#include "src/app/app.hpp"

using namespace std::chrono_literals;

struct coroutine {
public:
    struct promise_type {
        coroutine get_return_object() {
            return coroutine(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        std::suspend_never initial_suspend() {
            return {};
        }
        std::suspend_never final_suspend() noexcept {
            return {};
        }
        void return_void() {
        }
        void unhandled_exception() {
        }
    };

    std::coroutine_handle<promise_type> handle;

    ~coroutine() {
        SW_DEBUG_TRACE("~task 1");
        this->destroy();
    }

    coroutine(std::coroutine_handle<promise_type> handle)
        : handle(handle) {
    }
    coroutine& operator=(coroutine&& other) noexcept {
        this->~coroutine();
        this->handle = other.handle;
        other.handle = nullptr;
        return *this;
    }
    coroutine(coroutine&& other) noexcept {
        *this = std::move(other);
    }
    void destroy() {
        if(handle != nullptr) {
            handle.destroy();
        }
        handle = nullptr;
    }
};

struct Implicits {
    TimerPool* timers;
    SwireApp* app;

    ~Implicits() {
        SW_DEBUG_TRACE("~Implicits 1");
    }
};

auto delay(Implicits* implicits, furi::u32ms delay);
coroutine main_async(Implicits* implicits);
