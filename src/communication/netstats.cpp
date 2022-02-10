#include <communication/netstats.h>
#include <iostream>

std::atomic<size_t> NetStats::bytes_in(0), NetStats::bytes_out(0);
//------------------------------------------------------------------------------
void NetStats::add_bytes_out(size_t n) {
    if (n == 0) return;
    // std::cout << "out +" << n << std::endl;
    bytes_out += n;
}

//------------------------------------------------------------------------------
void NetStats::add_bytes_in(size_t n) {
    if (n == 0) return;
    // std::cout << "in +" << n << std::endl;
    bytes_in += n;
}

//------------------------------------------------------------------------------
