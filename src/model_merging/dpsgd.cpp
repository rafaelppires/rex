#include "dpsgd.h"

#include "dpsgd_entry.h"

//------------------------------------------------------------------------------
// DPSGDMerger
//------------------------------------------------------------------------------
DPSGDMerger::DPSGDMerger(unsigned rank, unsigned share_howmany,
                         Communication *c,
                         std::shared_ptr<MFSGDDecentralized> trainer,
                         std::set<unsigned> &neighbours, 
                         bool modelshare, bool datashare)
    : ModelMerger(rank, share_howmany, c, trainer, neighbours, modelshare, datashare) {}

//------------------------------------------------------------------------------
size_t DPSGDMerger::share(int epoch) {
    SharingRatings rawdata;
    if (datashare_) {
        rawdata = extract_ratings(share_howmany_);
    }

    DPSGDModelPtr toshare = std::make_shared<DPSGDShareableModel>(
        epoch, modelshare_? trainer_->model(): MatrixFactorizationModel(-2), // -2 for no model sharing
        rawdata, neighbours_.size());

    size_t ret = 0;
    for (auto &peer : neighbours_) {
        ret += communication_->send(userrank_, peer, toshare);
    }
    return ret;
}

//------------------------------------------------------------------------------
void DPSGDMerger::merge(int epoch) {
    std::unique_lock<std::mutex> lock(recv_mtx_);
    std::vector<DPSGDEntry> models;
    size_t count = 0;
    for (auto &m : received_models_[epoch]) {
        unsigned src = m.first;
        ShareableModelPtr shared = m.second;
        const auto &model =
            reinterpret_cast<DPSGDShareableModel *>(shared.get());
        models.emplace_back(src, model->degree_, shared->epoch, shared->model_);
        count += trainer_->add_raw_ratings(shared->rawdata);
        recvdfrom_.erase(std::make_pair(src, shared->epoch));
    }

    if (neighbours_.size() != models.size()) {
        std::cerr << "oh no! I am " << userrank_ << ". I have "
                  << neighbours_.size() << " neighbours: "
                  << " but received " << models.size() << " models."
                  << std::endl;
        for (const auto &n : neighbours_) std::cerr << n << " ";
        std::cerr << std::endl;
        for (const auto &m : models)
            std::cerr << m.src << "(" << m.epoch << ") ";
        std::cerr << std::endl;

        abort();
    }

    if(modelshare_) {
        auto &local_model = trainer_->mutable_model();
        local_model.merge_weighted(neighbours_.size(), models);
    }

    received_models_[epoch].clear();
}

//------------------------------------------------------------------------------
// DPSGDShareableModel
//------------------------------------------------------------------------------
DPSGDShareableModel::DPSGDShareableModel(int e,
                                         const MatrixFactorizationModel &m,
                                         SharingRatings data, size_t degree)
    : ShareableModel(e, DPSGD, m, data), degree_(degree) {
    // puts user embedding into its place
    //model_.prep_toshare();
    // call not needed now, training occurs at original place
}
//------------------------------------------------------------------------------
std::vector<uint8_t> DPSGDShareableModel::serialize() const {
    std::vector<uint8_t> ret(ShareableModel::serialize());
    const uint8_t *dptr = reinterpret_cast<const uint8_t *>(&degree_);
    ret.insert(ret.end(), dptr, dptr + sizeof(degree_));
    return ret;
}

//------------------------------------------------------------------------------
size_t DPSGDShareableModel::deserialize(const std::vector<uint8_t> &data) {
    size_t offset = ShareableModel::deserialize(data);
    assert(offset < data.size() && data.size() - offset >= size_t(degree_));
    memcpy(&degree_, &data[offset], sizeof(degree_));
    assert(degree_ != 0);
    return offset + sizeof(degree_);
}

//------------------------------------------------------------------------------
