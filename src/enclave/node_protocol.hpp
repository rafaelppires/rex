#include <aes_utils.h>
#ifdef NATIVE
extern ssize_t ocall_send(const char *id, const void *buffer, size_t length);
#else
#include "enclave_rex_t.h"
#endif
//------------------------------------------------------------------------------
template <typename T>
size_t NodeProtocol::send(const std::string &dst, const T &data) {
    ssize_t ret = 0;
#ifdef NATIVE
    ret = ocall_send(dst.c_str(), data.data(), data.size());
#else
    ocall_send(&ret, dst.c_str(), data.data(), data.size());
#endif
    return ret;
}

#ifndef NATIVE
//------------------------------------------------------------------------------
template <typename T>
size_t NodeProtocol::encrypted_send(const std::string &dst, const T &data) {
    ssize_t ret = 0;
    auto key = attestor_.get_key(dst);
    if (key.empty()) return ret;

    return send(dst, Crypto::encrypt_aesgcm(key, data));
}

//------------------------------------------------------------------------------
template <typename T>
std::pair<bool, std::vector<uint8_t>> NodeProtocol::decrypt_received(
    const std::string &src, const T &data) {
    std::pair<bool, std::vector<uint8_t>> ret(false, std::vector<uint8_t>());
    auto key = attestor_.get_key(src);
    if (key.empty()) return ret;

    auto plain = Crypto::decrypt_aesgcm(key, data);
    ret.first = plain.first;
    if (ret.first) {
        ret.second.insert(ret.second.end(), plain.second.begin(),
                          plain.second.end());
    }
    return ret;
}

//------------------------------------------------------------------------------
#endif
