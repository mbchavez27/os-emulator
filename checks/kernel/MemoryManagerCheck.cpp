#include "kernel/MemoryManager.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <stdexcept>
#include <string>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::string fileContents(const std::string &path)
{
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

template <typename Action>
bool throwsOutOfRange(Action action)
{
    try
    {
        action();
    }
    catch (const std::out_of_range &)
    {
        return true;
    }
    return false;
}

Config memoryConfig(std::uint64_t total, std::uint64_t frame)
{
    Config config;
    config.maxOverallMem = total;
    config.memPerFrame = frame;
    return config;
}
} // namespace

int main()
{
    const std::string path = "memory-manager-check-store.txt";
    try
    {
        // Two frames of 64 bytes; process 0 has 4 pages, process 1 has 1 page.
        MemoryManager memory;
        memory.configure(memoryConfig(128, 64), path);
        memory.allocate(0, 256);
        memory.allocate(1, 64);
        check(memory.pageCount(0) == 4, "256 bytes must be 4 pages of 64");
        check(memory.stats().usedFrames == 0, "Allocation must not load pages (demand paging)");
        check(memory.residentBytes(0) == 0, "No page is resident before the first access");

        check(memory.access(0, 0), "First touch must fault");
        check(!memory.access(0, 0), "A resident page must not fault");
        check(memory.access(0, 1), "A new page must fault");
        MemoryStats stats = memory.stats();
        check(stats.usedFrames == 2 && stats.usedBytes == 128 && stats.freeBytes == 0, "Frames are full");
        check(stats.pagedIn == 2 && stats.pagedOut == 0, "Two page-ins, no evictions yet");

        // Memory is full: FIFO evicts page 0 (loaded first) to the backing store.
        check(memory.access(1, 0), "Process 1's page must fault");
        stats = memory.stats();
        check(stats.pagedOut == 1, "A full memory must evict one page");
        check(memory.residentBytes(0) == 64 && memory.residentBytes(1) == 64, "Page 0 of process 0 left");
        {
            std::ifstream file(path);
            const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            check(contents.find("pid=0 page=0") != std::string::npos, "The victim must be written to the file");
        }

        // Touching the evicted page reads it back from the file and evicts page 1 (next oldest).
        check(memory.access(0, 0), "An evicted page must fault again");
        stats = memory.stats();
        check(stats.pagedIn == 4 && stats.pagedOut == 2, "Counters after the round trip");
        check(stats.corruptReads == 0, "The page read back must match what was written");
        check(!memory.access(1, 0), "Process 1's page was loaded after page 1, so it stays");
        check(memory.access(0, 99), "Out-of-range pages clamp to the last page");
        check(!memory.access(0, 3), "Page 3 is the clamped page and is now resident");
        check(memory.residentBytes(1) == 0, "FIFO evicted process 1, the oldest load");

        // Releasing frees frames and backing-store slots; the next eviction reuses a slot.
        memory.release(0);
        stats = memory.stats();
        check(stats.usedFrames == 0 && memory.residentBytes(0) == 0 && memory.pageCount(0) == 0,
              "Release must free every frame of the process");
        memory.release(0); // Releasing twice is harmless.
        memory.allocate(2, 128);
        check(memory.access(2, 0) && memory.access(2, 1), "New process faults in its pages");
        check(memory.stats().usedFrames == 2 && memory.stats().corruptReads == 0, "Memory stays consistent");

        bool threw = false;
        try
        {
            memory.allocate(2, 64);
        }
        catch (const std::logic_error &)
        {
            threw = true;
        }
        check(threw, "Allocating the same key twice must fail");

        // Stress: many processes over few frames never overfill memory or corrupt records.
        MemoryManager stress;
        stress.configure(memoryConfig(1024, 64), "");
        for (std::size_t pid = 0; pid < 8; ++pid)
        {
            stress.allocate(pid, 1024);
        }
        for (std::size_t round = 0; round < 2000; ++round)
        {
            stress.access(round % 8, (round / 8) % 16);
            check(stress.stats().usedBytes <= stress.stats().totalBytes, "Used memory exceeded total");
            if (round % 500 == 499)
            {
                stress.release(round % 8);
                stress.allocate(round % 8, 1024);
            }
        }
        check(stress.stats().pagedOut > 0 && stress.stats().corruptReads == 0, "Stress paging is inconsistent");

        // Page contents: values survive eviction through the backing-store file.
        MemoryManager data;
        data.configure(memoryConfig(128, 64), path); // 2 frames of 64 bytes
        data.allocate(0, 256);                        // 4 pages
        data.allocate(1, 128);                        // 2 pages
        check(data.read16(0, 0x10) == 0, "Uninitialized memory must read 0");
        data.write16(0, 0x10, 42);
        data.write16(0, 0x3F, 0xBEEF); // straddles pages 0 and 1
        check(data.read16(0, 0x10) == 42, "A written value must read back");
        data.write16(1, 0x00, 7);
        data.write16(1, 0x40, 9); // fills both frames with process 1, evicting process 0
        check(data.residentBytes(0) == 0, "Process 0's pages must have been evicted");
        {
            const std::string contents = fileContents(path);
            check(contents.find("pid=0 page=0") != std::string::npos, "Evicted page 0 must have a record");
            check(contents.find("002A") != std::string::npos, "The record must hold 42 as 002A");
            check(contents.find("pid=0 page=0                    | 0000 0000 0000 0000 0000 0000 0000 0000 002A") !=
                      std::string::npos,
                  "Record layout: padded header, '| ', little-endian words in hex");
            check(contents.find("EF") != std::string::npos && contents.find("00BE") != std::string::npos,
                  "A straddling value is split across two page records");
        }
        const std::uint64_t outBefore = data.stats().pagedOut;
        check(data.read16(0, 0x10) == 42, "A value must survive eviction and page-in");
        check(data.read16(0, 0x3F) == 0xBEEF, "A straddling value must survive eviction");
        check(data.read16(1, 0x00) == 7 && data.read16(1, 0x40) == 9, "Process 1's values survive too");
        check(data.stats().pagedOut > outBefore, "Reading evicted pages must page in and evict others");
        check(data.stats().corruptReads == 0, "Every record must read back intact");

        // Records show the process ID passed at allocation, not the internal key.
        MemoryManager labelled;
        labelled.configure(memoryConfig(64, 64), path); // a single frame
        labelled.allocate(5, 128, 77);
        labelled.write16(5, 0, 1);
        labelled.write16(5, 64, 2); // evicts page 0
        check(fileContents(path).find("pid=77 page=0") != std::string::npos, "Records must use the process ID");
        check(labelled.read16(5, 0) == 1 && labelled.stats().corruptReads == 0, "Labelled records must read back");

        // Range: the last valid uint16 starts 2 bytes before the end.
        check(!throwsOutOfRange([&] { data.write16(1, 126, 1); }), "address size-2 is valid");
        check(throwsOutOfRange([&] { data.write16(1, 127, 1); }), "address size-1 has only one byte");
        check(throwsOutOfRange([&] { data.read16(1, 0x1000); }), "address past the end must throw");
        try
        {
            data.read16(1, 0x1000);
        }
        catch (const std::out_of_range &error)
        {
            check(std::string(error.what()) == "0x1000", "The error names the hex address");
        }

        // Stress: random writes over few frames match a shadow copy after many evictions.
        MemoryManager shadowed;
        shadowed.configure(memoryConfig(256, 64), "");
        for (std::size_t pid = 0; pid < 4; ++pid)
        {
            shadowed.allocate(pid, 1024);
        }
        std::map<std::pair<std::size_t, std::uint64_t>, std::uint16_t> shadow;
        std::mt19937 rng(7);
        for (int round = 0; round < 5000; ++round)
        {
            const std::size_t pid = rng() % 4;
            const std::uint64_t address = (rng() % 511) * 2; // aligned, so writes never overlap
            if (rng() % 2 == 0)
            {
                const auto value = static_cast<std::uint16_t>(rng());
                shadowed.write16(pid, address, value);
                shadow[{pid, address}] = value;
            }
            else
            {
                const auto it = shadow.find({pid, address});
                check(shadowed.read16(pid, address) == (it == shadow.end() ? 0 : it->second),
                      "A read disagreed with the shadow copy");
            }
        }
        check(shadowed.stats().pagedOut > 0 && shadowed.stats().corruptReads == 0, "Shadow stress is inconsistent");

        std::remove(path.c_str());
        std::cout << "Memory manager checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::remove(path.c_str());
        std::cerr << error.what() << "\n";
        return 1;
    }
}
