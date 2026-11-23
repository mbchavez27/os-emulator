#pragma once

#include "config/Config.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/** @brief Copied memory counters, as shown by process-smi and vmstat. */
struct MemoryStats
{
    std::uint64_t totalBytes = 0;  /**< max-overall-mem. */
    std::uint64_t usedBytes = 0;   /**< Occupied frames * frame size. */
    std::uint64_t freeBytes = 0;   /**< totalBytes - usedBytes. */
    std::uint64_t frameSize = 0;   /**< mem-per-frame; also the page size. */
    std::size_t totalFrames = 0;   /**< max-overall-mem / mem-per-frame. */
    std::size_t usedFrames = 0;    /**< Frames holding a page. */
    std::uint64_t pagedIn = 0;     /**< Page faults served (pages loaded into a frame). */
    std::uint64_t pagedOut = 0;    /**< Pages evicted to the backing store. */
    std::uint64_t corruptReads = 0; /**< Backing-store records that did not match on page-in. */
};

/**
 * @brief Demand-paging allocator over a fixed pool of frames.
 *
 * A process gets a page table at allocate() with no resident pages. access()
 * loads a missing page into a free frame (a page fault); when none is free,
 * the oldest loaded page (global FIFO) is written to the backing store file
 * and its frame reused. Evicted pages are read back from that file when
 * touched again.
 *
 * Not thread-safe: Scheduler calls it only while holding its mutex.
 */
class MemoryManager
{
public:
    MemoryManager() = default;
    ~MemoryManager();

    MemoryManager(const MemoryManager &) = delete;
    MemoryManager &operator=(const MemoryManager &) = delete;

    /**
     * @brief Sets the frame pool and opens a fresh, empty backing store.
     *
     * Releases every allocation made before.
     *
     * @param config supplies max-overall-mem and mem-per-frame
     * @param backingStorePath file to use; empty uses an anonymous temporary file
     * @throws std::invalid_argument if the frame size is 0 or larger than memory
     * @throws std::runtime_error if the backing store cannot be opened
     */
    void configure(const Config &config, const std::string &backingStorePath);

    /** @return true once configure() has succeeded */
    bool isConfigured() const noexcept;

    /**
     * @brief Creates a page table for a process; no page becomes resident.
     *
     * @param pid a unique key for the process (the scheduler passes its process index)
     * @param bytes the process's memory size; rounded up to whole pages
     * @throws std::logic_error if not configured or pid is already allocated
     */
    void allocate(std::size_t pid, std::uint64_t bytes);

    /**
     * @brief Touches one page, loading it on demand.
     *
     * @param pid an allocated process
     * @param page index in [0, pageCount(pid)); larger values are clamped
     * @return true if the access caused a page fault
     * @throws std::logic_error if pid is not allocated
     */
    bool access(std::size_t pid, std::size_t page);

    /** @brief Frees a process's frames and backing-store slots; no-op if unknown. */
    void release(std::size_t pid);

    /** @return the pages in pid's address space, or 0 if not allocated */
    std::size_t pageCount(std::size_t pid) const;

    /** @return pid's allocated memory in bytes, or 0 if not allocated */
    std::uint64_t allocatedBytes(std::size_t pid) const;

    /** @return bytes of physical memory pid currently occupies */
    std::uint64_t residentBytes(std::size_t pid) const;

    /** @return a copy of the counters */
    MemoryStats stats() const;

private:
    /** @brief Where one virtual page currently lives. */
    struct PageEntry
    {
        std::optional<std::size_t> frame; /**< Set while resident. */
        std::optional<std::size_t> slot;  /**< Set while stored in the backing store. */
    };

    /** @brief One process's page table. */
    struct AddressSpace
    {
        std::uint64_t bytes = 0;
        std::vector<PageEntry> pages;
        std::size_t residentPages = 0;
    };

    /** @brief Which page a frame holds. */
    struct FrameOwner
    {
        std::size_t pid = 0;
        std::size_t page = 0;
    };

    std::size_t takeFrame();
    void evict(std::size_t frame);
    std::size_t takeSlot();
    std::string record(std::size_t pid, std::size_t page) const;
    void writeSlot(std::size_t slot, std::size_t pid, std::size_t page);
    bool readSlot(std::size_t slot, std::size_t pid, std::size_t page);

    /** @brief Fixed width of one backing-store record, newline included. */
    static constexpr std::size_t kRecordWidth = 32;

    struct FileCloser
    {
        void operator()(std::FILE *file) const noexcept;
    };

    std::uint64_t frameSize_ = 0;
    std::vector<std::optional<FrameOwner>> frames_;
    std::vector<std::size_t> freeFrames_;
    std::deque<std::size_t> loadOrder_; /**< Resident frames, oldest load first (FIFO). */
    std::map<std::size_t, AddressSpace> spaces_;
    std::vector<std::size_t> freeSlots_;
    std::size_t nextSlot_ = 0;
    std::uint64_t pagedIn_ = 0;
    std::uint64_t pagedOut_ = 0;
    std::uint64_t corruptReads_ = 0;
    std::unique_ptr<std::FILE, FileCloser> store_;
};
