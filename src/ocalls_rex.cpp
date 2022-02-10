#ifndef NATIVE
#include <enclave_rex_u.h>
#include <sgx_qe_errlist.h>
#include <sgx_qv_errlist.h>
#include "sgx_dcap_quoteverify.h"
#endif
#include <communication/sync_zmq.h>
#include <stdio.h>
#include <iostream>
#include <map>
#include <mutex>

#define BLD "\x1B[1m"
#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define MAG "\x1B[35m"
#define CYN "\x1B[36m"
#define WHT "\x1B[37m"
#define RST "\x1B[0m"

//------------------------------------------------------------------------------
/* OCall functions */
//------------------------------------------------------------------------------
std::mutex pmutex;
void uprint(const char *str) {
    /* Proxy/Bridge will check the length and null-terminate
     * the input string to prevent buffer overflow.
     */
    std::lock_guard<std::mutex> lock(pmutex);
    printf(YEL "%s" RST, str);
    fflush(stdout);
}
//------------------------------------------------------------------------------
ssize_t ocall_send(const char *id, const void *buffer, size_t length) {
    return CommunicationZmq::send(id, buffer, length);
}

//------------------------------------------------------------------------------
extern void ctrlc_handler(int s);
void ocall_farewell() { ctrlc_handler(0); }

//------------------------------------------------------------------------------
#ifndef NATIVE
struct TargetInfo {
    TargetInfo() : done(false) {}
    sgx_target_info_t qe_target_info;
    bool done;
} target;

//------------------------------------------------------------------------------
void qe3_error(const std::string &func, quote3_error_t qe3_ret) {
    std::string error, extra;
    retrieve_error_msg(qe3_ret, error, extra);
    std::cerr << func << ": " << error << " " << extra << std::endl;
}

//------------------------------------------------------------------------------
int ocall_get_target_info(sgx_target_info_t *p_qe3_target) {
    quote3_error_t qe3_ret = SGX_QL_SUCCESS;

    if (!target.done) {
        qe3_ret = sgx_qe_get_target_info(&target.qe_target_info);
        if (SGX_QL_SUCCESS != qe3_ret) {
            qe3_error("sgx_qe_get_target_info", qe3_ret);
            return -1;
        } else {
            target.done = true;
        }
    }
    memcpy(p_qe3_target, &target.qe_target_info, sizeof(sgx_target_info_t));
    return 0;
}

//------------------------------------------------------------------------------
int ocall_get_quote(sgx_report_t *report, uint8_t *quote_buff,
                    size_t buff_size) {
    uint32_t quote_size = 0;
    quote3_error_t qe3_ret = sgx_qe_get_quote_size(&quote_size);
    if (SGX_QL_SUCCESS != qe3_ret) {
        qe3_error("in sgx_qe_get_quote_size", qe3_ret);
        return -1;
    }

    if (buff_size < quote_size) {
        std::cerr << "Quote (" << quote_size
                  << "B) doesn't fit in the buffer provided (" << buff_size
                  << "B)" << std::endl;
        return -2;
    }

    memset(quote_buff, 0, buff_size);

    // Get the Quote
    qe3_ret = sgx_qe_get_quote(report, quote_size, quote_buff);
    if (SGX_QL_SUCCESS != qe3_ret) {
        qe3_error("sgx_qe_get_quote", qe3_ret);
        return -3;
    }
    return quote_size;
}

//------------------------------------------------------------------------------
int ocall_verify_quote(const uint8_t *quote_data, size_t quote_size,
                       sgx_ql_qe_report_info_t *qve_report_info,
                       time_t *current_time,
                       uint32_t *collateral_expiration_status,
                       sgx_ql_qv_result_t *quote_verification_result,
                       uint8_t *supplemental_data, uint32_t buff_size,
                       uint32_t *supplemental_data_size) {
    quote3_error_t dcap_ret =
        sgx_qv_get_quote_supplemental_data_size(supplemental_data_size);
    if (dcap_ret != SGX_QL_SUCCESS) {
        printf(
            "\tError: sgx_qv_get_quote_supplemental_data_size failed: 0x%04x\n",
            dcap_ret);
        return -1;
    } else if (*supplemental_data_size > buff_size) {
        std::cerr << "Supplemental data (" << *supplemental_data_size
                  << ") does not fit in provided buffer (" << buff_size << ")"
                  << std::endl;
    }

    // set current time. This is only for sample purposes, in production mode a
    // trusted time should be used.
    *current_time = time(NULL);
    dcap_ret = sgx_qv_verify_quote(quote_data, (uint32_t)quote_size, NULL,
                                   *current_time, collateral_expiration_status,
                                   quote_verification_result, qve_report_info,
                                   *supplemental_data_size, supplemental_data);
    if (dcap_ret != SGX_QL_SUCCESS) {
        std::string error, extra;
        qv_retrieve_error(dcap_ret, error, extra);
        std::cerr << "sgx_qv_verify_quote: " << error << " " << extra
                  << std::endl;
        return -2;
    }
    return 0;
}
#endif
//------------------------------------------------------------------------------
#include <utils/time_probe.h>
std::map<std::string, TimeProbe> time_probes;
std::mutex tp_mutex;
void ocall_start_timer(const char *hash) {
    std::lock_guard<std::mutex> lock(tp_mutex);
    time_probes[hash].start();
}

//------------------------------------------------------------------------------
double ocall_stop_timer(const char *hash) {
    std::lock_guard<std::mutex> lock(tp_mutex);
    return time_probes[hash].stop();
}

//------------------------------------------------------------------------------
std::map<std::string, TimeProbeStats> timeprobe_stats;
std::mutex tps_mutex;
void ocall_statsprobe_start(const char *hash) {
    std::lock_guard<std::mutex> lock(tps_mutex);
    timeprobe_stats[hash].start();
}

//------------------------------------------------------------------------------
double ocall_statsprobe_stop(const char *hash) {
    std::lock_guard<std::mutex> lock(tps_mutex);
    return timeprobe_stats[hash].stop();
}

//------------------------------------------------------------------------------
void ocall_statsprobe_summary(const char *hash, uint8_t *buf, size_t sz) {
    std::lock_guard<std::mutex> lock(tps_mutex);
    std::string s = timeprobe_stats[hash].summary();
    strncpy((char *)buf, s.data(), std::min(sz, 1 + s.size()));
}

//------------------------------------------------------------------------------
