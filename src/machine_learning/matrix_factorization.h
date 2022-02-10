#pragma once

#include <model_merging/dpsgd_entry.h>

#include <memory>
#include <set>
#include <vector>

#include "mf_weights.h"

//------------------------------------------------------------------------------
class MFSGD;
class DPSGDEntry;
class MatrixFactorizationModel {
   public:
    typedef std::vector<DPSGDEntry> DegreesAndModels;

    MatrixFactorizationModel() = default;
    MatrixFactorizationModel(int rank);
    double predict(int user, int item) const;
    std::vector<int> recommend_user(int item, int how_many);
    std::vector<int> recommend_item(int user, int how_many);
    int rank() { return rank_; }
    const Sparse& user_features() { return weights_.users; }
    const Sparse& item_features() { return weights_.items; }
    double rmse(const TripletVector<uint8_t>& testset);
    void get_factors(int user, int item);

    void serialize_append(std::vector<uint8_t> &out) const;
    size_t deserialize(const std::vector<uint8_t> &data, size_t offset);
    void find_space(int user, int item, const Sparse& col = Sparse(),
                    double b = -1);

    // Merging models
    void merge_average(const MatrixFactorizationModel& w);
    void merge_weighted(size_t my_degree, const DegreesAndModels& models);

    void item_merge_column(Sparse& Y, const Sparse& Other);
    void user_merge_column(Sparse& X, const Sparse& Other);

    bool init_item(int item, const Sparse& column);
    bool init_user(int user, const Sparse& column);
    void prep_toshare();
    void make_compressed();
    size_t estimate_serial_size() const;

   private:
    Sparse zero_embedding();
    std::set<int> metropolis_hastings(size_t my_degree,
                                      const DegreesAndModels& models,
                                      Sparse& factors, Sparse& biases,
                                      bool isusers);
    std::set<int> metropolis_hastings_users(size_t my_degree,
                                            const DegreesAndModels& models);
    std::set<int> metropolis_hastings_items(size_t my_degree,
                                            const DegreesAndModels& models);

    std::pair<double, Sparse> combine_neighbors(bool isuser, int index,
                                                const DegreesAndModels& models);
    void combine_neighbors_embeddings(bool isuser,
                                      const DegreesAndModels& models,
                                      std::set<int>& exclude_list);
    void combine_neighbors_users(const DegreesAndModels& models,
                                 std::set<int>& exclude_list);
    void combine_neighbors_items(const DegreesAndModels& models,
                                 std::set<int>& exclude_list);

    int rank_;
    MFWeights weights_;
    friend class MFSGD;
};

//------------------------------------------------------------------------------
class HyperMFSGD {
   public:
    HyperMFSGD(int r, double lr, double rp, double ib, double ifact);
    int rank;
    double learning_rate, regularization_param, init_factor, init_bias;
    Sparse init_column_;
};

//------------------------------------------------------------------------------
class MFSGD {
   public:
    MFSGD(HyperMFSGD h);
    virtual std::pair<double, size_t> train() = 0;
    MatrixFactorizationModel model() const { return model_; }
    static MatrixFactorizationModel trainX(const Ratings& ratings,
                                           uint8_t lowscore, uint8_t highscore,
                                           int rank, double learning,
                                           double regularization,
                                           int iterations,
                                           const TripletVector<uint8_t>& test);

   protected:
    double train(int user, int item, double value);
    MatrixFactorizationModel model_;
    HyperMFSGD hyper_;

   private:
    MFWeights& weights_;
};

//------------------------------------------------------------------------------
