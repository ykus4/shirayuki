#include "ThreadList.hpp"
#include <mach/mach.h>
#include <pthread.h>

namespace Shirayuki {

// State constant -> human label. Mach exposes a numeric run state per thread.
static const char *stateLabel(int state) {
    switch (state) {
        case TH_STATE_RUNNING:
            return "running";
        case TH_STATE_STOPPED:
            return "stopped";
        case TH_STATE_WAITING:
            return "waiting";
        case TH_STATE_UNINTERRUPTIBLE:
            return "uninterruptible";
        case TH_STATE_HALTED:
            return "halted";
    }
    return "unknown";
}

std::vector<ThreadInfo> ThreadList::all() {
    std::vector<ThreadInfo> out;

    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
        return out;

    out.reserve(count);
    for (mach_msg_type_number_t i = 0; i < count; i++) {
        ThreadInfo t;

        // Best-effort pthread id (only works for the current task's own threads;
        // returns 0 for kernel-only threads, which is acceptable for display).
        pthread_t p = pthread_from_mach_thread_np(threads[i]);
        if (p) {
            uint64_t tid = 0;
            pthread_threadid_np(p, &tid);
            t.tid = tid;
        }

        // Read ARM64 register state.
        arm_thread_state64_t state = {};
        mach_msg_type_number_t stateCount = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(threads[i], ARM_THREAD_STATE64,
                             reinterpret_cast<thread_state_t>(&state),
                             &stateCount) == KERN_SUCCESS) {
            t.pc = static_cast<uintptr_t>(arm_thread_state64_get_pc(state));
            t.sp = static_cast<uintptr_t>(arm_thread_state64_get_sp(state));
            t.lr = static_cast<uintptr_t>(arm_thread_state64_get_lr(state));
        }

        // Run state via thread_info().
        thread_basic_info_data_t info;
        mach_msg_type_number_t infoCount = THREAD_BASIC_INFO_COUNT;
        if (thread_info(threads[i], THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info),
                        &infoCount) == KERN_SUCCESS) {
            t.state = stateLabel(info.run_state);
        }

        // Release the thread port right — task_threads returns +1 references.
        mach_port_deallocate(mach_task_self(), threads[i]);

        out.push_back(t);
    }

    vm_deallocate(mach_task_self(), (vm_address_t)threads, sizeof(*threads) * count);
    return out;
}

} // namespace Shirayuki
