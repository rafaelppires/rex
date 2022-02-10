#pragma once

#include <functional>
#include <string>

typedef std::function<int(const std::string&)> InputFunction;

template <typename T>
class CommunicationManager {
   public:
    static void init(InputFunction f, int port) { T::init(f, port); }
    static void iterate() { T::iterate(); }
    static void finish() { T::finish(); }
};
