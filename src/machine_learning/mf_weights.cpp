#include "mf_weights.h"

#include <iostream>

//------------------------------------------------------------------------------
// MWeights
//------------------------------------------------------------------------------
bool MFWeights::has(int i, const Sparse &m) const {
    return i < m.outerSize() && Sparse::InnerIterator(m, i);
    // column exists (i.e., not all 0)
}

//------------------------------------------------------------------------------
Embedding MFWeights::get_factors(int i, const Sparse &factors,
                                 const Sparse &bias) const {
    std::vector<double> ret;
    const auto &v = factors.col(i);
    int n = v.rows();
    for (int i = 0; i < n; ++i) ret.emplace_back(v.coeff(i, 0));
    return Embedding(bias.coeff(0, i), ret);
}

//------------------------------------------------------------------------------
bool MFWeights::has_item(int item) const { return has(item, items); }

//------------------------------------------------------------------------------
bool MFWeights::has_user(int user) const { return has(user, users); }

//------------------------------------------------------------------------------
Embedding MFWeights::get_item_factors(int item) const {
    return get_factors(item, items, item_biases);
}

//------------------------------------------------------------------------------
Embedding MFWeights::get_user_factors(int user) const {
    return get_factors(user, users, user_biases);
}

//------------------------------------------------------------------------------
double MFWeights::predict(int user, int item) const {
    if (user < 0 || item < 0) {
        std::cerr << "Invalid index (" << user << "," << item << ")"
                  << std::endl;
        abort();
    }
    assert(users.rows() == items.rows());
    return Sparse(users.col(user).transpose() * items.col(item)).coeff(0, 0) +
           user_biases.coeff(0, user) + item_biases.coeff(0, item);
}

//------------------------------------------------------------------------------
size_t MFWeights::estimate_serial_size() const {
    return serial_size(users) + serial_size(items) + serial_size(user_biases) +
           serial_size(item_biases);
}

//------------------------------------------------------------------------------
void MFWeights::serialize_with_size(const Sparse &matrix,
                                    std::vector<uint8_t> &out) const {
    size_t index = out.size();
    uint8_t *nptr = reinterpret_cast<uint8_t *>(&index);
    out.insert(out.end(), nptr, nptr + sizeof(index));  // placeholder
    matrix_to_triplets_append(matrix, out);
    assert(*reinterpret_cast<size_t *>(&out[index]) == index);  // check value
    size_t tmp = out.size() - index - sizeof(size_t);
    memcpy(&out[index], &tmp, sizeof(tmp));  // fill size in B
}

//------------------------------------------------------------------------------
void MFWeights::serialize_append(std::vector<uint8_t> &out) const {
    serialize_with_size(users, out);
    serialize_with_size(items, out);
    serialize_with_size(user_biases, out);
    serialize_with_size(item_biases, out);
}

//------------------------------------------------------------------------------
size_t MFWeights::deserialize_matrix(Sparse &matrix,
                                     const std::vector<uint8_t> &data,
                                     size_t offset) {
    typedef TripletVector<Sparse::Scalar>::value_type TripletType;
    const size_t *size = reinterpret_cast<const size_t *>(&data[offset]);
    offset += sizeof(size_t);
    const TripletType *begin =
        reinterpret_cast<const TripletType *>(&data[offset]);
    offset += *size;
    assert(offset <= data.size());
    const TripletType *end =
        reinterpret_cast<const TripletType *>(&data[offset]);
    matrix = matrix_from_data<Sparse::Scalar, Sparse::Options>(begin, end);
    return offset;
}

//------------------------------------------------------------------------------
size_t MFWeights::deserialize(const std::vector<uint8_t> &data, size_t offset) {
    assert(offset < data.size());
    offset = deserialize_matrix(users, data, offset);
    assert(offset < data.size());
    offset = deserialize_matrix(items, data, offset);
    assert(offset < data.size());
    offset = deserialize_matrix(user_biases, data, offset);
    assert(offset < data.size());
    offset = deserialize_matrix(item_biases, data, offset);
    assert(offset <= data.size());
    return offset;
}

//------------------------------------------------------------------------------
size_t MFWeights::serial_size(const Sparse &m) const {
    return m.nonZeros() *
           (sizeof(Sparse::Scalar) + 2 * sizeof(Sparse::StorageIndex));
}

//------------------------------------------------------------------------------
