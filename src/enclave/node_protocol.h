#pragma once
#include <machine_learning/mf_node.h>
#include <queue>
#include "args_rex.h"

#ifndef NATIVE
#include <attestor.h>
extern "C" {
extern int printf(const char *fmt, ...);
}
#endif

class NodeProtocol : Communication {
   public:
    NodeProtocol();

    int init(EnclaveArguments &args);
    void input(const std::vector<std::string> &message);
    virtual size_t send(unsigned src, unsigned dst,
                        std::shared_ptr<ShareableModel> m);

   private:
    void print_training_summary(const TrainInfo &info);
    TrainInfo trigger_training();
    bool all_neighbors_attested();
    template <typename T>
    size_t send(const std::string &dst, const T &data);
    size_t send_queued(const std::string &dstid);
    template <typename T>
    size_t encrypted_send(const std::string &dst, const T &data);
    template <typename T>
    std::pair<bool, std::vector<uint8_t>> decrypt_received(
        const std::string &src, const T &data);

    std::shared_ptr<MFNode> node_;
    size_t degree_, steps_per_iteration_;
    unsigned share_howmany_, local_, epochs_;
    bool dpsgd_;
    std::shared_ptr<TimeProbe> absolutetime_;
#ifndef NATIVE
    void trigger_attestation(const std::string &nodeid);
    std::string new_attest_msg(const std::string &dst);
    void attestation_message(const std::string &nodeid,
                             const std::string &message);
    Attestor attestor_;
#endif

    // Neighbor management
    std::map<std::string, std::queue<std::shared_ptr<ShareableModel>>> waiting;
    std::set<std::string> neighbors;
    std::map<std::string, bool> attested;
    std::map<unsigned, std::string> rank_netid;
    std::map<std::string, unsigned> netid_rank;
};

#include "node_protocol.hpp"
