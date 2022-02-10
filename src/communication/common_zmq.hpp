#pragma once
#include <communication/netstats.h>

//------------------------------------------------------------------------------
template <typename T>
size_t zmq_send_more(zmq::socket_t& socket, const T& data, bool count = true) {
    zmq::send_result_t sent = socket.send(
        zmq::const_buffer(data.data(), data.size()), zmq::send_flags::sndmore);
    if (count) NetStats::add_bytes_out(sent.value());
    return sent.value();
}

//------------------------------------------------------------------------------
template <typename T>
size_t zmq_send(zmq::socket_t& socket, const T& data, bool count = true) {
    zmq::send_result_t sent =
        socket.send(zmq::const_buffer(data.data(), data.size()));
    if (count) NetStats::add_bytes_out(sent.value());
    return sent.value();
}

//------------------------------------------------------------------------------
