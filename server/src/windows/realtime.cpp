#include "stylus_cast/realtime.hpp"

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

#include <csignal>

namespace stylus_cast {

namespace {

std::atomic<bool>* g_runningFlag = nullptr;
bool               g_wsaOk       = false;

void onSignal(int /*signal*/) {
    if (g_runningFlag != nullptr) {
        g_runningFlag->store(false, std::memory_order_relaxed);
    }
}

BOOL WINAPI onConsoleCtrl(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (g_runningFlag != nullptr) {
                g_runningFlag->store(false, std::memory_order_relaxed);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

void enablePerMonitorDpiAwareness() {
    using SetDpiContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32        = ::GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        auto setDpiContext = reinterpret_cast<SetDpiContextFn>(
            reinterpret_cast<void*>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (setDpiContext != nullptr) {
            setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    ::SetProcessDPIAware();
}

int64_t queryFrequency() {
    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    return freq.QuadPart > 0 ? freq.QuadPart : 1;
}

} // namespace

bool platformInit() {
    enablePerMonitorDpiAwareness();
    WSADATA data{};
    g_wsaOk = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return g_wsaOk;
}

void platformCleanup() {
    if (g_wsaOk) {
        ::WSACleanup();
        g_wsaOk = false;
    }
}

void installSignalHandlers(std::atomic<bool>& running) {
    g_runningFlag = &running;
    ::SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
}

bool makeThreadRealtime() {
    ::SetPriorityClass(::GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    ::timeBeginPeriod(1);
    return ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != FALSE;
}

void endThreadRealtime() {
    ::timeEndPeriod(1);
}

int64_t monotonicMicros() {
    static const int64_t freq = queryFrequency();
    LARGE_INTEGER        counter{};
    ::QueryPerformanceCounter(&counter);
    const int64_t sec = counter.QuadPart / freq;
    const int64_t rem = counter.QuadPart % freq;
    return sec * 1'000'000LL + (rem * 1'000'000LL) / freq;
}

} // namespace stylus_cast
