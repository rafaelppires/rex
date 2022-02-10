#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
   public:
    ThreadPool(uint8_t n);
    ~ThreadPool();

    void add_task(std::function<void()>);

   private:
    void worker();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_queue_;
    std::condition_variable condition_;
    bool terminate_;
};
