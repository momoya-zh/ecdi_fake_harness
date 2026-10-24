#include "ChildSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <utility>
#include <vector>

namespace {

// 关句柄（幂等：INVALID_HANDLE_VALUE / nullptr 都安全）
void CloseIfValid(void*& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(handle));
    }
    handle = nullptr;
}

}   // namespace

ChildSource::ChildSource() = default;

ChildSource::~ChildSource() {
    Stop();
}

bool ChildSource::Start(const std::filesystem::path& exePath,
                        const std::string& args, std::string& error) {
    if (!std::filesystem::exists(exePath)) {
        error = "core executable not found: " + exePath.string();
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    // ① core→gui：stdout 管道（读端不继承）
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        error = "CreatePipe(stdout) failed: " + std::to_string(GetLastError());
        CloseIfValid(reinterpret_cast<void*&>(stdoutRead));
        CloseIfValid(reinterpret_cast<void*&>(stdoutWrite));
        return false;
    }

    // ② gui→core：stdin 管道（写端不继承）
    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0) ||
        !SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        error = "CreatePipe(stdin) failed: " + std::to_string(GetLastError());
        CloseIfValid(reinterpret_cast<void*&>(stdinRead));
        CloseIfValid(reinterpret_cast<void*&>(stdinWrite));
        CloseIfValid(reinterpret_cast<void*&>(stdoutRead));
        CloseIfValid(reinterpret_cast<void*&>(stdoutWrite));
        return false;
    }

    // ③ core 的 stderr → 文件（诊断：core 崩溃/警告可见）
    m_stderrPath = std::filesystem::temp_directory_path() / "harness-core-stderr.log";
    SECURITY_ATTRIBUTES saFile = sa;
    HANDLE stderrFile = CreateFileW(m_stderrPath.wstring().c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ, &saFile, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stderrFile == INVALID_HANDLE_VALUE) {
        stderrFile = nullptr;   // 无 stderr 不致命——继续
    }

    // ④ 命令行（UTF-8 → UTF-16；core 路径含空格也不怕）
    std::wstring cmdLine = L"\"" + exePath.wstring() + L"\"";
    if (!args.empty()) {
        const int wideLen = MultiByteToWideChar(CP_UTF8, 0, args.c_str(),
                                                static_cast<int>(args.size()), nullptr, 0);
        std::wstring wideArgs(static_cast<size_t>(wideLen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, args.c_str(), static_cast<int>(args.size()),
                            wideArgs.data(), wideLen);
        cmdLine += L" " + wideArgs;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = stdoutWrite;
    si.hStdError = (stderrFile ? stderrFile : GetStdHandle(STD_ERROR_HANDLE));

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    // 父进程侧立刻关掉子进程端（否则管道永不 EOF）
    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    if (stderrFile) CloseHandle(stderrFile);

    if (!ok) {
        error = "CreateProcess failed: " + std::to_string(GetLastError());
        CloseIfValid(reinterpret_cast<void*&>(stdoutRead));
        CloseIfValid(reinterpret_cast<void*&>(stdinWrite));
        return false;
    }

    m_process = pi.hProcess;
    CloseHandle(pi.hThread);
    m_stdoutRead = stdoutRead;
    m_stdinWrite = stdinWrite;
    m_stderrFile = stderrFile;

    m_reader = std::thread([this] { ReaderMain(); });
    return true;
}

void ChildSource::ReaderMain() {
    const HANDLE h = static_cast<HANDLE>(m_stdoutRead);
    std::string pending;
    char buf[8192];
    DWORD read = 0;

    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0) {
        pending.append(buf, read);
        // 按 '\n' 分行；残留留到下轮（不完整的行不得下发）
        size_t start = 0;
        for (;;) {
            const size_t nl = pending.find('\n', start);
            if (nl == std::string::npos) break;
            std::string line = pending.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            start = nl + 1;
            if (line.empty()) continue;   // 空行忽略（协议 §1.2）

            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_queue.size() < kQueueLimit; });
            m_queue.push_back(std::move(line));
            // ★ REQ-03 探针：入队时刻（默认关闭 ⇒ 这条队列恒空，不改变任何可见行为）
            if (m_stampEnabled) m_stamps.push_back(std::chrono::steady_clock::now());
            ++m_lineCount;
            lock.unlock();
            m_cv.notify_all();
            // ★★ REQ-03：**先入队、再投递**。反过来的话，UI 线程可能先被唤醒、
            //   进来却取到空队列 ⇒ 该行要等到下一个 33ms tick 才被发现
            //   （"唤醒到了但无事可做"，延迟一点没省）。
            NotifyDataArrived();
        }
        pending.erase(0, start);
    }

    // EOF：残留的非空内容按截断处理（协议 §1.2 不得静默丢弃 → 作为最后一行下发，
    // 由 Transcript 的 PARSE 分支报错——与回放的截断行为一致）
    if (!pending.empty()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push_back(pending + "\n[truncated]");
        if (m_stampEnabled) m_stamps.push_back(std::chrono::steady_clock::now());
        ++m_lineCount;
        lock.unlock();
        m_cv.notify_all();
        NotifyDataArrived();
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_eof = true;
    lock.unlock();
    m_cv.notify_all();
    // ★ REQ-03：断连也要立刻通知——否则「core 退了」这件事要等下一个 tick 才被看见，
    //   而那时 UI 还显示着"发送中"。tick 兜底仍在，这只是把它从 33ms 缩到 ~0。
    NotifyDataArrived();
}

// ── ★★ REQ-03：唤醒通路 ────────────────────────────────────────────

void ChildSource::NotifyDataArrived() {
    // ★ 合并点：exchange 返回 true ⇒ 已有在途唤醒，本次什么都不做。
    //   没有这一行，一次突发会变成「每行一个 std::function 入队」。
    if (m_wakeArmed.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    std::function<bool()> handler;
    {
        // ★ 拷出来再调用：持锁跑用户代码既会和 UI 线程的 `SetWakeHandler` 抢锁，
        //   也与 `PostToUi` 自己的入队锁构成嵌套（锁序问题）。
        std::lock_guard<std::mutex> lock(m_mutex);
        handler = m_wakeHandler;
    }

    if (!handler) {
        // 未装唤醒（回放 / `--legacy-poll` 世界）⇒ 复位并交给 33ms tick 拉取，
        // 与改动前的行为完全一致。
        m_wakeArmed.store(false, std::memory_order_release);
        return;
    }

    bool posted = false;
    try {
        posted = handler();
    } catch (...) {
        posted = false;   // 用户回调不该抛；真抛了也不能让读线程带着异常退出
    }

    if (posted) {
        m_wakePosts.fetch_add(1, std::memory_order_relaxed);
    } else {
        // ★ 投递失败（关闭中 / 唤醒失败 / bad_alloc —— `Application.h:83` 明写框架
        //   **不吞** bad_alloc，所以这里必须自己吞掉并降级）。复位后**下一行**会再试；
        //   期间 tick 兜底取数，不丢行。★ 不复位就是永久卡死：之后每一行都看到
        //   armed==true 而跳过投递，谁也不会再来取。
        m_wakeArmed.store(false, std::memory_order_release);
    }
}

void ChildSource::SetWakeHandler(std::function<bool()> handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_wakeHandler = std::move(handler);
}

void ChildSource::RearmWake() {
    m_wakeArmed.store(false, std::memory_order_release);
    bool hasData = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        hasData = !m_queue.empty();
    }
    // 这次 drain 有预算（见 App 侧），取剩的部分要自己再投一次——不能指望
    // 下一行到达来顺带唤醒（数据可能早就全到了，不会有"下一行"）。
    if (hasData) NotifyDataArrived();
}

bool ChildSource::HasPending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queue.empty();
}

void ChildSource::EnableLineStamps(bool on) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stampEnabled = on;
    if (!on) m_stamps.clear();
}

bool ChildSource::NextWithinStamped(std::string& line, int timeoutMs,
                                    std::chrono::steady_clock::time_point& stamp) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.empty() && !m_eof && timeoutMs > 0) {
        m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                      [this] { return !m_queue.empty() || m_eof; });
    }
    if (m_queue.empty()) {
        return false;
    }
    line = std::move(m_queue.front());
    m_queue.pop_front();
    // ★ 无戳（探针没开 / 该行在开戳之前入队）⇒ 返回 0 计数的 time_point，
    //   由调用方判 `time_since_epoch().count() == 0`。**绝不**用 now() 顶替——
    //   那是伪造一个"零延迟"读数。
    stamp = std::chrono::steady_clock::time_point{};
    if (!m_stamps.empty()) {
        stamp = m_stamps.front();
        m_stamps.pop_front();
    }
    lock.unlock();
    m_cv.notify_all();
    return true;
}

bool ChildSource::Next(std::string& line) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_queue.empty() || m_eof; });
    if (m_queue.empty()) {
        return false;   // EOF 且队列空
    }
    line = std::move(m_queue.front());
    m_queue.pop_front();
    if (!m_stamps.empty()) m_stamps.pop_front();   // 探针队列与行队列同步出队
    lock.unlock();
    m_cv.notify_all();   // 唤醒可能阻塞的读线程（队列上限）
    return true;
}

bool ChildSource::NextWithin(std::string& line, int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.empty() && !m_eof && timeoutMs > 0) {
        m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                      [this] { return !m_queue.empty() || m_eof; });
    }
    if (m_queue.empty()) {
        return false;   // 超时，或 EOF 且队列空
    }
    line = std::move(m_queue.front());
    m_queue.pop_front();
    if (!m_stamps.empty()) m_stamps.pop_front();   // 探针队列与行队列同步出队
    lock.unlock();
    m_cv.notify_all();
    return true;
}

bool ChildSource::Eof() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_eof;
}

bool ChildSource::Send(const std::string& line) {
    std::lock_guard<std::mutex> writeLock(m_writeMutex);
    const HANDLE h = static_cast<HANDLE>(m_stdinWrite);
    if (h == nullptr) {
        return false;
    }
    std::string data = line;
    data.push_back('\n');
    DWORD written = 0;
    const BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()),
                              &written, nullptr);
    if (!ok || written != data.size()) {
        return false;
    }
    FlushFileBuffers(h);
    return true;
}

void ChildSource::Stop(int graceMs) {
    // ① 关 stdin → core 侧 Scanner 得到 EOF → 走 eof 收尾自然退出
    {
        std::lock_guard<std::mutex> writeLock(m_writeMutex);
        CloseIfValid(m_stdinWrite);
    }

    // ② 宽限等待
    if (m_process != nullptr) {
        const HANDLE proc = static_cast<HANDLE>(m_process);
        if (WaitForSingleObject(proc, static_cast<DWORD>(graceMs)) == WAIT_TIMEOUT) {
            TerminateProcess(proc, 1);   // 不自退的故障不挂死退出路径
            WaitForSingleObject(proc, 2000);
        }
        DWORD code = 0;
        if (GetExitCodeProcess(proc, &code)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_exitCode = static_cast<int>(code);
            m_exited = true;
        }
        CloseIfValid(m_process);
    }

    // ③ 读线程会因管道另一端关闭而 EOF 退出
    if (m_reader.joinable()) {
        m_reader.join();
    }
    CloseIfValid(m_stdoutRead);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_eof = true;
    }
    m_cv.notify_all();
}

size_t ChildSource::LinesReceived() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lineCount;
}

bool ChildSource::Exited() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_exited;
}

int ChildSource::ExitCode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_exitCode;
}
