#pragma once

#include <communication_manager.h>

#include <atomic>
#include <zmq/zmq.hpp>
#include <set>

class CommunicationZmq {
   public:
    CommunicationZmq(int p, bool dummy);        // server
    CommunicationZmq(int id);  // client

    // If client: connect, send, recv
    bool connect(const std::string &host, uint16_t port);

    template <typename T>
    size_t send(const T &);
    template <typename T>
    size_t send_more(const T &, bool count = true);
    static ssize_t send(int socket, const void *buffer, size_t length);
    static ssize_t send(const std::string &routing_id, const void *buffer,
                        size_t length);

    typedef zmq::message_t RecvBuffer;
    int recv(RecvBuffer &);
    int recv(char *, size_t);
    void probed(const std::string &probed);

    // If server: init, iterate, finish
    static void init(InputFunction, int port);
    static void iterate();
    static void finish();
    static void add_endpoint(const std::string &host, int port);

    static std::atomic<size_t> bytes_out, bytes_in;
    static std::set<std::pair<std::string, int>> out_endpoints;

   private:
    static InputFunction finput;
    static std::atomic<bool> die;
    static zmq::context_t* context;
    static int localport;
    zmq::socket_t socket_;
    std::set<std::pair<std::string,int>> probed_;
};

#include <communication/sync_zmq.hpp>
