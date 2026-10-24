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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
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

    // ── ★★ REQ-03（2026-10-24）：数据到达 → 投递一次「唤醒」────────────
    // 投递的是**信号**，不是数据：行仍留在 `m_queue` 里，由 UI 线程自己取。
    // 理由：跨线程搬 `std::string` 要连带处理会话生命周期（Session 可被删），
    // 而队列本身已经是现成的同步点（mutex + cv 早就在这里）——搬数据只是多一条
    // 需要维护的交接路径，换不来任何东西。
    //
    // ★ 线程契约：
    //   · 回调**在读线程上**调用（`ReaderMain`），且**绝不在持有 `m_mutex` 时**调用
    //     ——`PostToUi` 自己要加锁入队，持锁调用 = 自找锁序问题；
    //   · 返回 true = 已投递；false = 投递失败（关闭中 / 唤醒失败 / `bad_alloc`）
    //     ⇒ 复位在途标志，让**下一行**再试一次；这期间 tick 兜底不会丢数据；
    //   · 合并：`m_wakeArmed` 保证同一时刻**至多一个**在途唤醒 ⇒ N 行通常只产生
    //     1 次 `PostToUi`（否则 8906 行 = 8906 个 `std::function` 入队）。
    void SetWakeHandler(std::function<bool()> handler);

    // UI 线程：drain 完之后调用。清在途标志；若期间又有行到达，立刻再投一次。
    // ★ 顺序是「先清标志、再看队列」：反过来的话，读数器在两者之间入队会看到
    //   标志仍为 true ⇒ 不投递；而我们已经看过队列（空）⇒ 那一行要等到 33ms
    //   后 tick 才发现。先清后看，最坏是多投一次空唤醒（幂等、无数据可取）。
    void RearmWake();

    // 队列是否还有待取行（非阻塞）。UI 线程用。
    bool HasPending() const;

    // 已成功投递的唤醒次数（诊断 / 探针；读线程写，故 atomic）。
    size_t WakePosts() const { return m_wakePosts.load(std::memory_order_relaxed); }

    // ── 探针用：入队时间戳（默认关闭 ⇒ 既有路径逐位不变）──────────────
    // 记的是**读线程 enqueue 的墙钟**，用来算「到达 → 被 HandleLine 消费」的延迟。
    // ★ 只有 `--async-probe` 会打开；关闭时 `m_stamps` 恒空，取行路径多一个
    //   `if (!m_stamps.empty())` 的判断，不改变任何可见行为。
    void EnableLineStamps(bool on);
    // 取行 + 该行的入队时间戳；`stamp` 只在返回 true 且当时开着戳时有效。
    bool NextWithinStamped(std::string& line, int timeoutMs,
                           std::chrono::steady_clock::time_point& stamp);
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
    // 读线程：数据/EOF 到达后投递唤醒（合并；不带锁调用用户回调）。
    void NotifyDataArrived();

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_queue;      // 读线程 → Next 的有界队列
    // ★ 探针：与 `m_queue` 同生同死的入队时间戳（只在 EnableLineStamps(true) 时增长）。
    std::deque<std::chrono::steady_clock::time_point> m_stamps;
    bool m_stampEnabled = false;
    size_t m_lineCount = 0;
    bool m_eof = false;
    bool m_exited = false;
    int m_exitCode = -1;

    // ★★ REQ-03：唤醒通路（详见公共区 `SetWakeHandler` 的线程契约）
    std::function<bool()> m_wakeHandler;              // 受 m_mutex 保护（可被 UI 线程替换）
    std::atomic<bool> m_wakeArmed{false};             // 在读途唤醒（读线程写 / UI 线程复位）
    std::atomic<size_t> m_wakePosts{0};               // 成功投递次数（诊断）

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
