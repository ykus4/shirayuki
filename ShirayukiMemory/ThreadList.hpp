#ifndef SHIRAYUKI_THREAD_LIST_HPP
#define SHIRAYUKI_THREAD_LIST_HPP

#include "ShirayukiMemory.hpp"

namespace Shirayuki {

struct ThreadInfo {
    uint64_t tid = 0;  // pthread thread id (best-effort)
    uintptr_t pc = 0;  // ARM64 PC at time of query
    uintptr_t sp = 0;  // stack pointer
    uintptr_t lr = 0;  // link register
    std::string state; // running/waiting/uninterruptible
};

namespace ThreadList {
// Enumerate all threads in the current task. Suspended briefly per thread to
// read the register state — cheap, but should not be called on the poll thread.
std::vector<ThreadInfo> all();
} // namespace ThreadList

} // namespace Shirayuki

#endif // SHIRAYUKI_THREAD_LIST_HPP
