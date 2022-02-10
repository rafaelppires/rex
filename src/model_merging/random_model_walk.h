#pragma once

#include <machine_learning/mf_node.h>

//------------------------------------------------------------------------------
class RandomModelWalkMerger : public ModelMerger {
   public:
    RandomModelWalkMerger(unsigned rank,
                          unsigned share_howmany, Communication *c,
                          std::shared_ptr<MFSGDDecentralized> trainer,
                          std::set<unsigned> &neighbours, 
                          bool modelshare, bool datashare);

    virtual size_t share(int epoch);
    virtual void merge(int epoch);
};

//------------------------------------------------------------------------------
