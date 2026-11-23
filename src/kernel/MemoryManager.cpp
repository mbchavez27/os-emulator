#include "kernel/MemoryManager.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

void MemoryManager::FileCloser::operator()(std::FILE *file) const noexcept
{
    std::fclose(file);
}

MemoryManager::~MemoryManager() = default;

void MemoryManager::configure(const Config &config, const std::string &backingStorePath)
{
    if (config.memPerFrame == 0 || config.memPerFrame > config.maxOverallMem)
    {
        throw std::invalid_argument("mem-per-frame must be in [1, max-overall-mem]");
    }

    std::FILE *file = backingStorePath.empty() ? std::tmpfile()
                                               : std::fopen(backingStorePath.c_str(), "w+b");
    if (file == nullptr)
    {
        throw std::runtime_error("Cannot open backing store" +
                                 (backingStorePath.empty() ? std::string() : ": " + backingStorePath));
    }
    store_.reset(file);

    frameSize_ = config.memPerFrame;
    const auto frameCount = static_cast<std::size_t>(config.maxOverallMem / config.memPerFrame);
    frames_.assign(frameCount, std::nullopt);
    freeFrames_.clear();
    // Reversed so frames are handed out from 0 upwards.
    for (std::size_t frame = frameCount; frame > 0; --frame)
    {
        freeFrames_.push_back(frame - 1);
    }
    loadOrder_.clear();
    spaces_.clear();
    freeSlots_.clear();
    nextSlot_ = 0;
    pagedIn_ = 0;
    pagedOut_ = 0;
    corruptReads_ = 0;
}

bool MemoryManager::isConfigured() const noexcept
{
    return store_ != nullptr;
}

void MemoryManager::allocate(std::size_t pid, std::uint64_t bytes)
{
    if (!isConfigured())
    {
        throw std::logic_error("MemoryManager::allocate called before configure()");
    }
    if (spaces_.count(pid) != 0)
    {
        throw std::logic_error("Process " + std::to_string(pid) + " already has memory");
    }
    AddressSpace space;
    space.bytes = bytes;
    space.pages.resize(static_cast<std::size_t>((bytes + frameSize_ - 1) / frameSize_));
    spaces_.emplace(pid, std::move(space));
}

bool MemoryManager::access(std::size_t pid, std::size_t page)
{
    const auto it = spaces_.find(pid);
    if (it == spaces_.end())
    {
        throw std::logic_error("Process " + std::to_string(pid) + " has no memory");
    }
    if (it->second.pages.empty())
    {
        return false;
    }
    page = std::min(page, it->second.pages.size() - 1);
    if (it->second.pages[page].frame)
    {
        return false;
    }

    // Page fault: take a frame (evicting if needed), then fill it.
    const std::size_t frame = takeFrame();
    // Looked up again: evict() only edits entries, so `it` stays valid.
    PageEntry &entry = it->second.pages[page];
    if (entry.slot)
    {
        if (!readSlot(*entry.slot, pid, page))
        {
            ++corruptReads_;
        }
        freeSlots_.push_back(*entry.slot);
        entry.slot.reset();
    }
    entry.frame = frame;
    frames_[frame] = FrameOwner{pid, page};
    loadOrder_.push_back(frame);
    ++it->second.residentPages;
    ++pagedIn_;
    return true;
}

void MemoryManager::release(std::size_t pid)
{
    const auto it = spaces_.find(pid);
    if (it == spaces_.end())
    {
        return;
    }
    for (PageEntry &entry : it->second.pages)
    {
        if (entry.frame)
        {
            frames_[*entry.frame].reset();
            freeFrames_.push_back(*entry.frame);
        }
        if (entry.slot)
        {
            freeSlots_.push_back(*entry.slot);
        }
    }
    loadOrder_.erase(std::remove_if(loadOrder_.begin(), loadOrder_.end(),
                                    [this](std::size_t frame) { return !frames_[frame]; }),
                     loadOrder_.end());
    spaces_.erase(it);
}

std::size_t MemoryManager::pageCount(std::size_t pid) const
{
    const auto it = spaces_.find(pid);
    return it == spaces_.end() ? 0 : it->second.pages.size();
}

std::uint64_t MemoryManager::allocatedBytes(std::size_t pid) const
{
    const auto it = spaces_.find(pid);
    return it == spaces_.end() ? 0 : it->second.bytes;
}

std::uint64_t MemoryManager::residentBytes(std::size_t pid) const
{
    const auto it = spaces_.find(pid);
    return it == spaces_.end() ? 0 : it->second.residentPages * frameSize_;
}

MemoryStats MemoryManager::stats() const
{
    MemoryStats stats;
    stats.frameSize = frameSize_;
    stats.totalFrames = frames_.size();
    stats.usedFrames = frames_.size() - freeFrames_.size();
    stats.totalBytes = stats.totalFrames * frameSize_;
    stats.usedBytes = stats.usedFrames * frameSize_;
    stats.freeBytes = stats.totalBytes - stats.usedBytes;
    stats.pagedIn = pagedIn_;
    stats.pagedOut = pagedOut_;
    stats.corruptReads = corruptReads_;
    return stats;
}

std::size_t MemoryManager::takeFrame()
{
    if (freeFrames_.empty())
    {
        // FIFO replacement: the page loaded longest ago leaves first.
        const std::size_t victim = loadOrder_.front();
        loadOrder_.pop_front();
        evict(victim);
    }
    const std::size_t frame = freeFrames_.back();
    freeFrames_.pop_back();
    return frame;
}

void MemoryManager::evict(std::size_t frame)
{
    const FrameOwner owner = *frames_[frame];
    PageEntry &entry = spaces_.at(owner.pid).pages[owner.page];
    const std::size_t slot = takeSlot();
    writeSlot(slot, owner.pid, owner.page);
    entry.frame.reset();
    entry.slot = slot;
    --spaces_.at(owner.pid).residentPages;
    frames_[frame].reset();
    freeFrames_.push_back(frame);
    ++pagedOut_;
}

std::size_t MemoryManager::takeSlot()
{
    if (!freeSlots_.empty())
    {
        const std::size_t slot = freeSlots_.back();
        freeSlots_.pop_back();
        return slot;
    }
    return nextSlot_++;
}

std::string MemoryManager::record(std::size_t pid, std::size_t page) const
{
    // e.g. "pid=12 page=3", space-padded to a fixed width so slots can be overwritten in place.
    std::string text = "pid=" + std::to_string(pid) + " page=" + std::to_string(page);
    text.resize(kRecordWidth - 1, ' ');
    text.push_back('\n');
    return text;
}

void MemoryManager::writeSlot(std::size_t slot, std::size_t pid, std::size_t page)
{
    const std::string text = record(pid, page);
    if (std::fseek(store_.get(), static_cast<long>(slot * kRecordWidth), SEEK_SET) != 0 ||
        std::fwrite(text.data(), 1, text.size(), store_.get()) != text.size())
    {
        // The page is still tracked in memory; a failed read-back counts as corrupt.
        std::clearerr(store_.get());
    }
    std::fflush(store_.get());
}

bool MemoryManager::readSlot(std::size_t slot, std::size_t pid, std::size_t page)
{
    char buffer[kRecordWidth];
    if (std::fseek(store_.get(), static_cast<long>(slot * kRecordWidth), SEEK_SET) != 0 ||
        std::fread(buffer, 1, kRecordWidth, store_.get()) != kRecordWidth)
    {
        std::clearerr(store_.get());
        return false;
    }
    const std::string expected = record(pid, page);
    return std::memcmp(buffer, expected.data(), kRecordWidth) == 0;
}
