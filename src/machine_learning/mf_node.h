#pragma once

#include <utils/time_probe.h>

#include <Eigen/Sparse>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>

#include "mf_decentralized.h"

enum ModelMergerType { RMW, DPSGD, UNKKOWN };

//------------------------------------------------------------------------------
class ShareableModel {
   public:
    ShareableModel() = default;
    ShareableModel(int e, ModelMergerType t, const MatrixFactorizationModel &m,
                   SharingRatings data);

    virtual std::vector<uint8_t> serialize() const;
    virtual size_t deserialize(const std::vector<uint8_t> &data);
    static ModelMergerType extract_type(const std::vector<uint8_t> &data);

    ModelMergerType type_;
    int epoch;
    MatrixFactorizationModel model_;
    SharingRatings rawdata;
};
typedef std::shared_ptr<ShareableModel> ShareableModelPtr;

//------------------------------------------------------------------------------
class Communication {
   public:
    virtual size_t send(unsigned src, unsigned dst,
                        std::shared_ptr<ShareableModel> m) = 0;
};

//------------------------------------------------------------------------------
class ModelMerger {
   public:
    ModelMerger(unsigned rank, unsigned share_howmany, Communication *c,
                std::shared_ptr<MFSGDDecentralized> trainer,
                std::set<unsigned> &neighbours, bool modelshare,
                bool datashare);

    virtual size_t share(int epoch) = 0;
    virtual void merge(int epoch) = 0;
    void receive(unsigned src, std::shared_ptr<ShareableModel> m);
    bool received_all(int epoch, size_t howmany);
#ifndef ENCLAVED
    virtual void set_logfile(std::shared_ptr<std::ofstream> file);
#endif

   protected:
    SharingRatings extract_ratings(unsigned howmany);

    std::shared_ptr<MFSGDDecentralized> trainer_;
    std::set<unsigned> &neighbours_;
    std::set<std::pair<unsigned, int>> recvdfrom_;
    std::map<int, std::vector<std::pair<unsigned, ShareableModelPtr>>>
        received_models_;
    std::mutex recv_mtx_;
    Communication *communication_;
    unsigned userrank_, share_howmany_;
    bool modelshare_, datashare_;

#ifndef ENCLAVED
    std::shared_ptr<std::ofstream> logfile_;
#endif
};

//------------------------------------------------------------------------------
struct TrainInfo {
    TrainInfo() : train_err(-1) {}
    TrainInfo(std::pair<double, size_t> train, double test, double dur,
              size_t bout, size_t bin);
    bool dummy() const { return train_err < 0; }
    double train_err, test_err, duration;
    size_t train_count, bytes_out, bytes_in;
};

//------------------------------------------------------------------------------
class MFNode {
   public:
    MFNode(unsigned node_index, std::shared_ptr<DataStore> node_data,
           const TripletVector<uint8_t> &test_set, bool modelshare,
           bool datashare, std::string outdir);
    ~MFNode();
    bool add_neighbour(unsigned rank);
    unsigned rank();
    void init_training(Communication *c, const HyperMFSGD &h,
                       ModelMergerType model, unsigned local = 1,
                       size_t steps_per_iteration = 30,
                       unsigned share_howmany = 20);
    TrainInfo train_and_share(int epoch);
    size_t receive(unsigned src, const std::vector<uint8_t> &data);
    size_t receive(unsigned src, const std::shared_ptr<ShareableModel> m);
    int finished_epoch();
    std::pair<bool, TrainInfo> trigger_epoch_if_ready(size_t degree);
    std::string summary();

   private:
    TripletVector<uint8_t> test_set_;
    std::set<unsigned> neighbours_;
    std::shared_ptr<DataStore> node_data_;
    std::shared_ptr<MFSGDDecentralized> trainer_;
    std::shared_ptr<ModelMerger> decentralized_sharing_;
    int finished_epoch_;
    unsigned local_iterations_, node_index_;
    bool modelshare_, datashare_;
    TimeProbeStats train_stats_, share_stats_, merging_stats_, inference_stats_;
    std::string outdir_;
    size_t bytes_reported_, bytes_in_;
#ifndef ENCLAVED
    std::shared_ptr<std::ofstream> logfile_;
#endif
};

//------------------------------------------------------------------------------
