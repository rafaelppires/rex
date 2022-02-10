#pragma once

#include <Eigen/Dense>
#include <Eigen/Sparse>

typedef Eigen::SparseMatrix<uint8_t> Ratings;
typedef Eigen::SparseMatrix<double> Sparse;
typedef Eigen::MatrixXd Dense;
using Eigen::Triplet;
template <typename T>
using TripletVector = std::vector<Triplet<T>>;

//------------------------------------------------------------------------------
template <typename S>
void sparse_matrix_outer_iterate(
    const S& matrix,
    std::function<void(typename S::InnerIterator, typename S::StorageIndex)>
        f) {
    if (!f) return;
    for (typename S::StorageIndex i = 0; i < matrix.outerSize(); ++i) {
        typename S::InnerIterator it(matrix, i);
        if (it) {
            f(it, i);
        }
    }
}

//------------------------------------------------------------------------------
template <typename S>
void sparse_matrix_iterate(
    const S& matrix, std::function<void(typename S::InnerIterator)> f,
    std::function<void(int)> f_endline = std::function<void(int)>()) {
    if (!f) return;
    for (int i = 0; i < matrix.outerSize(); ++i) {
        for (typename S::InnerIterator it(matrix, i); it; ++it) {
            f(it);
        }
        if (f_endline) f_endline(i);
    }
}

//------------------------------------------------------------------------------
template <typename T>
TripletVector<typename T::Scalar> matrix_to_triplets(const T& m) {
    typedef TripletVector<typename T::Scalar> ReturnType;
    ReturnType ret;
    ret.reserve(sizeof(typename ReturnType::value_type) * m.nonZeros());
    sparse_matrix_iterate(m, [&](typename T::InnerIterator it) {
        ret.emplace_back(it.row(), it.col(), it.value());
    });
    return ret;
}

//------------------------------------------------------------------------------
template <typename T>
void matrix_to_triplets_append(const T& m, std::vector<uint8_t>& out) {
    typedef typename TripletVector<typename T::Scalar>::value_type TripletType;
    sparse_matrix_iterate(m, [&](typename T::InnerIterator it) {
        size_t index = out.size();
        out.resize(index + sizeof(TripletType));
        new (&out[index]) TripletType(it.row(), it.col(), it.value());
    });
}

//------------------------------------------------------------------------------
template <typename T>
std::vector<uint8_t> triplets_to_raw(const TripletVector<T>& v) {
    uint8_t *beg = (uint8_t*)v.data(), *end = beg + sizeof(v[0]) * v.size();
    return std::vector<uint8_t>(beg, end);
}

//------------------------------------------------------------------------------
template <typename T>
TripletVector<T> raw_to_triplets(const std::vector<uint8_t>& data) {
    size_t n = data.size() / sizeof(Triplet<T>);
    const Triplet<T>* t = reinterpret_cast<const Triplet<T>*>(data.data());
    return TripletVector<T>(t, t + n);
}

//------------------------------------------------------------------------------
template <typename T, int Major = Eigen::ColMajor,
          typename Iterator = typename TripletVector<T>::const_iterator>
Eigen::SparseMatrix<T, Major> matrix_from_data(Iterator begin, Iterator end) {
    Eigen::SparseMatrix<T, Major> ret;
    size_t n = end - begin;
    int rows = 0, cols = 0;
    std::for_each(begin, end, [&](const Triplet<T>& t) {
        rows = std::max(rows, t.row() + 1);
        cols = std::max(cols, t.col() + 1);
    });
    ret.reserve(n);
    ret.resize(rows, cols);
    ret.setFromTriplets(begin, end);
    return ret;
}

//------------------------------------------------------------------------------
