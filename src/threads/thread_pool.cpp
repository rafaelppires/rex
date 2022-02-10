#include "thread_pool.h"

//------------------------------------------------------------------------------
ThreadPool::ThreadPool(uint8_t n) : terminate_(false) {
    for (uint8_t i = 0; i < n; ++i) {
        workers_.emplace_back(&ThreadPool::worker, this);
    }
}

//------------------------------------------------------------------------------
void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_queue_);
            condition_.wait(lock,
                            [this]() { return terminate_ || !tasks_.empty(); });
            if (terminate_) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

//------------------------------------------------------------------------------
void ThreadPool::add_task(std::function<void()> t) {
    {
        std::unique_lock<std::mutex> lock(mutex_queue_);
        if (terminate_)
            throw std::runtime_error("Adding task to a dying ThreadPool");
        tasks_.emplace(t);
    }
    condition_.notify_one();
}

//------------------------------------------------------------------------------
ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(mutex_queue_);
        terminate_ = true;
    }
    condition_.notify_all();
    for (auto &w : workers_) w.join();
}

//------------------------------------------------------------------------------
