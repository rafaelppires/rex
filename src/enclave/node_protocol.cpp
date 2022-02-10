#include "node_protocol.h"
#include <json_utils.h>
#include <stringtools.h>
#ifdef NATIVE
extern void ocall_farewell();
#endif
//------------------------------------------------------------------------------
NodeProtocol::NodeProtocol() : degree_(-1) {}

//------------------------------------------------------------------------------
int NodeProtocol::init(EnclaveArguments &args) {
    degree_ = args.degree;
    dpsgd_ = args.dpsgd;
    share_howmany_ = args.share_howmany;
    local_ = args.local;
    steps_per_iteration_ = args.steps_per_iteration;
    epochs_ = args.epochs;

    // Train and test data
    typedef TripletVector<uint8_t>::value_type TripletType;
    auto node_data = std::make_shared<DataStore>();
    TripletType *begin = reinterpret_cast<TripletType *>(args.train);
    TripletType *end = begin + args.train_size;
    std::for_each(begin, end, [&](const TripletType &t) {
        (*node_data)[std::make_pair(t.row(), t.col())] = t.value();
    });
    begin = reinterpret_cast<TripletType *>(args.train);
    end = begin + args.test_size;
    TripletVector<uint8_t> test_set(begin, end);

    node_ = std::make_shared<MFNode>(args.userrank, node_data, test_set,
                                     args.modelshare, args.datashare, "");
    printf("Hello enclave! I'm %d. Train: %ld. Test: %ld\n", args.userrank,
           node_data->size(), test_set.size());

    unsigned i = 0;
    for (const auto &n : split(args.nodes, " ")) {
        if (n.empty()) continue;
        if (n != "-") {
            node_->add_neighbour(i);
            rank_netid[i] = n;
            netid_rank[n] = i;
//            std::cout << i << " " << n << std::endl;
        }
        ++i;
    }

#ifndef NATIVE
    print_training_summary(trigger_training());
#endif
    return 0;
}

//------------------------------------------------------------------------------
void NodeProtocol::print_training_summary(const TrainInfo &info) {
    if (!info.dummy()) {
        int epoch = node_->finished_epoch();
        printf("%d;%lf;%lf;%lf;%ld;%lf;%ld;%ld\n", epoch, absolutetime_->stop(),
               info.train_err, info.test_err, info.train_count, info.duration,
               info.bytes_out, info.bytes_in);
        if (epoch >= epochs_) {
            std::cout << node_->summary();
            ocall_farewell();
        }
    }
}

//------------------------------------------------------------------------------
TrainInfo NodeProtocol::trigger_training() {
    double init_bias = 1, init_factor = sqrt(0.9);
    HyperMFSGD hyper(10, 0.001, 0.1, init_bias, init_factor);
    absolutetime_ = std::make_shared<TimeProbe>();
    absolutetime_->start();
    printf(
        "epoch;timestamp;trainerr;testerr;traincount;duration;bytesout;"
        "bytesin\n");
    node_->init_training(this, hyper, dpsgd_ ? DPSGD : RMW, local_,
                         steps_per_iteration_, share_howmany_);
    return node_->train_and_share(0);
}

//------------------------------------------------------------------------------
bool NodeProtocol::all_neighbors_attested() {
    return neighbors.size() == degree_ &&
           std::all_of(attested.begin(), attested.end(),
                       [](std::map<std::string, bool>::const_reference node) {
                           return node.second;
                       });
}

//------------------------------------------------------------------------------
void NodeProtocol::input(const std::vector<std::string> &message) {
    if (message.empty()) return;
    const std::string &nodeid = message[0];
    if (message.size() == 1) {
        bool inserted = neighbors.insert(nodeid).second;
#ifdef NATIVE
        if (inserted && neighbors.size() == degree_)
            print_training_summary(trigger_training());
#else
        if (inserted && attested.find(nodeid) == attested.end()) {
            std::cout << nodeid << std::endl;
            attested[nodeid] = false;
            trigger_attestation(nodeid);
        }
#endif
    } else {
        /*std::cout << nodeid << " " << message[1].size() << "- "
                  << int(message[1][0]) << std::endl;*/
#ifdef NATIVE
        std::vector<uint8_t> content(message[1].begin(), message[1].end());
        node_->receive(netid_rank[nodeid], content);
        print_training_summary(node_->trigger_epoch_if_ready(degree_).second);
#else
        if (attested.find(nodeid) == attested.end() || !attested[nodeid]) {
            attestation_message(nodeid, message[1]);
        } else {
            auto m = decrypt_received(nodeid, message[1]);
            auto &content = m.second;
            if (m.first) {
                node_->receive(netid_rank[nodeid], content);
                print_training_summary(
                    node_->trigger_epoch_if_ready(degree_).second);
            } else {
                std::cerr << "Error in decryption of a message from " << nodeid
                          << std::endl;
            }
        }
#endif
    }
}

#ifndef NATIVE
//------------------------------------------------------------------------------
void NodeProtocol::trigger_attestation(const std::string &nodeid) {
    if (!attestor_.was_challenged(nodeid)) {
        send(nodeid, new_attest_msg(nodeid));
    }
}

//------------------------------------------------------------------------------
void NodeProtocol::attestation_message(const std::string &nodeid,
                                       const std::string &message) {
    // std::cout << message[1] << std::endl;
    json input(parse_json(message));
    if (input.find(ATTEST_CHALLENGE) != input.end()) {
        std::string challenge = attestor_.new_challenge(
            nodeid, input[ATTEST_CHALLENGE].get<std::string>());
        send(nodeid, challenge);
    } else if (input.find(CHALLENGE_RESPONSE) != input.end()) {
        int res = attestor_.challenge_response(
            nodeid, input[CHALLENGE_RESPONSE].get<std::string>());
        if (res == 0 || res == 1) {
            attested[nodeid] = true;
            send_queued(nodeid);
        }
    }
}

//------------------------------------------------------------------------------
std::string NodeProtocol::new_attest_msg(const std::string &dst) {
    json j;
    j[ATTEST_CHALLENGE] = attestor_.generate_challenge(dst);
    return j.dump();
}
#endif

//------------------------------------------------------------------------------
size_t NodeProtocol::send_queued(const std::string &dstid) {
    size_t ret = 0;
    auto &waiting_queue = waiting[dstid];
    while (!waiting_queue.empty()) {
#ifdef NATIVE
        ret += send(dstid, waiting_queue.front()->serialize());
#else
        ret += encrypted_send(dstid, waiting_queue.front()->serialize());
#endif
        waiting_queue.pop();
    }
    return ret;
}

//------------------------------------------------------------------------------
size_t NodeProtocol::send(unsigned src, unsigned dst,
                          std::shared_ptr<ShareableModel> m) {
    size_t ret = 0;
    const std::string dstid = rank_netid[dst];
#ifdef NATIVE
    ret += send(dstid, m->serialize());
#else
    if (attested.find(dstid) == attested.end() || !attested[dstid]) {
        waiting[dstid].emplace(m);
    } else {
        ret += send_queued(dstid);
        ret += encrypted_send(dstid, m->serialize());
    }
#endif
    return ret;
}

//------------------------------------------------------------------------------
