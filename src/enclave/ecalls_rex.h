#pragma once
#ifdef NATIVE

#include <args_rex.h>

#include <cstdlib>
int ecall_init(EnclaveArguments args);
int ecall_input(const char *data, size_t data_size);
#endif
