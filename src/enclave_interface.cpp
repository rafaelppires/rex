#include <enclave_interface.h>
#include <fstream>
#include <iostream>

#ifndef NATIVE
#include <sgx/sgx_initenclave.h>

#define ENCLAVENAME "enclave_rex"
#define ENCLAVEFILE ENCLAVENAME ".signed.so"
#define TOKENFILE ENCLAVENAME ".token"

sgx_enclave_id_t EnclaveInterface::g_eid;
#endif

//------------------------------------------------------------------------------
bool EnclaveInterface::init(const EnclaveArguments &args) {
    int ret;
#ifdef NATIVE
    ret = ecall_init(args);
#else
    // Sets up enclave and initializes remote_attestation
    if (initialize_enclave(g_eid, ENCLAVEFILE, TOKENFILE)) {
        return false;
    }
    ecall_init(g_eid, &ret, args);
#endif

    if (ret) {
        std::cerr << "Failed to init enclave" << std::endl;
        exit(2);
    }
    return true;
}

//------------------------------------------------------------------------------
void EnclaveInterface::finish() {
#ifdef NATIVE
    // ecall_finish();
#else
    // ecall_finish(g_eid);
    destroy_enclave(g_eid);
#endif
}

//------------------------------------------------------------------------------
