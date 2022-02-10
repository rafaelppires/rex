#pragma once

#include <matrices/matrices_common.h>

#include <set>

#include "matrix_factorization.h"

//------------------------------------------------------------------------------
typedef Eigen::SparseMatrix<uint8_t, Eigen::RowMajor> RowMRatings;
typedef std::shared_ptr<TripletVector<uint8_t>> SharingRatings;
typedef std::map<std::pair<int, int>, int> DataStore;

class MFSGDDecentralized : public MFSGD {
   public:
    MFSGDDecentralized(unsigned node_index,
                    std::shared_ptr<DataStore> node_data,
                    HyperMFSGD h,
                    size_t steps_per_iteration);
    
    virtual std::pair<double, size_t> train();
    double test(const TripletVector<uint8_t>& testset);
    void extract_raw_ratings(unsigned userrank, unsigned howmany,
                             TripletVector<uint8_t>& dst);
    size_t add_raw_ratings(SharingRatings sr);
    MatrixFactorizationModel& mutable_model();
    void make_compressed();

   private:

    std::shared_ptr<DataStore> node_data_;
    unsigned node_index_;
    size_t steps_per_iteration_;
};

//------------------------------------------------------------------------------
