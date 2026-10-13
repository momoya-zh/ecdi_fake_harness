#pragma once
// 子进程消息源（P1.5 真路径——规格 §4.2）。
// 拉起假 core（或未来任何说协议的后端），经匿名管道双向通信：
//   stdout ← core→gui 消息（后台线程读 → 有界队列 → Next 阻塞取）
//   stdin  → gui→core 消息（Send 写一行 + flush）
//
// ★ 线程模型（规格 §4.2 定案）：Next 由 UI 线程调用并把 UI 线程阻塞在队列上——
//   与 P1 回放的形状一致（读数可直接对比）；async 投递留给 P2 对照实验。
// ★ 生命周期：Stop 先关 stdin（让 core 走 eof 收尾）→ 宽限等待 → 必要时 Terminate。
//   core 侧不自退的故障不会挂死 harness 退出路径。

#include "MessageSource.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

class ChildSource : public MessageSource {
public:
    ChildSource();
    ~ChildSource() override;

    ChildSource(const ChildSource&) = delete;
    ChildSource& operator=(const ChildSource&) = delete;

    // 启动子进程；false + error = 失败（路径无效 / CreateProcess / 管道创建失败）
    bool Start(const std::filesystem::path& exePath,
               const std::string& args, std::string& error);

    // ── MessageSource ──────────────────────────────────────────
    // 阻塞取一行；队列空则等待读线程；EOF 且队列空 → false。
    // ★ UI 线程**不要**用它（会卡死消息循环）——用 NextWithin(line, 0) 轮询。
    bool Next(std::string& line) override;
    // 带超时取一行：true = 拿到；false = 超时或 EOF+空（用 Eof() 区分）。
    // timeoutMs <= 0 = 非阻塞（队列空立即返回）。UI 线程唯一该用的入口。
    bool NextWithin(std::string& line, int timeoutMs);
    // 写一行到子进程 stdin（自动补 '\n'）；失败（进程已退/管道断）→ false。
    bool Send(const std::string& line) override;
    bool IsLive() const override { return true; }

    // 读线程已见 EOF（管道关闭）——注意：队列可能仍有残留行，需配合 NextWithin 取空。
    bool Eof() const;

    // 关 stdin → 等宽限 → 必要时终止；幂等。
    void Stop(int graceMs = 1500);

    // ── 诊断 ───────────────────────────────────────────────────
    size_t LinesReceived() const;
    bool Exited() const;
    int ExitCode() const;
    std::filesystem::path StderrPath() const { return m_stderrPath; }

private:
    void ReaderMain();          // 唯一读线程体

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_queue;      // 读线程 → Next 的有界队列
    size_t m_lineCount = 0;
    bool m_eof = false;
    bool m_exited = false;
    int m_exitCode = -1;

    static constexpr size_t kQueueLimit = 8192;   // 防失控内存（core 狂吐时）

    std::thread m_reader;

    // Windows 句柄（void* 以避免头文件泄漏 <Windows.h>）
    void* m_process = nullptr;
    void* m_stdinWrite = nullptr;
    void* m_stdoutRead = nullptr;
    void* m_stderrFile = nullptr;         // core 的 stderr 重定向到文件（诊断用）
    std::filesystem::path m_stderrPath;
    std::mutex m_writeMutex;              // Send 序列化（防未来多写者）
};
