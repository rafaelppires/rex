#include <communication/async_zmq.h>
#include <communication/netstats.h>
#include <unistd.h>
#include <climits>
#include <iostream>
#include <thread>

//------------------------------------------------------------------------------
std::atomic<bool> AsyncZmq::die(false);
std::mutex AsyncZmq::callbackmap_mtx_;
std::map<int, AsyncZmq::CallbackType> AsyncZmq::callbacks_;
//------------------------------------------------------------------------------
AsyncZmq::AsyncZmq(const std::string& endpoint, const std::string& idprefix)
    : context_(1),
      frontend_(context_, ZMQ_ROUTER),
      backend_(context_, ZMQ_ROUTER) {
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
    frontend_.set(zmq::sockopt::routing_id, idprefix + hostname);
    frontend_.set(zmq::sockopt::probe_router, 1);
    frontend_.connect("tcp://" + endpoint);
    NetStats::add_bytes_out(frontend_.get(zmq::sockopt::routing_id).size());
    backend_.bind("ipc://backend.ipc");
}

//------------------------------------------------------------------------------
void AsyncZmq::finish() { die = true; }

//------------------------------------------------------------------------------
void AsyncZmq::add_callback(int id, CallbackType cb) {
    std::unique_lock<std::mutex> lock(callbackmap_mtx_);
    callbacks_[id] = cb;
}

//------------------------------------------------------------------------------
extern void print(const char* fmt, ...);
void AsyncZmq::worker_thread(int id) {
    zmq::context_t context(1);
    zmq::socket_t worker(context, ZMQ_REQ);
    worker.set(zmq::sockopt::routing_id, "worker" + std::to_string(id));
    worker.connect("ipc://backend.ipc");
    while (!die) {
        std::string r("READY");
        worker.send(zmq::const_buffer(r.data(), r.size()));
        zmq::message_t m;
        std::vector<std::string> msg = internal_recv_multipart(worker);
        if (msg.size() == 1) {
            continue;  // probe msg or suicide request
        }
        std::string prefix = "internal";
        if (msg[1].rfind(prefix) == 0) {
            int rank = std::stoi(msg[1].substr(prefix.size()));
            CallbackType callback;
            {
                std::unique_lock<std::mutex> lock(callbackmap_mtx_);
                callback = callbacks_[rank];
            }
            // print("W%d from %s to r%d\n", id, msg[0].c_str(), rank);
            if (callback) {
                Datatype t;
                try {
                    callback(unpack(std::move(msg.back()), t));
                } catch (const std::out_of_range& e) {
                    std::cerr << "\x1B[31m" << e.what() << "\x1B[0m"
                              << std::endl;
                }
            } else {
                std::cerr << "Could not find a suitable callback for rank "
                          << rank << std::endl;
            }
        } else {
            std::cerr << "Unknown destination: " << msg[1] << std::endl;
        }
    }
}

//------------------------------------------------------------------------------
size_t AsyncZmq::send_standing_messages() {
    size_t ret = 0;
    if (serverid_.empty()) return ret;
    while (true) {
        std::pair<int, std::string> next;
        {
            std::unique_lock<std::mutex> lock(oqueue_mtx_);
            if (outqueue_.empty()) break;
            outqueue_.front().swap(next);
            outqueue_.pop();
        }
        ret +=
            send_more(serverid_, false);  // doesn't count serverid as data sent
        std::string empty;
        ret += send_more(empty);
        ret += send_more("internal" + std::to_string(next.first));
        ret += send_more(empty);
        ret += send(next.second);
    }
    return ret;
}

//------------------------------------------------------------------------------
void AsyncZmq::handle_backend() {
    zmq::message_t m;
    backend_.recv(m);
    worker_queue_.push(m.to_string());
    backend_.recv(m);
    assert(m.size() == 0);
    backend_.recv(m);
    assert(m.to_string() == "READY");
}

//------------------------------------------------------------------------------
void AsyncZmq::handle_frontend() {
    if (worker_queue_.empty()) return;
    std::string address = worker_queue_.front();
    worker_queue_.pop();
    recv_and_forward(address);
}

//------------------------------------------------------------------------------
size_t AsyncZmq::recv_and_forward(const std::string& address) {
    size_t ret = 0;
    zmq::message_t message;
    zmq::recv_result_t recvd_size;
    int more, zero_count = 0, nonzero_count = 0;
    std::string sender;

    ret += zmq_send_more(backend_, address, false);
    ret += zmq_send_more(backend_, std::string(), false);
    // false: since it is internal movement of data, it does not account in
    // traffic measurements.
    do {
        message.rebuild();
        recvd_size = frontend_.recv(message);
        size_t n = recvd_size.value();
        if (n != 0) {
            ++nonzero_count;
            if (zero_count != 0) NetStats::add_bytes_in(n);
            // does not count sender address as bytes received
        } else {
            ++zero_count;
        }

        more = frontend_.get(zmq::sockopt::rcvmore);
        if (more) {
            if (zero_count == 0) sender = message.to_string();
            ret += zmq_send_more(backend_, message, false);
        } else {
            ret += zmq_send(backend_, message, false);
        }
    } while (more);
    if (zero_count == 1 && nonzero_count == 1) {  // probe msgs
        if (serverid_.empty()) serverid_ = sender;
        NetStats::add_bytes_in(sender.size());
    }
    return ret;  // amount of bytes forwarded
}

//------------------------------------------------------------------------------
std::vector<std::string> AsyncZmq::internal_recv_multipart(
    zmq::socket_t& worker_socket) {
    std::vector<std::string> ret;
    zmq::message_t message;
    zmq::recv_result_t recvd_size;
    int more;
    do {
        message.rebuild();
        recvd_size = worker_socket.recv(message);
        if (recvd_size.value() != 0) {
            ret.push_back(message.to_string());
        }
        more = worker_socket.get(zmq::sockopt::rcvmore);
    } while (more);
    return ret;
}

//------------------------------------------------------------------------------
void AsyncZmq::polling_loop() {
    int thread_count = 16;
    std::thread* t[thread_count];
    for (int i = 0; i < thread_count; ++i) {
        t[i] = new std::thread(AsyncZmq::worker_thread, i);
    }

    while (!die) {
        send_standing_messages();
        zmq::pollitem_t items[] = {{backend_, 0, ZMQ_POLLIN, 0},
                                   {frontend_, 0, ZMQ_POLLIN, 0}};

        int nevents,
            timeout = 200;  // in ms, -1 for indefinetely (not good for standing
                            // messages)
        if (worker_queue_.empty()) {  // if no worker, just check backend
            nevents = zmq::poll(&items[0], 1, timeout);
        } else {
            nevents = zmq::poll(&items[0], 2, timeout);
        }
        send_standing_messages();

        if (nevents <= 0) continue;

        if (items[0].revents & ZMQ_POLLIN) {
            handle_backend();
        }
        if (items[1].revents & ZMQ_POLLIN) {
            handle_frontend();
        }
    }

    for (int i = 0; i < thread_count; ++i) {
        t[i]->join();
        delete t[i];
    }
}

//------------------------------------------------------------------------------
void AsyncZmq::announce_completion(int rank, char t) {
    {
        std::unique_lock<std::mutex> lock(oqueue_mtx_);
        outqueue_.push(
            std::make_pair(rank, pack(t + std::string("so long"), TEXT)));
    }
    callbacks_.erase(rank);
    if (callbacks_.empty()) {
        finish();
        // Job is done! Ask workers to kill themselves
        for (int i = 0; i < 16; ++i) {
            std::string address = "worker" + std::to_string(i);
            zmq_send_more(backend_, address, false);
            zmq_send_more(backend_, std::string(), false);
            zmq_send(backend_, std::string("suicide, please"), false);
        }
    }
}

//------------------------------------------------------------------------------
