#include <csv.h>
#include <ratings_parser.h>
#include <stringtools.h>

#include <Eigen/Sparse>
#include <fstream>
#include <iostream>
#include <set>
using Eigen::Triplet;
static std::set<int> users;
//------------------------------------------------------------------------------
bool csvitem_tovector(TripletVector<uint8_t>& v,
                      const std::vector<std::string>& item,
                      std::pair<int, int>& dim, int limit, int filter_divisor,
                      int filter_modulo) {
    try {
        Triplet<uint8_t> t(std::stoul(item[0]) - 1, std::stoul(item[1]) - 1,
                           int(std::stof(item[2]) * 2));
        if (limit > 0) {
            users.insert(t.row());
            if (users.size() > limit) return false;
        }
        if (filter_modulo < 0 || filter_divisor == 0 ||
            t.row() % filter_divisor == filter_modulo) {
            dim.first = std::max(dim.first, t.row() + 1);
            dim.second = std::max(dim.second, t.col() + 1);
            v.push_back(t);
        }
    } catch (const std::invalid_argument& e) {
        std::cerr << "Unable to convert <" << item[0] << "," << item[1] << ","
                  << item[2] << ">" << std::endl;
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
struct FileBuckets {
    FileBuckets() : joiner(",") {}
    std::map<unsigned, std::ofstream> buckets;
    Joiner joiner;

    void closeall() {
        for (auto& b : buckets) {
            b.second.close();
        }
        buckets.clear();
    }

    ~FileBuckets() { closeall(); }
};

//------------------------------------------------------------------------------
bool csvitem_touserbucket(const std::vector<std::string>& item,
                          FileBuckets& fb) {
    try {
        unsigned user = std::stoul(item[0]) - 1;
        if (fb.buckets.find(user) == fb.buckets.end()) {
            fb.closeall();  // keep only one file opened at a time
                            // it's fine when input is sorted
            fb.buckets[user].open(item[0] + ".csv");
        }
        fb.buckets[user] << fb.joiner.join(item) << std::endl;
    } catch (const std::invalid_argument& e) {
        std::cerr << "Unable to convert <" << item[0] << "," << item[1] << ","
                  << item[2] << ">" << std::endl;
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
bool csv_ratings_triplet_vector(const std::string fname,
                                TripletVector<uint8_t>& v) {
    std::pair<int, int> dim(0, 0);
    if (!csv_parse(fname, std::bind(csvitem_tovector, std::ref(v),
                                    std::placeholders::_1, std::ref(dim), -1, 0,
                                    -1))) {
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
bool csv_ratings_sparse_matrix(const std::string fname,
                               Eigen::SparseMatrix<uint8_t>& m) {
    TripletVector<uint8_t> v;
    std::pair<int, int> dim(0, 0);
    if (!csv_parse(fname, std::bind(csvitem_tovector, std::ref(v),
                                    std::placeholders::_1, std::ref(dim), -1, 0,
                                    -1))) {
        return false;
    }
    m.reserve(v.size());
    m.resize(dim.first, dim.second);
    m.setFromTriplets(v.begin(), v.end());
    return true;
}

//------------------------------------------------------------------------------
bool csv_bucketize_byuser(const std::string& fname) {
    FileBuckets filebuckets;
    if (!csv_parse(fname, std::bind(csvitem_touserbucket, std::placeholders::_1,
                                    std::ref(filebuckets)))) {
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
