#include <model_merging/dpsgd.h>
#include <model_merging/random_model_walk.h>
#include <utils/time_probe.h>

#include <iostream>

//------------------------------------------------------------------------------
// TrainInfo
//------------------------------------------------------------------------------
TrainInfo::TrainInfo(std::pair<double, size_t> train, double test, double dur,
                     size_t bo, size_t bi)
    : train_err(sqrt(train.first / train.second)),
      train_count(train.second),
      test_err(test),
      duration(dur),
      bytes_out(bo),
      bytes_in(bi) {}

//------------------------------------------------------------------------------
// MFNode
//------------------------------------------------------------------------------
MFNode::MFNode(unsigned node_index, std::shared_ptr<DataStore> node_data,
               const TripletVector<uint8_t> &test_set, bool modelshare,
               bool datashare, std::string outdir)
    : node_index_(node_index),
      node_data_(node_data),
      test_set_(test_set),
      modelshare_(modelshare),
      datashare_(datashare),
      outdir_(outdir),
      bytes_reported_(0),
      bytes_in_(0),
      finished_epoch_(-1) {
    if (!modelshare_) {
        datashare_ = true;
    }
}
//------------------------------------------------------------------------------
unsigned MFNode::rank() { return node_index_; }

//------------------------------------------------------------------------------
bool MFNode::add_neighbour(unsigned rank) {
    return neighbours_.insert(rank).second;
}

//------------------------------------------------------------------------------
void MFNode::init_training(Communication *comm, const HyperMFSGD &h,
                           ModelMergerType model, unsigned local,
                           size_t steps_per_iteration, unsigned share_howmany) {
    trainer_ = std::make_shared<MFSGDDecentralized>(node_index_, node_data_, h,
                                                    steps_per_iteration);
    local_iterations_ = local;
    switch (model) {
        case RMW:
            decentralized_sharing_ =
                std::shared_ptr<ModelMerger>(new RandomModelWalkMerger(
                    node_index_, share_howmany, comm, trainer_, neighbours_,
                    modelshare_, datashare_));
            break;
        case DPSGD:
            decentralized_sharing_ = std::shared_ptr<ModelMerger>(
                new DPSGDMerger(node_index_, share_howmany, comm, trainer_,
                                neighbours_, modelshare_, datashare_));
            break;
        default:
            std::cerr << "Unknown model " << model << std::endl;
    }

#ifndef ENCLAVED
    std::string fname = outdir_ + "/" + std::to_string(node_index_) + ".dat";
    logfile_ = std::make_shared<std::ofstream>(fname);
    decentralized_sharing_->set_logfile(logfile_);
#endif
}

//------------------------------------------------------------------------------
TrainInfo MFNode::train_and_share(int epoch) {
    if (!trainer_) {
        std::cerr << "MFNode::train_and_share: no trainer" << std::endl;
        abort();
    }

    TimeProbe chrono;
    chrono.start();
    if (epoch > 0 && decentralized_sharing_) {
        merging_stats_.start();
        decentralized_sharing_->merge(epoch - 1);
        merging_stats_.stop();
    }

    std::pair<double, size_t> train;
    train_stats_.start();
    for (int i = 0; i < local_iterations_; ++i) {
        train = trainer_->train();
    }
    train_stats_.stop();

    size_t bytes_out;
    share_stats_.start();
    if (decentralized_sharing_) {
        bytes_out = decentralized_sharing_->share(epoch);
    }
    share_stats_.stop();

    inference_stats_.start();
    double test_err = trainer_->test(test_set_);
    inference_stats_.stop();

    trainer_->make_compressed();  // saves memory
    size_t bytes_in_report = bytes_in_ - bytes_reported_;
    bytes_reported_ += bytes_in_report;
    finished_epoch_ = epoch;
    return TrainInfo(train, test_err, chrono.stop(), bytes_out,
                     bytes_in_report);
}

//------------------------------------------------------------------------------
size_t MFNode::receive(unsigned src, const std::vector<uint8_t> &data) {
    ModelMergerType t = ShareableModel::extract_type(data);
    std::shared_ptr<ShareableModel> smodelptr(
        t == DPSGD ? new DPSGDShareableModel() : new ShareableModel());
    smodelptr->deserialize(data);
    if (!decentralized_sharing_) {
        std::cerr << "decentralized_sharing_ shold not be null" << std::endl;
        abort();
    }
    decentralized_sharing_->receive(src, smodelptr);
    /*if (t == DPSGD) {
        std::cout
            << "from: " << src << " d: "
            << reinterpret_cast<DPSGDShareableModel *>(smodelptr.get())->degree_
            << std::endl;
    }*/
    size_t ret = data.size();
    bytes_in_ += ret;
    return ret;
}

//------------------------------------------------------------------------------
size_t MFNode::receive(unsigned src, const std::shared_ptr<ShareableModel> m) {
    decentralized_sharing_->receive(src, m);
    return 0;
}

//------------------------------------------------------------------------------
std::pair<bool, TrainInfo> MFNode::trigger_epoch_if_ready(size_t degree) {
    TrainInfo info;
    bool trained = false;
    if (decentralized_sharing_->received_all(finished_epoch_, degree)) {
        info = train_and_share(finished_epoch_ + 1);
        trained = true;
    }
    return std::make_pair(trained, info);
}

//------------------------------------------------------------------------------
int MFNode::finished_epoch() { return finished_epoch_; }

//------------------------------------------------------------------------------
std::string MFNode::summary() {
    std::stringstream ss;
    ss << "event\tcount\taverage\tstdev\tsum\n"
       << "merging\t" << merging_stats_.summary() << "\n"
       << "train\t" << train_stats_.summary() << "\n"
       << "share\t" << share_stats_.summary() << "\n"
       << "test\t" << inference_stats_.summary() << std::endl;
    return ss.str();
}

//------------------------------------------------------------------------------
MFNode::~MFNode() {
#ifndef ENCLAVED
    if (logfile_) {
        *logfile_ << summary();
    }
#endif
}

//------------------------------------------------------------------------------
// ModelMerger
//------------------------------------------------------------------------------
ModelMerger::ModelMerger(unsigned rank, unsigned share_howmany,
                         Communication *c,
                         std::shared_ptr<MFSGDDecentralized> t,
                         std::set<unsigned> &n, bool modelshare, bool datashare)
    : userrank_(rank),
      share_howmany_(share_howmany),
      communication_(c),
      trainer_(t),
      neighbours_(n),
      modelshare_(modelshare),
      datashare_(datashare) {}

//------------------------------------------------------------------------------
void ModelMerger::receive(unsigned src, std::shared_ptr<ShareableModel> m) {
    std::unique_lock<std::mutex> lock(recv_mtx_);
    int epoch = m->epoch;
    if (recvdfrom_.insert(std::make_pair(src, epoch)).second) {
        received_models_[epoch].emplace_back(src, m);
    } else {
        std::cerr << "I am " << userrank_ << " and received a duplicate from "
                  << src << ". I currently have msgs from " << recvdfrom_.size()
                  << " neighbours" << std::endl;
        abort();
    }
}

//------------------------------------------------------------------------------
bool ModelMerger::received_all(int epoch, size_t howmany) {
    return received_models_[epoch].size() == howmany;
}

//------------------------------------------------------------------------------
#ifndef ENCLAVED
void ModelMerger::set_logfile(std::shared_ptr<std::ofstream> file) {
    logfile_ = file;
}
#endif

//------------------------------------------------------------------------------
SharingRatings ModelMerger::extract_ratings(unsigned howmany) {
    SharingRatings ret = std::make_shared<SharingRatings::element_type>();
    trainer_->extract_raw_ratings(userrank_, howmany, *ret);
    return ret;
}

//------------------------------------------------------------------------------
// ShareableModel
//------------------------------------------------------------------------------
ShareableModel::ShareableModel(int e, ModelMergerType t,
                               const MatrixFactorizationModel &m,
                               SharingRatings data)
    : epoch(e), type_(t), model_(m), rawdata(data) {}

//------------------------------------------------------------------------------
std::vector<uint8_t> ShareableModel::serialize() const {
    std::vector<uint8_t> ret(sizeof(type_) + sizeof(epoch));
    size_t datasize =
        rawdata ? (rawdata->size() * sizeof(TripletVector<uint8_t>::value_type))
                : 0;
    ret.reserve(ret.size() + model_.estimate_serial_size() + datasize);

    memcpy(ret.data(), &type_, sizeof(type_));
    memcpy(&ret[sizeof(type_)], &epoch, sizeof(epoch));
    model_.serialize_append(ret);

    size_t index = ret.size();
    uint8_t *nptr = reinterpret_cast<uint8_t *>(&index);
    ret.insert(ret.end(), nptr, nptr + sizeof(index));  // placeholder
    if (rawdata) {
        std::vector<uint8_t> data(triplets_to_raw(*rawdata));
        ret.insert(ret.end(), data.begin(), data.end());
    }
    assert(*reinterpret_cast<size_t *>(&ret[index]) == index);  // check value
    size_t tmp = ret.size() - index - sizeof(size_t);
    memcpy(&ret[index], &tmp, sizeof(tmp));  // fill size in B
    return ret;
}

//------------------------------------------------------------------------------
size_t ShareableModel::deserialize(const std::vector<uint8_t> &data) {
    type_ = extract_type(data);
    epoch = *reinterpret_cast<const int *>(&data[sizeof(type_)]);
    assert(epoch >= 0 && (type_ == RMW || type_ == DPSGD));
    size_t offset = model_.deserialize(data, sizeof(type_) + sizeof(epoch));
    assert(offset + sizeof(size_t) <= data.size());

    const size_t *size = reinterpret_cast<const size_t *>(&data[offset]);
    offset += sizeof(size_t);
    if (*size > 0) {
        typedef SharingRatings::element_type::value_type TripletType;
        const TripletType *begin =
            reinterpret_cast<const TripletType *>(&data[offset]);
        offset += *size;
        assert(offset <= data.size());
        const TripletType *end = begin + *size / sizeof(TripletType);
        rawdata = std::make_shared<SharingRatings::element_type>(begin, end);
    }
    return offset;
}

//------------------------------------------------------------------------------
ModelMergerType ShareableModel::extract_type(const std::vector<uint8_t> &data) {
    return *reinterpret_cast<const ModelMergerType *>(data.data());
}

//------------------------------------------------------------------------------
