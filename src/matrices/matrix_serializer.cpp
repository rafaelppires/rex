#include "matrix_serializer.h"

#ifdef ENCLAVED
#include <libc_mock/libcpp_mock.h>
#include <json/json.hpp>
#else
#include <json/json-17.hpp>
#endif
using json = nlohmann::json;

//------------------------------------------------------------------------------
// JsonSerializer
//------------------------------------------------------------------------------
std::vector<uint8_t> JsonSerializer::serialize(const Dense &) {
    std::vector<uint8_t> ret;
    return ret;
}

//------------------------------------------------------------------------------
std::vector<uint8_t> JsonSerializer::serialize(const Sparse &inputmatrix) {
    json matrix, column;
    sparse_matrix_iterate(inputmatrix,
                          [&](Sparse::InnerIterator it) {
                              json line;
                              line[std::to_string(it.row())] = it.value();
                              column.emplace_back(line);
                          },
                          [&](int c) {
                              if (!column.empty()) {
                                  matrix[std::to_string(c)] = column;
                                  column.clear();
                              }
                          });
    std::string m = matrix.dump();
    return std::vector<uint8_t>(m.begin(), m.end());
}

//------------------------------------------------------------------------------
Dense JsonSerializer::deserialize_dense(const std::vector<uint8_t> &) {
    Dense ret;
    return ret;
}

//------------------------------------------------------------------------------
Sparse JsonSerializer::deserialize_sparse(const std::vector<uint8_t> &) {
    Sparse ret;
    return ret;
}

//------------------------------------------------------------------------------
// BinarySerializer
//------------------------------------------------------------------------------
std::vector<uint8_t> BinarySerializer::serialize(const Dense &) {
    std::vector<uint8_t> ret;
    return ret;
}

//------------------------------------------------------------------------------
std::vector<uint8_t> BinarySerializer::serialize(const Sparse &m) {
    return triplets_to_raw(matrix_to_triplets(m));
}

//------------------------------------------------------------------------------
Dense BinarySerializer::deserialize_dense(const std::vector<uint8_t> &) {
    Dense ret;
    return ret;
}

//------------------------------------------------------------------------------
Sparse BinarySerializer::deserialize_sparse(const std::vector<uint8_t> &v) {
    return ::deserialize_sparse<Sparse>(v);
}
//------------------------------------------------------------------------------
