#include "matrix_factorization.h"
#include <utils/time_probe.h>

#include <iostream>

//------------------------------------------------------------------------------
// Model
//------------------------------------------------------------------------------
MatrixFactorizationModel::MatrixFactorizationModel(int rank) : rank_(rank) {}

//------------------------------------------------------------------------------
double MatrixFactorizationModel::predict(int user, int item) const {
    return weights_.predict(user, item);
}

//------------------------------------------------------------------------------
double MatrixFactorizationModel::rmse(const TripletVector<uint8_t> &testset) {
    size_t count = 0;
    double sumofsquares = 0;
    for (auto &t : testset) {
        double diff = t.value() - predict(t.row(), t.col());
        sumofsquares += diff * diff;
        ++count;
    }
    return sqrt(sumofsquares / count);
}

//------------------------------------------------------------------------------
bool MatrixFactorizationModel::init_item(int item, const Sparse &column) {
    if (!weights_.has_item(item)) {
        find_space(0, item);
        weights_.items.col(item) = column;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
bool MatrixFactorizationModel::init_user(int user, const Sparse &column) {
    if (!weights_.has_user(user)) {
        find_space(user, 0);
        weights_.users.col(user) = column;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
Sparse MatrixFactorizationModel::zero_embedding() {
    Sparse ret;
    ret.reserve(rank_);
    ret.resize(rank_, 1);
    ret.col(0) = Eigen::MatrixXd::Zero(rank_, 1).sparseView();
    return ret;
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::make_compressed() {
    weights_.users.makeCompressed();
    weights_.items.makeCompressed();
    weights_.user_biases.makeCompressed();
    weights_.item_biases.makeCompressed();
}

//------------------------------------------------------------------------------
// Iterates through Other and average its non-zero columns with Y, i.e.,
// Y.col(i) = average(Y.col(i), Other.col(i))
//------------------------------------------------------------------------------
void MatrixFactorizationModel::item_merge_column(Sparse &Y,
                                                 const Sparse &Other) {
    sparse_matrix_outer_iterate(Other, [&](Sparse::InnerIterator it, int i) {
        int item = it.col();
        if (!init_item(item, Other.col(item))) {
            Y.col(item) = (Y.col(item) + Other.col(item)) / 2.;
        }
    });
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::user_merge_column(Sparse &X,
                                                 const Sparse &Other) {
    sparse_matrix_outer_iterate(Other, [&](Sparse::InnerIterator it, int i) {
        int user = it.col();
        if (!init_user(user, Other.col(user))) {
            X.col(user) = (X.col(user) + Other.col(user)) / 2.;
        }
    });
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::merge_average(
    const MatrixFactorizationModel &m) {
    item_merge_column(weights_.items, m.weights_.items);
    item_merge_column(weights_.item_biases, m.weights_.item_biases);

    user_merge_column(weights_.users, m.weights_.users);
    user_merge_column(weights_.user_biases, m.weights_.user_biases);
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::prep_toshare() {
    init_user(rank_, weights_.users.col(0));
    weights_.users.col(rank_) = weights_.users.col(0);
    weights_.users.col(0) = Eigen::MatrixXd::Zero(rank_, 1).sparseView();
    weights_.user_biases.coeffRef(0, rank_) = weights_.user_biases.coeff(0, 0);
    weights_.user_biases.coeffRef(0, 0) = 0;
}

//------------------------------------------------------------------------------
// Per-embedding Metropolis-Hastings averaging (D-PSGD)
//------------------------------------------------------------------------------
std::set<int> MatrixFactorizationModel::metropolis_hastings(
    size_t my_degree, const DegreesAndModels &models, Sparse &factors,
    Sparse &biases, bool isusers) {
    std::set<int> ret;
    sparse_matrix_outer_iterate(factors, [&](Sparse::InnerIterator it, int i) {
        int index = it.col();
        double sum_weights = 0;
        Sparse embedding(zero_embedding()), bias;
        bias.resize(1, 1);
        bias.coeffRef(0, 0) = 0;
        std::vector<size_t> degrees;
        for (const auto &model : models) {
            size_t degree = model.degree;
            const MatrixFactorizationModel &nmodel = model.model;
            if (isusers ? nmodel.weights_.has_user(index)
                        : nmodel.weights_.has_item(index)) {
                double weight = 1. / (1 + std::max(my_degree, degree));
                embedding.col(0) +=
                    weight * (isusers ? nmodel.weights_.users.col(index)
                                      : nmodel.weights_.items.col(index));
                bias.coeffRef(0, 0) +=
                    weight *
                    (isusers ? nmodel.weights_.user_biases.coeff(0, index)
                             : nmodel.weights_.item_biases.coeff(0, index));
                sum_weights += weight;
                degrees.emplace_back(degree);
            }
        }
        if (sum_weights > 1.0) {
            std::cerr << "my rank: " << rank_ << " idx: " << index << std::endl;
            std::cerr << "Sum of weights > 1: " << sum_weights << std::endl;
            std::cerr << my_degree << " (" << degrees.size() << ") - ";
            for (auto &d : degrees) std::cerr << d << " ";
            std::cerr << std::endl;
            abort();
        }
        double my_weight = 1. - sum_weights;
        factors.col(index) *= my_weight;
        biases.coeffRef(0, index) *= my_weight;
        factors.col(index) += embedding.col(0);
        biases.coeffRef(0, index) += bias.coeff(0, 0);
        ret.insert(index);
    });
    return ret;
}

//------------------------------------------------------------------------------
std::set<int> MatrixFactorizationModel::metropolis_hastings_users(
    size_t my_degree, const DegreesAndModels &models) {
    return metropolis_hastings(my_degree, models, weights_.users,
                               weights_.user_biases, true);
}

//------------------------------------------------------------------------------
std::set<int> MatrixFactorizationModel::metropolis_hastings_items(
    size_t my_degree, const DegreesAndModels &models) {
    return metropolis_hastings(my_degree, models, weights_.items,
                               weights_.item_biases, false);
}

//------------------------------------------------------------------------------
std::pair<double, Sparse> MatrixFactorizationModel::combine_neighbors(
    bool isuser, int index, const DegreesAndModels &models) {
    struct Entry {
        Entry(unsigned d, Sparse c, double b) : degree(d), col(c), bias(b) {}
        unsigned degree;
        Sparse col;
        double bias;
    };
    std::vector<Entry> embs;  // degree and embedding
    for (const auto &neigh : models) {
        const MatrixFactorizationModel &nmodel = neigh.model;
        if (isuser ? nmodel.weights_.has_user(index)
                   : nmodel.weights_.has_item(index)) {
            embs.emplace_back(
                neigh.degree,
                isuser ? nmodel.weights_.users.col(index)
                       : nmodel.weights_.items.col(index),
                isuser ? nmodel.weights_.user_biases.coeff(0, index)
                       : nmodel.weights_.item_biases.coeff(0, index));
        }
    }

    Sparse embedd(zero_embedding());
    double sum_of_inverses = 0, bias = 0;
    for (const auto &e : embs) sum_of_inverses += 1.0 / e.degree;
    for (const auto &e : embs) {
        double w = (1.0 / (1 + e.degree * (sum_of_inverses - 1.0 / e.degree)));
        embedd.col(0) += w * e.col;
        bias += w * e.bias;
    }
    return std::make_pair(bias, embedd);
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::combine_neighbors_embeddings(
    bool isuser, const DegreesAndModels &models, std::set<int> &exclude_list) {
    for (const auto &neigh : models) {
        sparse_matrix_outer_iterate(
            isuser ? neigh.model.weights_.users : neigh.model.weights_.items,
            [&](Sparse::InnerIterator it, int i) {
                int index = it.col();
                if (exclude_list.insert(index).second) {
                    auto combined = combine_neighbors(isuser, index, models);
                    if (isuser) {
                        init_user(index, combined.second);
                        weights_.user_biases.coeffRef(0, index) =
                            combined.first;
                    } else {
                        init_item(index, combined.second);
                        weights_.item_biases.coeffRef(0, index) =
                            combined.first;
                    }
                }
            });
    }
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::combine_neighbors_users(
    const DegreesAndModels &models, std::set<int> &exclude_list) {
    combine_neighbors_embeddings(true, models, exclude_list);
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::combine_neighbors_items(
    const DegreesAndModels &models, std::set<int> &exclude_list) {
    combine_neighbors_embeddings(false, models, exclude_list);
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::merge_weighted(size_t my_degree,
                                              const DegreesAndModels &models) {
    // first update embeddings that this node's model already has
    std::set<int> users_done = metropolis_hastings_users(my_degree, models),
                  items_done = metropolis_hastings_items(my_degree, models);

    // then, initialize those that it does not have (combining neighbours)
    combine_neighbors_users(models, users_done);
    combine_neighbors_items(models, items_done);
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::find_space(int user, int item, const Sparse &col,
                                          double b) {
    auto &Y = weights_.items, &B = weights_.item_biases;
    auto &X = weights_.users, &A = weights_.user_biases;
    if (item >= Y.cols()) {
        Y.conservativeResize(rank_, item + 1);
        B.conservativeResize(1, item + 1);
        if (col.outerSize() > 0) Y.col(item) = col;
        if (b > 0) B.coeffRef(0, item) = b;
    }
    if (user >= X.cols()) {
        X.conservativeResize(rank_, user + 1);
        A.conservativeResize(1, user + 1);
        if (col.outerSize() > 0) X.col(user) = col;
        if (b > 0) A.coeffRef(0, user) = b;
    }
}

//------------------------------------------------------------------------------
size_t MatrixFactorizationModel::estimate_serial_size() const {
    return sizeof(rank_) + weights_.estimate_serial_size();
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::serialize_append(
    std::vector<uint8_t> &out) const {
    const uint8_t *rptr = reinterpret_cast<const uint8_t *>(&rank_);
    out.insert(out.end(), rptr, rptr + sizeof(rank_));
    weights_.serialize_append(out);
}

//------------------------------------------------------------------------------
size_t MatrixFactorizationModel::deserialize(const std::vector<uint8_t> &data,
                                             size_t offset) {
    rank_ = *reinterpret_cast<const int *>(&data[offset]);
    return weights_.deserialize(data, offset + sizeof(rank_));
}

//------------------------------------------------------------------------------
// Hyperparameters
//------------------------------------------------------------------------------
HyperMFSGD::HyperMFSGD(int r, double lr, double rp, double ib, double ifact)
    : rank(r),
      learning_rate(lr),
      regularization_param(rp),
      init_factor(ifact),
      init_bias(ib) {
#if 0
    init_column_ = Sparse(Dense::Constant(r, 1, ifact).sparseView());
#else  // random
    TripletVector<double> column;
    for (int i = 0; i < r; ++i) {
        column.emplace_back(i, 0, ifact * double(rand() % 10000) / 10000);
    }
    Sparse m(r, 1);
    m.setFromTriplets(column.begin(), column.end());
    init_column_ = m;
#endif
}

//------------------------------------------------------------------------------
// Training
//------------------------------------------------------------------------------
MFSGD::MFSGD(HyperMFSGD h)
    : hyper_(h), model_(h.rank), weights_(model_.weights_) {}

//------------------------------------------------------------------------------
double MFSGD::train(int user, int item, double value) {
    auto &Y = weights_.items, &B = weights_.item_biases;
    auto &X = weights_.users, &A = weights_.user_biases;
    double lambda = hyper_.regularization_param, eta = hyper_.learning_rate;
#if 1  // P. 9 https://www.inf.u-szeged.hu/~jelasity/cikkek/dmle19.pdf
    double err = value - model_.predict(user, item), step = eta * err,
           mult = 1 - eta * lambda;
    Y.col(item) = mult * Y.col(item) + step * X.col(user);
    B.coeffRef(0, item) += step;
    X.col(user) = mult * X.col(user) + step * Y.col(item);
    A.coeffRef(0, user) += step;
#else  // https://blog.insightdatascience.com/explicit-matrix-factorization-als-sgd-and-all-that-jazz-b00e4d9b21ea
    double err = value - model_.predict(user, item);
    B.coeffRef(0, item) += eta * (err - lambda * B.coeffRef(0, item));
    A.coeffRef(0, user) += eta * (err - lambda * A.coeffRef(0, user));
    X.col(user) += eta * (err * Y.col(item) - lambda * X.col(user));
    Y.col(item) += eta * (err * X.col(user) - lambda * Y.col(item));
#endif
    return err * err;
}

//------------------------------------------------------------------------------
void MatrixFactorizationModel::get_factors(int user, int item) {
    /*
        std::cout << "u" << user << " i" << item << std::endl;
        const auto &ufs = weights_.get_user_factors(user);
        std::cout << ufs.first << " - ";
        for (const auto &uf : ufs.second) {
            std::cout << uf << " ";
        }
        std::cout << std::endl;
        const auto &ifs = weights_.get_item_factors(item);
        std::cout << ifs.first << " - ";
        for (const auto &itf : ifs.second) {
            std::cout << itf << " ";
        }
        std::cout << std::endl;
    */
}

//------------------------------------------------------------------------------
