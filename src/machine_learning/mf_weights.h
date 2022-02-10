#pragma once

#include <matrices/matrices_common.h>

typedef std::pair<double, std::vector<double>> Embedding;
class MFWeights {
   public:
    bool has_item(int item) const;
    bool has_user(int user) const;

    Embedding get_item_factors(int item) const;
    Embedding get_user_factors(int user) const;
    double predict(int user, int item) const;

    size_t estimate_serial_size() const;
    void serialize_append(std::vector<uint8_t>& out) const;
    size_t deserialize(const std::vector<uint8_t>& data, size_t offset);

    Sparse users, user_biases;
    Sparse items, item_biases;

   private:
    size_t serial_size(const Sparse&) const;
    size_t deserialize_matrix(Sparse &matrix,
                                     const std::vector<uint8_t> &data,
                                     size_t offset);
    void serialize_with_size(const Sparse &matrix,
                                    std::vector<uint8_t> &out) const;
    
    Embedding get_factors(int i, const Sparse& factors,
                          const Sparse& bias) const;
    bool has(int i, const Sparse& m) const;
};
