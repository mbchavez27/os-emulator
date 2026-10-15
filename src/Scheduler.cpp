#include "Scheduler.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

Scheduler::Scheduler(Algo a) : algo_(a) {
    core_busy_.fill(false);
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::setFileLogEnabled(bool enabled) {
    file_log_enabled_ = enabled;
}

void Scheduler::start() {
    std::lock_guard<std::mutex> lk(m_);
    if (running_) {
        return;
    }
    running_ = true;

    if (file_log_enabled_) {
        std::error_code ec;
        std::filesystem::create_directories(log_dir_, ec);
    }

    // Test case: create 10 processes, each with 100 print commands, on start.
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        auto p = std::make_shared<Process>();
        char buf[16];
        std::snprintf(buf, sizeof(buf), "screen_%02d", i + 1);
        p->name = buf;
        p->arrival_id = i;
        p->total = PRINTS_PER_PROCESS;
        p->done = 0;
        p->state = ProcState::Queued;
        p->core = -1;
        p->start = std::chrono::system_clock::now();
        all_.push_back(p);
        q_.push(p);

        if (file_log_enabled_) {
            std::lock_guard<std::mutex> log_lk(log_m_);
            std::ofstream out(logPath(p->name), std::ios::trunc);
            out << "Process name: " << p->name << "\nLogs:\n";
        }
    }

    sched_ = std::thread(&Scheduler::schedLoop, this);
    for (int c = 0; c < NUM_CORES; ++c) {
        workers_.emplace_back(&Scheduler::workerLoop, this, c);
    }
    cv_.notify_all();
}

void Scheduler::stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!running_ && !sched_.joinable() && workers_.empty()) {
            return;
        }
        running_ = false;
    }
    cv_.notify_all();
    if (sched_.joinable()) {
        sched_.join();
    }
    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
    workers_.clear();
}

std::vector<std::shared_ptr<Process>> Scheduler::snapshot() {
    std::lock_guard<std::mutex> lk(m_);
    return all_;
}

std::shared_ptr<Process> Scheduler::pickNextFCFS() {
    if (q_.empty()) {
        return nullptr;
    }
    auto p = q_.front();
    q_.pop();
    return p;
}

bool Scheduler::anyIdleLocked() const {
    for (bool b : core_busy_) {
        if (!b) {
            return true;
        }
    }
    return false;
}

int Scheduler::findIdleLocked() const {
    for (int i = 0; i < NUM_CORES; ++i) {
        if (!core_busy_[i]) {
            return i;
        }
    }
    return -1;
}

std::string Scheduler::formatTime(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%m/%d/%Y %I:%M:%S%p");
    return oss.str();
}

std::string Scheduler::logPath(const std::string& name) const {
    return log_dir_ + "/" + name + ".txt";
}

void Scheduler::logPrint(const std::shared_ptr<Process>& p, int core) {
    if (!file_log_enabled_) {
        return;
    }
    std::string ts = formatTime(std::chrono::system_clock::now());
    std::lock_guard<std::mutex> lk(log_m_);
    std::ofstream out(logPath(p->name), std::ios::app);
    out << "(" << ts << ") Core:" << core << " \"Hello world from " << p->name << "!\"\n";
}

void Scheduler::schedLoop() {
    while (true) {
        std::shared_ptr<Process> next;
        int core = -1;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return !running_ || (!q_.empty() && anyIdleLocked()); });
            if (!running_) {
                break;
            }
            // Future algos: switch (algo_) { case Algo::FCFS: ... }
            next = pickNextFCFS();
            core = findIdleLocked();
            if (!next || core < 0) {
                continue;
            }
            core_busy_[core] = true;
            core_proc_[core] = next;
            next->state = ProcState::Running;
            next->core = core;
        }
        cv_.notify_all();
    }
    cv_.notify_all();
}

void Scheduler::workerLoop(int core) {
    while (true) {
        std::shared_ptr<Process> p;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return !running_ || core_proc_[core] != nullptr; });
            if (!running_ && core_proc_[core] == nullptr) {
                break;
            }
            p = core_proc_[core];
            if (!p) {
                continue;
            }
        }

        // FCFS non-preemptive: run to completion (100 prints).
        while (p->done < p->total) {
            {
                std::lock_guard<std::mutex> lk(m_);
                if (!running_) {
                    break;
                }
            }
            logPrint(p, core);
            ++p->done;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        {
            std::lock_guard<std::mutex> lk(m_);
            // If stop() fired mid-process, leave state as-is and exit.
            if (p->done >= p->total) {
                p->state = ProcState::Finished;
            }
            p->core = -1;
            core_proc_[core] = nullptr;
            core_busy_[core] = false;
            if (!running_) {
                cv_.notify_all();
                break;
            }
        }
        cv_.notify_all();
    }
}
