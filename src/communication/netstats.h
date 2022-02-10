#pragma once
#include <atomic>
#include <cstddef>

class NetStats {
   public:
    static void add_bytes_out(size_t);
    static void add_bytes_in(size_t);

    static std::atomic<size_t> bytes_out, bytes_in;
};
