#include "mf_centralized.h"

#include <utils/time_probe.h>

#include <future>
#include <iostream>
#include <mutex>
//------------------------------------------------------------------------------
MatrixFactorizationModel MFSGD::trainX(const Ratings &ratings, uint8_t lowscore,
                                       uint8_t highscore, int matrix_rank,
                                       double learning, double regularization,
                                       int iterations,
                                       const TripletVector<uint8_t> &test) {
    double init_bias = lowscore,
           init_factor = sqrt(double(highscore - lowscore) / matrix_rank);
    HyperMFSGD hyper(matrix_rank, learning, regularization, init_bias,
                     init_factor);
    MFSGDCentralized trainer(ratings, hyper);
    trainer.init();
    std::cout << "epoch;trainerr;testerr\n";
    TimeProbe time;
    std::cout << "epoch;timestamp;trainerr;testerr\n";
    time.start();
    for (int i = 0; i < iterations; ++i) {
        auto res = trainer.train();
        std::cout << i << ";" << time.stop() << ";"
                  << sqrt(res.first / res.second) << ";"
                  << trainer.model().rmse(test) << std::endl;
    }
    return trainer.model();
}

//------------------------------------------------------------------------------
// MFSGDCentralized
//------------------------------------------------------------------------------
MFSGDCentralized::MFSGDCentralized(const Ratings &r, HyperMFSGD h)
    : MFSGD(h), ratings_(r), pool_(std::thread::hardware_concurrency()) {}

//------------------------------------------------------------------------------
std::shared_ptr<std::mutex> MFSGDCentralized::get_user_lock(int user) {
    std::shared_ptr<std::mutex> ret;
    {
        std::lock_guard<std::mutex> lock(usermtx);
        std::map<int, std::shared_ptr<std::mutex>>::iterator it;
        if ((it = user_locks.find(user)) == user_locks.end()) {
            user_locks[user] = std::make_shared<std::mutex>();
            it = user_locks.find(user);
        }
        ret = it->second;
    }
    return ret;
}

//------------------------------------------------------------------------------
std::shared_ptr<std::mutex> MFSGDCentralized::get_item_lock(int item) {
    std::shared_ptr<std::mutex> ret;
    {
        std::lock_guard<std::mutex> lock(itemmtx);
        std::map<int, std::shared_ptr<std::mutex>>::iterator it;
        if ((it = item_locks.find(item)) == item_locks.end()) {
            item_locks[item] = std::make_shared<std::mutex>();
            it = item_locks.find(item);
        }
        ret = it->second;
    }
    return ret;
}

//------------------------------------------------------------------------------
std::pair<double, size_t> MFSGDCentralized::parallel_train(
    Ratings::InnerIterator it, int item) {
    double err = 0;
    size_t count = 0;
    std::shared_ptr<std::mutex> ulock, ilock;
    ilock = get_item_lock(item);
    if (!ilock) {
        std::cerr << "Error getting item " << item << " lock" << std::endl;
        abort();
    } else {
        std::lock_guard<std::mutex> lock_item(*ilock);
        for (; it; ++it) {
            ulock = get_user_lock(it.row());
            if (!ulock) {
                std::cerr << "Error getting user " << it.row() << " lock"
                          << std::endl;
                abort();
            } else {
                std::lock_guard<std::mutex> lock_user(*ulock);
                err += MFSGD::train(it.row(), item, it.value());
                ++count;
            }
        }
    }
    return std::make_pair(err, count);
}

//------------------------------------------------------------------------------
void MFSGDCentralized::init() {
    sparse_matrix_iterate(ratings_, [&](Ratings::InnerIterator it) {
        int user = it.row(), item = it.col();
        model_.find_space(user, item, hyper_.init_column_, hyper_.init_bias);
        model_.init_item(item, hyper_.init_column_);
        model_.init_user(user, hyper_.init_column_);
    });
}

//------------------------------------------------------------------------------
std::pair<double, size_t> MFSGDCentralized::train() {
    std::vector<std::future<std::pair<double, size_t>>> results;
    sparse_matrix_outer_iterate(ratings_, [&](Ratings::InnerIterator it,
                                              int item) {
        auto shared =
            std::make_shared<std::packaged_task<std::pair<double, size_t>()>>(
                std::bind(&MFSGDCentralized::parallel_train, this, it, item));
        results.emplace_back(shared->get_future());
        pool_.add_task([shared]() { (*shared)(); });
    });

    double total_err = 0;
    size_t count = 0;
    for (auto &r : results) {
        r.wait();
        auto ret = r.get();
        total_err += ret.first;
        count += ret.second;
    }
    return std::make_pair(total_err, count);
}

//------------------------------------------------------------------------------
