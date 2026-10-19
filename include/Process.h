#pragma once

#include <atomic>
#include <chrono>
#include <string>

// Minimal process model for FCFS homework. Deferred OOP refactor: plain struct.
enum class ProcState { Queued, Running, Finished };

struct Process {
    std::string name;
    int arrival_id = 0;
    int total = 100;
    std::atomic<int> done{0};
    std::atomic<ProcState> state{ProcState::Queued};
    std::atomic<int> core{-1};
    std::chrono::system_clock::time_point start{};
};
