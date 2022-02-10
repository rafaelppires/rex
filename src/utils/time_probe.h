#pragma once

#include <string>
#ifndef ENCLAVED
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>
#include <chrono>
using boost::accumulators::accumulator_set;
using boost::accumulators::stats;
#endif

//------------------------------------------------------------------------------
class TimeProbe {
   public:
    enum Resolution { SECONDS, MICRO, MILI, NANO };

    TimeProbe(Resolution r = MILI);
    void start();
    double stop();

   private:
#ifdef ENCLAVED
    std::string hash();
    std::string hash_;
#else
    std::chrono::time_point<std::chrono::steady_clock> start_;
#endif
    double multiplier_;
};

//------------------------------------------------------------------------------
class TimeProbeStats {
   public:
    TimeProbeStats();
    std::string summary(const std::string &title = "");
    void start();
    double stop();

   private:
#ifdef ENCLAVED
    std::string hash();
    std::string hash_;
#else
    accumulator_set<
        double,
        stats<boost::accumulators::tag::variance, boost::accumulators::tag::sum,
              boost::accumulators::tag::count, boost::accumulators::tag::mean> >
        accum_;
#endif
    TimeProbe probe_;
};
//------------------------------------------------------------------------------
