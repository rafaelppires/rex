#include "mf_coordinator.h"

#include <machine_learning/matrix_factorization.h>

#include <boost/graph/erdos_renyi_generator.hpp>
#include <boost/graph/make_connected.hpp>
#include <boost/graph/small_world_generator.hpp>
#include <boost/random/linear_congruential.hpp>
#include <future>

//------------------------------------------------------------------------------
Graph random_graph_small_world(unsigned num_nodes) {
    typedef boost::small_world_iterator<boost::minstd_rand, Graph> SWGen;
    boost::minstd_rand gen;
    return Graph(SWGen(gen, num_nodes, 6, 0.03), SWGen(), num_nodes);
}

//------------------------------------------------------------------------------
Graph random_graph_erdos_renyi(unsigned num_nodes) {
    typedef boost::erdos_renyi_iterator<boost::minstd_rand, Graph> ERGen;
    boost::minstd_rand gen;
    return Graph(ERGen(gen, num_nodes, 0.05), ERGen(), num_nodes);
}

//------------------------------------------------------------------------------
// MFCoordinator
//------------------------------------------------------------------------------
MFCoordinator::MFCoordinator(std::vector<MFNode> &nodes, bool shared_memory)
    : nodes_(nodes), shared_memory_(shared_memory) {}

//------------------------------------------------------------------------------
unsigned MFCoordinator::establish_relations(Graph &G) {
    unsigned edges = 0;
    auto name = get(boost::vertex_index, G);
    typename boost::graph_traits<Graph>::vertex_iterator ui, ui_end;
    for (boost::tie(ui, ui_end) = vertices(G); ui != ui_end; ++ui) {
        unsigned id = get(name, *ui);
        typename boost::graph_traits<Graph>::out_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = out_edges(*ui, G); ei != ei_end; ++ei) {
            unsigned neigh = get(name, target(*ei, G));
            if (id < nodes_.size() && neigh < nodes_.size()) {
                if (nodes_[id].add_neighbour(neigh)) ++edges;
                if (nodes_[neigh].add_neighbour(id)) ++edges;
            } else {
                std::cerr << "Out of bounds nodes[" << id << "].add_neighbour("
                          << neigh << ")" << std::endl;
                abort();
            }
        }
    }
    return edges;
}

//------------------------------------------------------------------------------
void MFCoordinator::coordinate_epoch(int epoch, ThreadPool &tp) {
    std::vector<std::pair<unsigned, std::future<TrainInfo>>> results;
    epoch_stats_.start();
    for (auto &n : nodes_) {
        auto shared = std::make_shared<std::packaged_task<TrainInfo()>>(
            std::bind(&MFNode::train_and_share, &n, epoch));
        results.emplace_back(std::make_pair(n.rank(), shared->get_future()));
        tp.add_task([shared]() { (*shared)(); });
    }

    double sum_train_err = 0, sum_test_err = 0, sum_time = 0, sum_items = 0,
           sumbout = 0, sumbin = 0;
    unsigned count = 0;
    for (auto &kv : results) {
        kv.second.wait();  // barrier
        auto retval = kv.second.get();
        sum_train_err += retval.train_err;
        sum_items += retval.train_count;
        sum_test_err += retval.test_err;
        sum_time += retval.duration;
        sumbout += retval.bytes_out;
        sumbin += retval.bytes_in;
        ++count;
    }
    epoch_stats_.stop();
    std::cout << epoch << ";" << absolute_timer_.stop() << ";"
              << (sum_train_err / count) << ";" << (sum_test_err / count) << ";"
              << (sum_items / count) << ";" << (sum_time / count) << ";"
              << (sumbout / count) << ";" << (sumbin / count) << ";" << count
              << std::endl;
    // std::cout << epoch_stats_.summary() << std::endl;
}

//------------------------------------------------------------------------------
void MFCoordinator::run(uint8_t lowscore, uint8_t highscore, int matrix_rank,
                        double learning, double regularization, int iterations,
                        bool dpsgd, bool randgraph, unsigned local,
                        size_t steps_per_iteration, unsigned share_howmany) {
    Graph g = randgraph ? random_graph_erdos_renyi(nodes_.size())
                        : random_graph_small_world(nodes_.size());
    make_connected(g);
    unsigned edges = establish_relations(g);
    //std::cout << (randgraph ? "Erdos-Renyi" : "Small World")
    //          << " Mean degree: " << double(edges) / nodes_.size() << std::endl;

    double init_bias = lowscore,
           init_factor = sqrt(double(highscore - lowscore) / matrix_rank);
    HyperMFSGD hyper(matrix_rank, learning, regularization, init_bias,
                     init_factor);
    for (auto &n : nodes_) {
        n.init_training(this, hyper, dpsgd ? DPSGD : RMW, local,
                        steps_per_iteration, share_howmany);
    }

    unsigned processes = std::thread::hardware_concurrency();
    ThreadPool tp(processes);
    std::cout << "epoch;timestamp;meantrainerr;meantesterr;meandataitems;time;"
                 "bytesout;bytesin;nodes\n";
    absolute_timer_.start();
    for (int e = 0; e <= iterations; ++e) {
        coordinate_epoch(e, tp);
    }
}

//------------------------------------------------------------------------------
size_t MFCoordinator::send(unsigned src, unsigned dst,
                           std::shared_ptr<ShareableModel> m) {
    if (shared_memory_) {
        return nodes_[dst].receive(src, m);
    } else {
        return nodes_[dst].receive(src, m->serialize());
    }
}

//------------------------------------------------------------------------------
