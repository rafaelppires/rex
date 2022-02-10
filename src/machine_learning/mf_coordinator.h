#pragma once

#include <threads/thread_pool.h>
#include <utils/time_probe.h>

#include <boost/graph/adjacency_list.hpp>

#include "mf_node.h"

//------------------------------------------------------------------------------
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>
    Graph;

//------------------------------------------------------------------------------
class MFCoordinator : Communication {
   public:
    MFCoordinator(std::vector<MFNode> &nodes, bool shared_memory);
    void run(uint8_t lowscore, uint8_t highscore, int matrix_rank,
             double learning, double regularization, int iterations,
             bool dpsgd, bool randgraph, unsigned local, 
             size_t steps_per_iteration, unsigned share_howmany);
    virtual size_t send(unsigned src, unsigned dst,
                      std::shared_ptr<ShareableModel>);

   private:
    void coordinate_epoch(int epoch, ThreadPool &tp);
    unsigned establish_relations(Graph &G);

    bool datashare_, shared_memory_;
    TimeProbeStats epoch_stats_;
    TimeProbe absolute_timer_;

    std::vector<MFNode> &nodes_;
};

//------------------------------------------------------------------------------
