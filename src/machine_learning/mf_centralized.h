#include <threads/thread_pool.h>

#include <mutex>

#include "matrix_factorization.h"

//------------------------------------------------------------------------------
class MFSGDCentralized : public MFSGD {
   public:
    MFSGDCentralized(const Ratings& ratings, HyperMFSGD h);
    virtual std::pair<double, size_t> train();
    void init();

   private:
    std::pair<double, size_t> parallel_train(Ratings::InnerIterator it,
                                             int item);
    std::shared_ptr<std::mutex> get_user_lock(int user);
    std::shared_ptr<std::mutex> get_item_lock(int item);

    const Ratings& ratings_;
    std::mutex usermtx, itemmtx;
    std::map<int, std::shared_ptr<std::mutex>> user_locks;
    std::map<int, std::shared_ptr<std::mutex>> item_locks;
    ThreadPool pool_;
};

//------------------------------------------------------------------------------
