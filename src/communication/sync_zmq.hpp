#include <communication/common_zmq.hpp>
//------------------------------------------------------------------------------
template <typename T>
size_t CommunicationZmq::send_more(const T& data, bool count) {
    return zmq_send_more(socket_, data, count);
}

//------------------------------------------------------------------------------
template <typename T>
size_t CommunicationZmq::send(const T& data) {
    size_t ret = zmq_send_more(socket_, std::string());
    return ret + zmq_send(socket_, data);
}

//------------------------------------------------------------------------------
