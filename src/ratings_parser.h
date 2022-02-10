#pragma once
#include <Eigen/Sparse>
#include <vector>

template <typename T>
using TripletVector = std::vector<Eigen::Triplet<T>>;

bool csv_ratings_triplet_vector(const std::string fname,
                                TripletVector<uint8_t>& v);
bool csv_ratings_sparse_matrix(const std::string fname,
                               Eigen::SparseMatrix<uint8_t>& m);
bool csv_bucketize_byuser(const std::string& fname);
bool csvitem_tovector(TripletVector<uint8_t>& v,
                      const std::vector<std::string>& item,
                      std::pair<int, int>& dim, int limit = -1,
                      int filter_divisor = 0, int filter_modulo = -1);
