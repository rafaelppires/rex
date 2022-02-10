#include "data_splitter.h"
#include <csv.h>
#include <ratings_parser.h>
#include <set>

using std::string;
//------------------------------------------------------------------------------
void split_vectors(int from, int to, TripletVector<uint8_t> &src,
                   TripletVector<uint8_t> &train,
                   TripletVector<uint8_t> &test) {
    float tmult = 0.7;
    int count = to - from;
    int tcount = tmult * count;
    std::set<int> indexes;
    while (indexes.size() != tcount) indexes.insert(from + rand() % count);
    for (int i = from; i < to; ++i) {
        if (indexes.find(i) == indexes.end())
            test.push_back(src[i]);
        else
            train.push_back(src[i]);
    }
}

//------------------------------------------------------------------------------
void split_data(TripletVector<uint8_t> &from, TripletVector<uint8_t> &train,
                TripletVector<uint8_t> &test) {
    int i = 0, id = -1, count = 0, idx = -1;
    srand(0);
    for (i = 0; i < from.size(); ++i) {
        if (id != from[i].row()) {
            if (id != -1) {
                split_vectors(idx, i, from, train, test);
            }
            id = from[i].row();
            idx = i;
        }
    }
    if (idx != i - 1) {
        split_vectors(idx, i, from, train, test);
    }

    assert(from.size() == train.size() + test.size());
}

//------------------------------------------------------------------------------
std::pair<int, int> read_and_split(const std::string &fname,
                                   TripletVector<uint8_t> &train,
                                   TripletVector<uint8_t> &test, int limit,
                                   int filter_divisor, int filter_modulo) {
    TripletVector<uint8_t> v;
    std::pair<int, int> dim(0, 0);
    if (csv_parse(fname, std::bind(csvitem_tovector, std::ref(v),
                                   std::placeholders::_1, std::ref(dim), limit,
                                   filter_divisor, filter_modulo))) {
        split_data(v, train, test);
    }
    return dim;
}

//------------------------------------------------------------------------------
bool read_data(const string &fname, Ratings &m, TripletVector<uint8_t> &test) {
    TripletVector<uint8_t> train;
    std::pair<int, int> dim = read_and_split(fname, train, test);
    if (dim.first == 0 || dim.second == 0) return false;
    fill_matrix(m, dim, train);
    return true;
}

//------------------------------------------------------------------------------
