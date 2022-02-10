#include <machine_learning/mf_node.h>
#include <stdarg.h>
#include "args_rex.h"
#include "node_protocol.h"

//------------------------------------------------------------------------------
#ifndef NATIVE
#include <enclave_rex_t.h>
#include <libc_mock/libcpp_mock.h>
extern "C" {
int printf(const char *fmt, ...) {
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    uprint(buf);
    return ret;
}
}
#endif
//------------------------------------------------------------------------------
NodeProtocol protocol;
int ecall_init(struct EnclaveArguments args) { return protocol.init(args); }
//------------------------------------------------------------------------------
std::vector<std::string> multipart_deserialize(const char *data,
                                               size_t data_size) {
    std::vector<std::string> ret;
    size_t chunk_size, cursor;
    for (cursor = 0; cursor < data_size;
         cursor += sizeof(size_t) + chunk_size) {
        chunk_size = *(size_t *)(data + cursor);
        if (chunk_size + cursor > data_size) {
            std::cerr << "multipart_deserialize: out of bounds. Chunk: "
                      << chunk_size << " Cursor: " << cursor
                      << " Data size: " << data_size << std::endl;
            break;
        }
        ret.push_back(std::string(data + cursor + sizeof(size_t), chunk_size));
    }
    return ret;
}

//------------------------------------------------------------------------------
int ecall_input(const char *data, size_t data_size) {
    protocol.input(multipart_deserialize(data, data_size));
}
//------------------------------------------------------------------------------
