#include "random_model_walk.h"

//------------------------------------------------------------------------------
// RandomModelWalkMerger
//------------------------------------------------------------------------------
RandomModelWalkMerger::RandomModelWalkMerger(
    unsigned rank, unsigned share_howmany, Communication *c,
    std::shared_ptr<MFSGDDecentralized> trainer, std::set<unsigned> &neighbours,
    bool modelshare, bool datashare)
    : ModelMerger(rank, share_howmany, c, trainer, neighbours, modelshare,
                  datashare) {}

//------------------------------------------------------------------------------
size_t RandomModelWalkMerger::share(int epoch) {
    SharingRatings rawdata;
    if (datashare_) {
        rawdata = extract_ratings(share_howmany_);
    }
    ShareableModelPtr toshare = std::make_shared<ShareableModel>(
                          epoch, RMW,
                          modelshare_ ? trainer_->model()
                                      : MatrixFactorizationModel(
                                            -2),  //-2 when no model sharing
                          rawdata),
                      dummy = std::make_shared<ShareableModel>(
                          epoch, RMW,
                          MatrixFactorizationModel(-1),  // -1 for dummy
                          SharingRatings());

    // Choose a random neighbor to send
    unsigned peer = rand() % neighbours_.size(), i = 0;
    size_t ret = 0;
    for (const auto &n : neighbours_) {
        if (i == peer) {
            ret += communication_->send(userrank_, n, toshare);
        } else {
            ret += communication_->send(userrank_, n, dummy);
        }
        ++i;
    }

    return ret;
}

//------------------------------------------------------------------------------
void RandomModelWalkMerger::merge(int epoch) {
    std::unique_lock<std::mutex> lock(recv_mtx_);

    size_t count = 0;
    for (auto &model : received_models_[epoch]) {
        unsigned src = model.first;
        ShareableModelPtr shared = model.second;
        if (shared->model_.rank() != -1) {
            if (modelshare_) {  // or shared->model_.rank() != -2
                trainer_->mutable_model().merge_average(shared->model_);
            }
            count += trainer_->add_raw_ratings(shared->rawdata);
        }  // else it's a dummy. Used for synchronization
        recvdfrom_.erase(std::make_pair(src, shared->epoch));
    }

    received_models_[epoch].clear();
}

//------------------------------------------------------------------------------
