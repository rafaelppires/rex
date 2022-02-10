#pragma once

class MatrixFactorizationModel;
//------------------------------------------------------------------------------
// To ease manipulation
//------------------------------------------------------------------------------
class DPSGDEntry {
   public:
    DPSGDEntry(unsigned s, unsigned d, int e, const MatrixFactorizationModel &m)
        : src(s), degree(d), epoch(e), model(m) {}

    int epoch;
    unsigned src;
    unsigned degree;
    const MatrixFactorizationModel &model;
};
