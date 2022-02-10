#pragma once

#include <ecdh.h>
#include <sgx_qve_header.h>
#include <map>
#include <string>
#include <vector>

#define ATTEST_CHALLENGE "attest_challenge"
#define CHALLENGE_RESPONSE "challenge_response"

struct AttestData {
    AttestData() : attested(false), challenged(false) {}
    bool attested, challenged;
    ECDH ecdh;
    std::vector<uint8_t> shared_key;
};

class Attestor {
   public:
    std::vector<uint8_t> get_quote(const std::string &id,
                                   const std::string &challenge);
    std::string new_challenge(const std::string &id,
                              const std::string &challenge);
    int challenge_response(const std::string &id, const std::string &quote);

    bool was_challenged(const std::string &id);
    std::string generate_challenge(const std::string &id);
    std::string challenge_response_message(const std::vector<uint8_t> &quote);

    std::vector<uint8_t> get_key(const std::string &id);

   private:
    int check_mrenclave_and_get_ecdhpeer(const std::string &quote,
                                         std::vector<uint8_t> &);
    int quote_check_result(sgx_ql_qv_result_t result);
    std::map<std::string, AttestData> attestees_;
};
