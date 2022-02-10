#include <communication/netstats.h>
#include <communication/sync_zmq.h>
#include <limits.h>
#include <stringtools.h>
#include <unistd.h>
#include <iostream>

zmq::context_t *CommunicationZmq::context(nullptr);
std::atomic<bool> CommunicationZmq::die(false);
InputFunction CommunicationZmq::finput;
int CommunicationZmq::localport = 0;
//------------------------------------------------------------------------------
// Client
//------------------------------------------------------------------------------
CommunicationZmq::CommunicationZmq(int id)
    : socket_(*context, zmq::socket_type::dealer) {
    socket_.set(zmq::sockopt::routing_id, "edge" + std::to_string(id));
}

//------------------------------------------------------------------------------
bool CommunicationZmq::connect(const std::string &host, uint16_t port) {
    std::string endpoint("tcp://" + host + ":" + std::to_string(port));
    try {
        socket_.connect(endpoint);
        return true;
    } catch (const zmq::error_t &e) {
        return false;
    }
}

//------------------------------------------------------------------------------
int CommunicationZmq::recv(char *buff, size_t len) {
    zmq::mutable_buffer b(buff, len);
    zmq::recv_buffer_result_t ret = socket_.recv(b);
    assert(ret.value().size == 0);
    ret = socket_.recv(b);
    std::cout << "s: " << ret.value().size << " "
              << ret.value().untruncated_size << std::endl;
    NetStats::add_bytes_in(ret.value().size);
    return ret.value().size;
}

//------------------------------------------------------------------------------
int CommunicationZmq::recv(CommunicationZmq::RecvBuffer &message) {
    zmq::recv_result_t recvd = socket_.recv(message);
    if (recvd.value() == 0) {
        recvd = socket_.recv(message);
    }
    NetStats::add_bytes_in(recvd.value());
    return recvd.value();
}

//------------------------------------------------------------------------------
// Server
//------------------------------------------------------------------------------
static CommunicationZmq *communication = nullptr;
CommunicationZmq::CommunicationZmq(int port, bool dummy)
    : socket_(*context, zmq::socket_type::router) {
    socket_.set(zmq::sockopt::probe_router, 1);
    socket_.set(zmq::sockopt::linger, 0);

    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
    socket_.set(zmq::sockopt::routing_id,
                hostname + (":" + std::to_string(port)));
}

//------------------------------------------------------------------------------
std::set<std::pair<std::string, int>> CommunicationZmq::out_endpoints;
void CommunicationZmq::init(InputFunction f, int port) {
    localport = port;

    finput = f;
    context = new zmq::context_t(1);
    communication = new CommunicationZmq(port, true);

    for (const auto &ep : out_endpoints) {
        communication->connect(ep.first + ".iccluster.epfl.ch", ep.second);
    }
}

//------------------------------------------------------------------------------
void CommunicationZmq::add_endpoint(const std::string &host, int port) {
    out_endpoints.insert(std::make_pair(host, port));
}

//------------------------------------------------------------------------------
ssize_t CommunicationZmq::send(int id, const void *buffer, size_t length) {
    std::string routing_id = std::string("edge") + std::to_string(id);
    return send(routing_id, buffer, length);
}

//------------------------------------------------------------------------------
ssize_t CommunicationZmq::send(const std::string &routing_id,
                               const void *buffer, size_t length) {
    if (communication == nullptr) {
        std::cerr << "Server not initialized" << std::endl;
        return 0;
    }

    std::vector<std::string> route = split(routing_id, " ");
    ssize_t ret = 0;
    for (auto hop = route.begin(); hop != route.end(); ++hop) {
        ret += communication->send_more(*hop, hop != route.begin());
        if (hop + 1 != route.end())
            ret += communication->send_more(std::string());
    }
    ret += communication->send(zmq::const_buffer(buffer, length));

    return ret;
}

//------------------------------------------------------------------------------
void CommunicationZmq::probed(const std::string &probed) {
    auto ep = split(probed, ":");
//    std::cout << probed_.size() << " - " << out_endpoints.size() <<std::endl;
    if (probed_.insert(std::make_pair(ep[0],std::stoi(ep[1]))).second) {
        if (probed_.size() < out_endpoints.size()) {
//            std::cout << "Not done yet!" << std::endl;
        }
    }
}

//------------------------------------------------------------------------------
void CommunicationZmq::iterate() {
    std::string endpoint("tcp://*:" + std::to_string(localport)), sender;
    std::cout << ("Listening on " + endpoint) << std::endl;
    communication->socket_.bind(endpoint);
    while (!die) {
        zmq::message_t message;
        zmq::recv_result_t recvd_size;

        int more, zero_count = 0, nonzero_count = 0;
        std::string multipart_serialized;
        do {
            message.rebuild();
            try {
                recvd_size = communication->socket_.recv(message);
            } catch (const zmq::error_t &e) {
                break;
                break;
            }
            size_t n = recvd_size.value();
            if (n != 0) {
                if (zero_count != 0) NetStats::add_bytes_in(n);
                // does not count sender address as bytes received
                multipart_serialized +=
                    std::string((char *)&n, sizeof(n)) + message.to_string();
                ++nonzero_count;
            } else {
                ++zero_count;
            }
            more = communication->socket_.get(zmq::sockopt::rcvmore);
            if (more && zero_count == 0) {
                sender = message.to_string();
            }
        } while (more);
        if (zero_count == 1 && nonzero_count == 1) {  // it is a probe msg
            // account for the reciprocal probe. It assumes probe_router=1
            communication->probed(sender);
            NetStats::add_bytes_out(
                communication->socket_.get(zmq::sockopt::routing_id).size());
            NetStats::add_bytes_in(sender.size());
        }
        // std::cout << multipart_serialized.size() << std::endl;
        finput(multipart_serialized);
    }
    communication->socket_.close();
}

//------------------------------------------------------------------------------
void CommunicationZmq::finish() {
    die = true;
    delete context;
    context = nullptr;
}

//------------------------------------------------------------------------------
