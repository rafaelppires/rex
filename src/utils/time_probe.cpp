#include "time_probe.h"
#ifdef ENCLAVED
#include <enclave_rex_t.h>
#include <sgx_cryptoall.h>
#endif

//------------------------------------------------------------------------------
// TimeProbe
//------------------------------------------------------------------------------
TimeProbe::TimeProbe(Resolution r)
#ifdef ENCLAVED
    : hash_(hash())
#endif
{
    switch (r) {
        case SECONDS:
            multiplier_ = 1;
            break;
        case MILI:
            multiplier_ = 1e+3;
            break;
        case MICRO:
            multiplier_ = 1e+6;
            break;
        case NANO:
            multiplier_ = 1e+9;
            break;
    }
}

//------------------------------------------------------------------------------
#ifdef ENCLAVED
std::string TimeProbe::hash() {
    void *me = this;
    return Crypto::b64_encode(
        Crypto::sha224(std::string((const char *)&me, sizeof(me))));
}
#endif

//------------------------------------------------------------------------------
void TimeProbe::start() {
#ifdef ENCLAVED
    ocall_start_timer(hash_.data());
#else
    start_ = std::chrono::steady_clock::now();
#endif
}

//------------------------------------------------------------------------------
double TimeProbe::stop() {
#ifdef ENCLAVED
    double ret;
    ocall_stop_timer(&ret, hash_.data());
    return ret;
#else
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_).count() * multiplier_;
#endif
}

//------------------------------------------------------------------------------
// TimeProbeStats
//------------------------------------------------------------------------------
TimeProbeStats::TimeProbeStats()
#ifdef ENCLAVED
    : hash_(hash())
#endif
{
}

//------------------------------------------------------------------------------
#ifdef ENCLAVED
#include <libc_mock/libcpp_mock.h>
std::string TimeProbeStats::hash() {
    void *me = this;
    return Crypto::b64_encode(
        Crypto::sha224(std::string((const char *)&me, sizeof(me))));
}
#endif

//------------------------------------------------------------------------------
void TimeProbeStats::start() {
#ifdef ENCLAVED
    ocall_statsprobe_start(hash_.data());
#else
    probe_.start();
#endif
}

//------------------------------------------------------------------------------
double TimeProbeStats::stop() {
    double ret;
#ifdef ENCLAVED
    ocall_statsprobe_stop(&ret, hash_.data());
#else
    ret = probe_.stop();
    accum_(ret);
    return ret;
#endif
}

//------------------------------------------------------------------------------
std::string TimeProbeStats::summary(const std::string &title) {
#ifdef ENCLAVED
    std::vector<uint8_t> buf(200);
    ocall_statsprobe_summary(hash_.data(), buf.data(), buf.size());
    buf[buf.size() - 1] = 0;
    return (char *)buf.data();
#else
    using boost::accumulators::count;
    using boost::accumulators::mean;
    using boost::accumulators::sum;
    using boost::accumulators::variance;
    std::stringstream ss;
    ss << count(accum_) << "\t" << mean(accum_) << "\t"
       << sqrt(variance(accum_)) << "\t" << sum(accum_);
    return ss.str();
#endif
}

//------------------------------------------------------------------------------
