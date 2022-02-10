#include <attestor.h>
#include <json_utils.h>
#include <sgx_cryptoall.h>
#ifdef ENCLAVED
#include <crypto_common.h>
#include <libc_mock/libcpp_mock.h>
#include <sgx_dcap_tvl.h>
#include <sgx_qve_errlist.h>
#include <sgx_utils.h>
#include "enclave_rex_t.h"
#endif

//------------------------------------------------------------------------------
std::vector<uint8_t> Attestor::get_key(const std::string &id) {
    if(attestees_[id].attested) {
        return attestees_[id].shared_key;
    }
    return std::vector<uint8_t>();
}

//------------------------------------------------------------------------------
std::string Attestor::new_challenge(const std::string &id,
                             const std::string &challenge) {
    std::vector<uint8_t> response = get_quote(id, challenge);
    if (!response.empty()) {
        return challenge_response_message(response);
    }
    return std::string();
}

//------------------------------------------------------------------------------
int Attestor::challenge_response(const std::string &id,
                                 const std::string &quote_b64) {
    std::string quote = Crypto::b64_decode(quote_b64);

    std::vector<uint8_t> peer_pubkey;
    if (check_mrenclave_and_get_ecdhpeer(quote, peer_pubkey) < 0) {
        std::cout << "\tBeware: I am different than peer " << id << std::endl;
        // return -2; // uncomment if all peers must be equal
    }

    sgx_ql_qe_report_info_t qve_report_info;
    sgx_status_t status =
        sgx_self_target(&qve_report_info.app_enclave_target_info);
    if (status != SGX_SUCCESS) {
        std::cerr << "sgx_self_target: failed" << std::endl;
        return -3;
    }

    int ret;
    auto rand_nonce = get_rand(sizeof(qve_report_info.nonce.rand));
    memcpy(&qve_report_info.nonce.rand[0], rand_nonce.data(),
           rand_nonce.size());

    time_t expiration_check_date;
    uint32_t collateral_expiration_status;
    sgx_ql_qv_result_t quote_verification_result;
    std::vector<uint8_t> supplemental_data;
    uint32_t supplemental_data_size = 1000;
    supplemental_data.resize(supplemental_data_size);
    status = ocall_verify_quote(
        &ret, (uint8_t *)quote.data(), quote.size(), &qve_report_info,
        &expiration_check_date, &collateral_expiration_status,
        &quote_verification_result, supplemental_data.data(),
        supplemental_data.size(), &supplemental_data_size);
    if (status != 0) {
        std::cerr << "Quote could not be verified. Returned: " << status
                  << std::endl;
        return -4;
    }

    if (supplemental_data_size > supplemental_data.size()) {
        std::cerr << "Supplemental data too big" << std::endl;
        return -5;
    } else {
        supplemental_data.resize(supplemental_data_size);
    }

    // Threshold of QvE ISV SVN. Get from
    // https://api.trustedservices.intel.com/sgx/certification/v2/qve/identity
    sgx_isv_svn_t qve_isvsvn_threshold = 3;
    quote3_error_t ret_verify = sgx_tvl_verify_qve_report_and_identity(
        (uint8_t *)quote.data(), quote.size(), &qve_report_info,
        expiration_check_date, collateral_expiration_status,
        quote_verification_result, supplemental_data.data(),
        supplemental_data.size(), qve_isvsvn_threshold);
    if (ret_verify != SGX_QL_SUCCESS) {
        std::cerr << "Could not verify report and/or identity of QE"
                  << std::endl;
        return -6;
    }

    ret = quote_check_result(quote_verification_result);

    // alternatively, positive values can be considered unsafe
    // because they mean some SW/HW update should be done
    if (attestees_[id].attested = ret >= 0) {
        attestees_[id].ecdh.peer_pubkey(peer_pubkey);
        attestees_[id].shared_key = attestees_[id].ecdh.derive_shared_key();
    }
    
    /*std::cout << id << " is " << (attestees_[id].attested ? "" : "not ")
              << "attested. Key: "
              << Crypto::b64_encode(attestees_[id].shared_key) << std::endl;*/

    return ret;
}

//------------------------------------------------------------------------------
int Attestor::check_mrenclave_and_get_ecdhpeer(const std::string &quote,
                                               std::vector<uint8_t> &peerpub) {
    const sgx_quote3_t *q = (const sgx_quote3_t *)quote.data();
    if (quote.size() < sizeof(sgx_quote3_t) ||
        q->signature_data_len > quote.size() - sizeof(sgx_quote_t)) {
        std::cerr << "Bad quote" << std::endl;
        return -1;
    }

    peerpub.clear();
    peerpub.push_back(0x04);
    const uint8_t *data = q->report_body.report_data.d;
    peerpub.insert(peerpub.end(), data, data + sizeof(sgx_report_data_t));

    const sgx_report_t *rep = sgx_self_report();
    if (memcmp(&q->report_body.mr_enclave, &rep->body.mr_enclave,
               sizeof(sgx_measurement_t)) != 0) {
        return -2;
    }

    return 0;
}

//------------------------------------------------------------------------------
int Attestor::quote_check_result(sgx_ql_qv_result_t result) {
    std::string msg, extra;
    qve_retrieve_error(result, msg, extra);
    int ret;
    switch (result) {
        case SGX_QL_QV_RESULT_OK:
            ret = 0;
            break;
        case SGX_QL_QV_RESULT_CONFIG_NEEDED:
        case SGX_QL_QV_RESULT_OUT_OF_DATE:
        case SGX_QL_QV_RESULT_OUT_OF_DATE_CONFIG_NEEDED:
        case SGX_QL_QV_RESULT_SW_HARDENING_NEEDED:
        case SGX_QL_QV_RESULT_CONFIG_AND_SW_HARDENING_NEEDED:
            /*std::cout
                << "\tWarning: App: Verification completed with Non-terminal "
                   "result: "
                << msg << std::endl;*/
            ret = 1;
            break;
        case SGX_QL_QV_RESULT_INVALID_SIGNATURE:
        case SGX_QL_QV_RESULT_REVOKED:
        case SGX_QL_QV_RESULT_UNSPECIFIED:
        default:
            /*std::cerr
                << "\tError: App: Verification completed with Terminal result: "
                << msg << std::endl;*/
            ret = -1;
            break;
    }
    return ret;
}
//------------------------------------------------------------------------------
std::vector<uint8_t> Attestor::get_quote(const std::string &id,
                                         const std::string &challenge) {
    // attestees_[id].ecdh.peer_pubkey(Crypto::b64_decode(challenge));
    sgx_target_info_t p_qe3_target;
    sgx_report_t report;
    sgx_report_data_t report_data = {0};
    int ret = -1;
    std::vector<uint8_t> response;
    sgx_status_t status = ocall_get_target_info(&ret, &p_qe3_target);
    if (status != SGX_SUCCESS || ret != 0) {
        std::cerr << "ocall_get_target_info status: " << status
                  << " ret: " << ret << std::endl;
        return response;
    }

    std::vector<uint8_t> ecdh_pub = attestees_[id].ecdh.pubkey_serialize();
    if (ecdh_pub[0] == 0x04) {
        ecdh_pub.erase(ecdh_pub.begin());
    } else {
        std::cerr << "First byte of ECDH pub is not 0x04" << std::endl;
    }

    memcpy(report_data.d, ecdh_pub.data(),
           std::min(sizeof(sgx_report_data_t), ecdh_pub.size()));
    sgx_create_report(&p_qe3_target, &report_data, &report);

    std::vector<uint8_t> localbuff(5000, 0);
    status = ocall_get_quote(&ret, &report, &localbuff[0], localbuff.size());
    if (status != SGX_SUCCESS || ret <= 0) {
        std::cerr << "ocall_get_quote status: " << status
                  << ". Response: " << ret << std::endl;
        return response;
    } else if (ret > localbuff.size()) {
        std::cerr << "ocall_get_quote buffer too small: " << localbuff.size()
                  << ". Response size: " << ret << std::endl;
        return response;
    }
    localbuff.resize(ret);
    return localbuff;
}

//------------------------------------------------------------------------------
bool Attestor::was_challenged(const std::string &id) {
    return attestees_[id].challenged;
}

//------------------------------------------------------------------------------
std::string Attestor::generate_challenge(const std::string &id) {
    attestees_[id].challenged = true;
    return "aloha";
}

//------------------------------------------------------------------------------
std::string Attestor::challenge_response_message(
    const std::vector<uint8_t> &quote) {
    json j;
    j[CHALLENGE_RESPONSE] = Crypto::b64_encode(quote);
    return j.dump();
}

//------------------------------------------------------------------------------
