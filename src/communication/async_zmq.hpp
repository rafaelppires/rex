#include <envelope.h>
#include <communication/common_zmq.hpp>
//------------------------------------------------------------------------------
template <typename T>
size_t AsyncZmq::push(int id, const T &data) {
    std::unique_lock<std::mutex> lock(oqueue_mtx_);
    outqueue_.push(std::make_pair(id, pack(data)));
    return outqueue_.back().second.size();
}

//------------------------------------------------------------------------------
template <typename T>
size_t AsyncZmq::send_more(const T &data, bool count) {
    return zmq_send_more(frontend_, data, count);
}

//------------------------------------------------------------------------------
template <typename T>
size_t AsyncZmq::send(const T &data) {
    return zmq_send(frontend_, data);
}

//------------------------------------------------------------------------------
