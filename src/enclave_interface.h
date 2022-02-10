#pragma once
#ifndef NATIVE
#include <sgx_eid.h>
#endif
#include <args_rex.h>
#include <string>

class EnclaveInterface {
   public:
    static bool init(const EnclaveArguments &args);
    static void finish();
    template <typename T>
    static int input(const T &);
#ifndef NATIVE
    static sgx_enclave_id_t g_eid;
#endif
};

#include <enclave_interface.hpp>
