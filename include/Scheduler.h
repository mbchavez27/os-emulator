#pragma once

#include "Process.h"
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Minimal FCFS scheduler. Policy isolated in pickNextFCFS() for future algos.
enum class Algo { FCFS };

class Scheduler {
public:
    static constexpr int NUM_CORES = 4;
    static constexpr int NUM_PROCESSES = 10;
    static constexpr int PRINTS_PER_PROCESS = 100;

    explicit Scheduler(Algo a = Algo::FCFS);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    void start();
    void stop();
    std::vector<std::shared_ptr<Process>> snapshot();

    // Homework-only toggle. Disable for MP submission (disk I/O hurts perf).
    void setFileLogEnabled(bool enabled);

private:
    void schedLoop();
    void workerLoop(int core);

    // Isolated FCFS policy: FIFO pop. Caller must hold m_ and ensure !q_.empty().
    // Future: pickNextSJF() / pickNextRR() or ISchedulingAlgorithm strategy.
    std::shared_ptr<Process> pickNextFCFS();

    bool anyIdleLocked() const;
    int findIdleLocked() const;
    static std::string formatTime(const std::chrono::system_clock::time_point& tp);
    void logPrint(const std::shared_ptr<Process>& p, int core);

    Algo algo_;
    std::queue<std::shared_ptr<Process>> q_;
    std::vector<std::shared_ptr<Process>> all_;
    std::array<std::shared_ptr<Process>, NUM_CORES> core_proc_{};
    std::array<bool, NUM_CORES> core_busy_{};

    std::mutex m_;
    std::condition_variable cv_;
    std::mutex log_m_;

    std::thread sched_;
    std::vector<std::thread> workers_;
    bool running_ = false;
    bool file_log_enabled_ = true;
};
