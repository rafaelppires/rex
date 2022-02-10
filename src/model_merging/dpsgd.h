#pragma once

#include <machine_learning/mf_node.h>

//------------------------------------------------------------------------------
// Decorator of ShareableModel, to be trasmitted
// Adds degree for DPSGD
//------------------------------------------------------------------------------
class DPSGDShareableModel : public ShareableModel {
   public:
    DPSGDShareableModel() = default; 
    DPSGDShareableModel(int e, const MatrixFactorizationModel &m,
                        SharingRatings, size_t degree);

    virtual std::vector<uint8_t> serialize() const;
    virtual size_t deserialize(const std::vector<uint8_t> &data);
    size_t degree_;
};

//------------------------------------------------------------------------------
typedef std::shared_ptr<DPSGDShareableModel> DPSGDModelPtr;
class DPSGDMerger : public ModelMerger {
   public:
    DPSGDMerger(unsigned userrank, 
                unsigned share_howmany, Communication *c,
                std::shared_ptr<MFSGDDecentralized> trainer,
                std::set<unsigned> &neighbours, 
                bool modelshare = true, bool datashare = false);

    virtual size_t share(int epoch);
    virtual void merge(int epoch);
};

//------------------------------------------------------------------------------
