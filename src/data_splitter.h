#pragma once

#include <matrices/matrices_common.h>

//------------------------------------------------------------------------------
template <typename T1, int Major>
void fill_matrix(Eigen::SparseMatrix<T1, Major> &m, std::pair<int, int> dim,
                 const TripletVector<T1> &data) {
    m.reserve(data.size());
    m.resize(dim.first, dim.second);
    m.setFromTriplets(data.begin(), data.end());
}

//------------------------------------------------------------------------------
bool read_data(const std::string &fname, Ratings &m,
               TripletVector<uint8_t> &test);
std::pair<int, int> read_and_split(const std::string &fname,
                                   TripletVector<uint8_t> &train,
                                   TripletVector<uint8_t> &test, int limit = -1,
                                   int filter_divisor = 0,
                                   int filter_modulo = -1);
