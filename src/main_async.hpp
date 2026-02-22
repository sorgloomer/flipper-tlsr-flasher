#pragma once

#include <coroutine>
#include "src/utils/timerpool.hpp"
#include "src/app/app.hpp"

using namespace std::chrono_literals;

struct task {
public:
    struct promise_type {
        task get_return_object() {
            return task(std::coroutine_handle<promise_type>::from_promise(*this));
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

    ~task() {
        FURI_LOG_I(TAG, "checkpoint ~task");
        this->destroy();
    }

    task(std::coroutine_handle<promise_type> handle)
        : handle(handle) {
    }
    task& operator=(task&& other) noexcept {
        this->~task();
        this->handle = other.handle;
        other.handle = nullptr;
        return *this;
    }
    task(task&& other) noexcept {
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
        FURI_LOG_I(TAG, "checkpoint ~Implicits");
    }
};

auto delay(Implicits* timerpool, furi::u32ms delay);
task main_async(Implicits* timerpool);
