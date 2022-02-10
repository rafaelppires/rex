#pragma once

#include <vector>
#include "matrices_common.h"

class MatrixSerializer {
   public:
    virtual std::vector<uint8_t> serialize(const Dense &) = 0;
    virtual std::vector<uint8_t> serialize(const Sparse &) = 0;
    virtual Dense deserialize_dense(const std::vector<uint8_t>&) = 0;
    virtual Sparse deserialize_sparse(const std::vector<uint8_t>&) = 0;
};

class JsonSerializer : public MatrixSerializer {
   public:
    virtual std::vector<uint8_t> serialize(const Dense &);
    virtual std::vector<uint8_t> serialize(const Sparse &);
    virtual Dense deserialize_dense(const std::vector<uint8_t>&);
    virtual Sparse deserialize_sparse(const std::vector<uint8_t>&);
};

class BinarySerializer : public MatrixSerializer {
    virtual std::vector<uint8_t> serialize(const Dense &);
    virtual std::vector<uint8_t> serialize(const Sparse &);
    virtual Dense deserialize_dense(const std::vector<uint8_t>&);
    virtual Sparse deserialize_sparse(const std::vector<uint8_t>&);
};

//------------------------------------------------------------------------------
template <typename SparseMatrix>
SparseMatrix deserialize_sparse(const std::vector<uint8_t> &v) {
    typedef typename SparseMatrix::Scalar ValueType;
    TripletVector<ValueType> tv = raw_to_triplets<ValueType>(v);
    return matrix_from_data<ValueType>(tv.begin(), tv.end());
}

//------------------------------------------------------------------------------
