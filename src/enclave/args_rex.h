#pragma once

#ifdef __cplusplus
#include <cstddef>
extern "C" {
#endif
struct EnclaveArguments {
    unsigned char *train, *test, datashare, modelshare, dpsgd;
    size_t train_size, test_size, degree, steps_per_iteration;
    int userrank;
    char nodes[1000];
    unsigned share_howmany, local, epochs;
};
#ifdef __cplusplus
}
#endif
