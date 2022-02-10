#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <zmq/zmq.hpp>

class AsyncZmq {
   public:
    AsyncZmq(const std::string &endpoint, const std::string &idprefix);
    typedef std::function<void(std::string &&)> CallbackType;
    template <typename T>
    size_t push(int id, const T &data);
    void polling_loop();
    void handle_backend();
    void handle_frontend();
    void announce_completion(int rank, char t);

    template <typename T>
    size_t send_more(const T &, bool count = true);
    template <typename T>
    size_t send(const T &);

    static void add_callback(int id, CallbackType);
    static void finish();

   private:
    std::mutex oqueue_mtx_, iqueue_mtx_;
    std::queue<std::pair<int, std::string>> outqueue_;
    zmq::context_t context_;
    zmq::socket_t frontend_, backend_;
    std::queue<std::string> worker_queue_;
    std::string serverid_;

    size_t recv_and_forward(const std::string &address);
    size_t send_standing_messages();

    static void worker_thread(int id);
    static std::vector<std::string> internal_recv_multipart(zmq::socket_t &w);

    static std::atomic<bool> die;
    static std::mutex callbackmap_mtx_;
    static std::map<int, CallbackType> callbacks_;
};

#include <communication/async_zmq.hpp>
