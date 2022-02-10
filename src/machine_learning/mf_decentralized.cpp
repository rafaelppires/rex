#include "mf_decentralized.h"
#include <random>
#include <iostream>
//------------------------------------------------------------------------------
// MFSGDDecentralized
//------------------------------------------------------------------------------
MFSGDDecentralized::MFSGDDecentralized(unsigned node_index,
                    std::shared_ptr<DataStore> node_data,
                    HyperMFSGD h,
                    size_t steps_per_iteration)
    : MFSGD(h), node_index_(node_index), node_data_(node_data), steps_per_iteration_(steps_per_iteration) {}
//------------------------------------------------------------------------------
MatrixFactorizationModel& MFSGDDecentralized::mutable_model() { return model_; }

//------------------------------------------------------------------------------
void MFSGDDecentralized::make_compressed() {
    model_.make_compressed();
}

//------------------------------------------------------------------------------
std::pair<double, size_t> MFSGDDecentralized::train() {
    double total_err = 0;
    size_t count = 0;
    size_t num_local_steps = std::min(steps_per_iteration_, node_data_->size()); //see local data only once

    std::set<unsigned> train_indices;
#ifndef ENCLAVED
    std::default_random_engine generator;
    std::uniform_int_distribution<int> distribution(0, node_data_->size()-1);
#endif
    
    while(train_indices.size() < num_local_steps) {
#ifdef ENCLAVED
        train_indices.insert(rand() % node_data_->size());
#else
        train_indices.insert(distribution(generator));
#endif
    }

    int i = 0;
    for(const auto& x: *node_data_) {
        if(train_indices.find(i) != train_indices.end()) {
            int user = x.first.first, item = x.first.second;
            assert(x.second >= 0 && x.second <= 10);

            model_.find_space(user, item, hyper_.init_column_,
                                                hyper_.init_bias);
            total_err += MFSGD::train(user, item, x.second);
            ++count;
        }
        i++;
        if(count == num_local_steps)
            break;
    }

    return std::make_pair(total_err, count);
}

//------------------------------------------------------------------------------
double MFSGDDecentralized::test(const TripletVector<uint8_t> &testset) {
    for (const auto &t : testset) {
        model_.init_user(t.row(), hyper_.init_column_);
        model_.init_item(t.col(), hyper_.init_column_); 
    }
    return model_.rmse(testset);
}

//------------------------------------------------------------------------------
size_t MFSGDDecentralized::add_raw_ratings(SharingRatings sr) {
    size_t count = 0;
    if (sr) {
        for(auto it = sr->begin(); it != sr->end(); it++) {
            auto res = node_data_->insert(std::make_pair(std::make_pair(it->row(), it->col()), it->value()));
            if(res.second)
                count++;
        }
    }
    return count;
}

//------------------------------------------------------------------------------
void MFSGDDecentralized::extract_raw_ratings(unsigned userrank,
                                             unsigned howmany,
                                             TripletVector<uint8_t> &dst) {
    std::set<unsigned> sharing_indices;
    std::default_random_engine generator;
    std::uniform_int_distribution<int> distribution(0, node_data_->size()-1);
    
    howmany = std::min(howmany, (unsigned) node_data_->size());

    while(sharing_indices.size() < howmany) {
        sharing_indices.insert(distribution(generator));
    }

    int i = 0, count = 0;
    for(const auto& x: *node_data_) {
        if(sharing_indices.find(i) != sharing_indices.end()) {
            int user = x.first.first, item = x.first.second;
            assert(x.second >= 0 && x.second <= 10);
            dst.emplace_back(user, item, x.second);
            ++count;
        }  
        i++;
        if(count == howmany)
            break;
    }
}

//------------------------------------------------------------------------------
