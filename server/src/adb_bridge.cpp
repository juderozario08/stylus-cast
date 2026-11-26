#include "stylus_cast/adb_bridge.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace stylus_cast {

namespace {

constexpr int ADB_POLL_MS    = 1500;
constexpr int ADB_TIMEOUT_MS = 1500;

#ifdef _WIN32

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 1) {
        return {};
    }
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), len);
    return out;
}

bool fileExists(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring getEnvWide(const wchar_t* name) {
    const DWORD len = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (len == 0) {
        return {};
    }
    std::wstring val(len, L'\0');
    const DWORD  written = ::GetEnvironmentVariableW(name, val.data(), len);
    if (written == 0) {
        return {};
    }
    val.resize(written);
    return val;
}

std::wstring findAdbExecutable() {
    wchar_t pathBuf[MAX_PATH] = {};
    if (::SearchPathW(nullptr, L"adb.exe", nullptr, MAX_PATH, pathBuf, nullptr) > 0) {
        return pathBuf;
    }

    const wchar_t* envVars[] = {L"LOCALAPPDATA", L"ANDROID_HOME", L"ANDROID_SDK_ROOT"};
    const wchar_t* suffixes[] = {
        L"\\Android\\Sdk\\platform-tools\\adb.exe",
        L"\\platform-tools\\adb.exe",
        L"\\platform-tools\\adb.exe",
    };
    for (size_t i = 0; i < 3; ++i) {
        const std::wstring base = getEnvWide(envVars[i]);
        if (!base.empty()) {
            const std::wstring candidate = base + suffixes[i];
            if (fileExists(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

bool runAdbCommand(const std::wstring& exe,
                   const std::wstring& args,
                   std::string*        capturedOut,
                   DWORD               timeoutMs) {
    HANDLE readPipe  = nullptr;
    HANDLE writePipe = nullptr;
    HANDLE nulOut    = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (capturedOut != nullptr) {
        if (!::CreatePipe(&readPipe, &writePipe, &sa, 0)) {
            return false;
        }
        ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    } else {
        nulOut = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = INVALID_HANDLE_VALUE;
    si.hStdOutput = writePipe != nullptr ? writePipe : nulOut;
    si.hStdError  = nulOut != nullptr ? nulOut : writePipe;

    PROCESS_INFORMATION pi{};
    std::wstring        cmdLine = L"\"" + exe + L"\" " + args;
    const BOOL          ok      = ::CreateProcessW(exe.c_str(), cmdLine.data(), nullptr, nullptr, TRUE,
                                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    if (writePipe != nullptr) {
        ::CloseHandle(writePipe);
    }
    if (nulOut != nullptr) {
        ::CloseHandle(nulOut);
    }
    if (!ok) {
        if (readPipe != nullptr) {
            ::CloseHandle(readPipe);
        }
        return false;
    }

    if (capturedOut != nullptr && readPipe != nullptr) {
        capturedOut->clear();
        char  buf[1024];
        DWORD bytesRead = 0;
        while (::ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
            capturedOut->append(buf, bytesRead);
        }
        ::CloseHandle(readPipe);
    }

    const DWORD waitRes = ::WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD       exitCode = 1;
    if (waitRes == WAIT_OBJECT_0) {
        ::GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        ::TerminateProcess(pi.hProcess, 1);
    }

    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return waitRes == WAIT_OBJECT_0 && exitCode == 0;
}

#else

bool isExecutable(const std::string& path) {
    struct stat st {};
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(path.c_str(), X_OK) == 0;
}

std::string findAdbExecutable() {
    if (const char* pathEnv = std::getenv("PATH")) {
        std::istringstream iss(pathEnv);
        std::string        dir;
        while (std::getline(iss, dir, ':')) {
            if (!dir.empty()) {
                const std::string candidate = dir + "/adb";
                if (isExecutable(candidate)) {
                    return candidate;
                }
            }
        }
    }

    std::vector<std::string> candidates;
    if (const char* home = std::getenv("HOME")) {
        candidates.push_back(std::string(home) + "/Library/Android/sdk/platform-tools/adb");
    }
    if (const char* androidHome = std::getenv("ANDROID_HOME")) {
        candidates.push_back(std::string(androidHome) + "/platform-tools/adb");
    }
    if (const char* sdkRoot = std::getenv("ANDROID_SDK_ROOT")) {
        candidates.push_back(std::string(sdkRoot) + "/platform-tools/adb");
    }
    candidates.emplace_back("/opt/homebrew/bin/adb");
    candidates.emplace_back("/usr/local/bin/adb");

    for (const auto& candidate : candidates) {
        if (isExecutable(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool runAdbCommand(const std::string&              exe,
                   const std::vector<std::string>& args,
                   std::string*                    capturedOut,
                   int                             timeoutMs) {
    int pipeFds[2] = {-1, -1};
    if (capturedOut != nullptr && ::pipe(pipeFds) != 0) {
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        if (pipeFds[0] >= 0) {
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
        }
        return false;
    }

    if (pid == 0) {
        const int devNull = ::open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            ::dup2(devNull, STDIN_FILENO);
            ::dup2(devNull, STDERR_FILENO);
            if (capturedOut == nullptr) {
                ::dup2(devNull, STDOUT_FILENO);
            }
            if (devNull > STDERR_FILENO) {
                ::close(devNull);
            }
        }
        if (capturedOut != nullptr) {
            ::close(pipeFds[0]);
            ::dup2(pipeFds[1], STDOUT_FILENO);
            ::close(pipeFds[1]);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(exe.c_str(), argv.data());
        ::_exit(127);
    }

    if (capturedOut != nullptr) {
        ::close(pipeFds[1]);
        capturedOut->clear();
        char    buf[1024];
        pollfd  pfd{pipeFds[0], POLLIN, 0};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            const int remMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
            if (::poll(&pfd, 1, std::max(1, remMs)) > 0 && (pfd.revents & POLLIN) != 0) {
                const ssize_t n = ::read(pipeFds[0], buf, sizeof(buf));
                if (n <= 0) {
                    break;
                }
                capturedOut->append(buf, static_cast<size_t>(n));
            } else {
                break;
            }
        }
        ::close(pipeFds[0]);
    }

    int        status   = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (r < 0 || std::chrono::steady_clock::now() >= deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            return false;
        }
        ::usleep(10'000);
    }
}

#endif

std::vector<std::string> parseDeviceSerials(const std::string& devicesOutput) {
    std::vector<std::string> serials;
    std::istringstream       iss(devicesOutput);
    std::string              line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab != std::string::npos && line.substr(tab + 1) == "device") {
            serials.push_back(line.substr(0, tab));
        }
    }
    return serials;
}

} // namespace

AdbBridge::AdbBridge(uint16_t port, bool verbose, const std::atomic<bool>& usbClientConnected)
    : port_(port), verbose_(verbose), usbClientConnected_(usbClientConnected), thread_([this] { run(); }) {}

AdbBridge::~AdbBridge() {
    running_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void AdbBridge::run() {
    const auto adbExe = findAdbExecutable();
    if (adbExe.empty()) {
        if (verbose_) {
            std::cout << "[USB] 'adb' not found in PATH or Android SDK; automatic USB reverse disabled "
                         "(manual `adb reverse tcp:"
                      << port_ << " tcp:" << port_ << "` still works).\n";
        }
        return;
    }

    const std::string portArg = "tcp:" + std::to_string(port_);
    bool              active  = false;

    while (running_.load(std::memory_order_relaxed)) {
        if (!usbClientConnected_.load(std::memory_order_relaxed)) {
#ifdef _WIN32
            const std::wstring wPort = utf8ToWide(portArg);
            bool               ok    = runAdbCommand(adbExe, L"reverse " + wPort + L" " + wPort, nullptr, ADB_TIMEOUT_MS);
            if (!ok) {
                std::string devicesOut;
                if (runAdbCommand(adbExe, L"devices", &devicesOut, ADB_TIMEOUT_MS)) {
                    for (const auto& serial : parseDeviceSerials(devicesOut)) {
                        const std::wstring wSerial = utf8ToWide(serial);
                        if (runAdbCommand(adbExe, L"-s " + wSerial + L" reverse " + wPort + L" " + wPort, nullptr,
                                          ADB_TIMEOUT_MS)) {
                            ok = true;
                        }
                    }
                }
            }
#else
            bool ok = runAdbCommand(adbExe, {"reverse", portArg, portArg}, nullptr, ADB_TIMEOUT_MS);
            if (!ok) {
                std::string devicesOut;
                if (runAdbCommand(adbExe, {"devices"}, &devicesOut, ADB_TIMEOUT_MS)) {
                    for (const auto& serial : parseDeviceSerials(devicesOut)) {
                        if (runAdbCommand(adbExe, {"-s", serial, "reverse", portArg, portArg}, nullptr,
                                          ADB_TIMEOUT_MS)) {
                            ok = true;
                        }
                    }
                }
            }
#endif
            if (ok && !active) {
                active = true;
                std::cout << "[USB] ADB reverse active on tcp:" << port_ << " (ready for USB cable)" << std::endl;
            } else if (!ok && active) {
                active = false;
                if (verbose_) {
                    std::cout << "[USB] ADB device disconnected" << std::endl;
                }
            }
        }

        for (int i = 0; i < ADB_POLL_MS / 100 && running_.load(std::memory_order_relaxed); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (active) {
#ifdef _WIN32
        const std::wstring wPort = utf8ToWide(portArg);
        runAdbCommand(adbExe, L"reverse --remove " + wPort, nullptr, 500);
#else
        runAdbCommand(adbExe, {"reverse", "--remove", portArg}, nullptr, 500);
#endif
    }
}

} // namespace stylus_cast
