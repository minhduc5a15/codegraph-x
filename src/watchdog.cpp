#include "watchdog.hpp"

#include <iostream>

#if defined(__linux__)
#include <sys/prctl.h>
#include <unistd.h>

#include <csignal>
#elif defined(_WIN32)
#include <windows.h>

#include <cstdlib>
#include <thread>
#endif

void initialize_parent_death_watchdog(int parent_pid) {
#if defined(__linux__)
    // Configure Linux kernel to automatically send SIGTERM when parent dies
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#elif defined(_WIN32)
    if (parent_pid > 0) {
        std::thread([parent_pid]() {
            HANDLE parent_handle = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
            if (parent_handle) {
                WaitForSingleObject(parent_handle, INFINITE);
                CloseHandle(parent_handle);
                std::exit(0);
            }
        }).detach();
    }
#endif
}
