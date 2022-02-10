#ifdef NATIVE
#include <ecalls_rex.h>
#else
#include <enclave_rex_u.h>
#endif
template <typename T>
int EnclaveInterface::input(const T& msg) {
    int ret;

#ifdef NATIVE
    ret = ecall_input(msg.data(), msg.size());
#else
    ecall_input(g_eid, &ret, msg.data(), msg.size());
#endif
    return ret;
}
