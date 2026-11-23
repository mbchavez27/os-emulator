#include "kernel/MemoryManager.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

void MemoryManager::FileCloser::operator()(std::FILE *file) const noexcept
{
    std::fclose(file);
}

MemoryManager::~MemoryManager() = default;

void MemoryManager::configure(const Config &config, const std::string &backingStorePath)
{
    if (config.memPerFrame < 2 || config.memPerFrame % 2 != 0 || config.memPerFrame > config.maxOverallMem)
    {
        throw std::invalid_argument("mem-per-frame must be even and in [2, max-overall-mem]");
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
    // Header, "| ", then one 4-digit hex word plus separator per 2 bytes; the
    // last separator becomes the newline.
    recordWidth_ = kHeaderWidth + 2 + static_cast<std::size_t>(frameSize_ / 2) * 5;
    const auto frameCount = static_cast<std::size_t>(config.maxOverallMem / config.memPerFrame);
    physical_.assign(static_cast<std::size_t>(frameCount * frameSize_), 0);
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

void MemoryManager::allocate(std::size_t pid, std::uint64_t bytes, std::optional<std::uint32_t> processId)
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
    space.processId = processId ? *processId : pid;
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
    // First touch reads zeros; a page coming back from the store gets its bytes restored.
    std::memset(frameBytes(frame), 0, static_cast<std::size_t>(frameSize_));
    if (entry.slot)
    {
        if (!readSlot(*entry.slot, frame, it->second.processId, page))
        {
            ++corruptReads_;
            std::memset(frameBytes(frame), 0, static_cast<std::size_t>(frameSize_));
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

std::uint16_t MemoryManager::read16(std::size_t pid, std::uint64_t address)
{
    checkRange(pid, address);
    // Copy each byte before touching the next: loading the second page may evict the first.
    const std::uint8_t low = byteAt(pid, address);
    const std::uint8_t high = byteAt(pid, address + 1);
    return static_cast<std::uint16_t>(low | (high << 8));
}

void MemoryManager::write16(std::size_t pid, std::uint64_t address, std::uint16_t value)
{
    checkRange(pid, address);
    byteAt(pid, address) = static_cast<std::uint8_t>(value & 0xFF);
    byteAt(pid, address + 1) = static_cast<std::uint8_t>(value >> 8);
}

void MemoryManager::checkRange(std::size_t pid, std::uint64_t address) const
{
    const auto it = spaces_.find(pid);
    if (it == spaces_.end())
    {
        throw std::logic_error("Process " + std::to_string(pid) + " has no memory");
    }
    if (it->second.bytes < 2 || address > it->second.bytes - 2)
    {
        std::ostringstream hex;
        hex << "0x" << std::uppercase << std::hex << address;
        throw std::out_of_range(hex.str());
    }
}

std::uint8_t &MemoryManager::byteAt(std::size_t pid, std::uint64_t address)
{
    const auto page = static_cast<std::size_t>(address / frameSize_);
    access(pid, page);
    const std::size_t frame = *spaces_.at(pid).pages[page].frame;
    return frameBytes(frame)[address % frameSize_];
}

std::uint8_t *MemoryManager::frameBytes(std::size_t frame)
{
    return physical_.data() + frame * frameSize_;
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
    writeSlot(slot, frame, spaces_.at(owner.pid).processId, owner.page);
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

std::string MemoryManager::recordHeader(std::uint64_t processId, std::size_t page) const
{
    // e.g. "pid=12 page=3", space-padded so every record has the same width.
    std::string header = "pid=" + std::to_string(processId) + " page=" + std::to_string(page);
    header.resize(kHeaderWidth, ' ');
    return header;
}

void MemoryManager::writeSlot(std::size_t slot, std::size_t frame, std::uint64_t processId, std::size_t page)
{
    // "pid=3 page=12 ... | 0000 002A ...\n": the page as little-endian uint16 words in hex.
    std::ostringstream text;
    text << recordHeader(processId, page) << "| " << std::uppercase << std::hex << std::setfill('0');
    const std::uint8_t *bytes = frameBytes(frame);
    for (std::uint64_t offset = 0; offset < frameSize_; offset += 2)
    {
        if (offset != 0)
        {
            text << ' ';
        }
        text << std::setw(4) << (bytes[offset] | (bytes[offset + 1] << 8));
    }
    text << '\n';
    const std::string record = text.str();
    if (std::fseek(store_.get(), static_cast<long>(slot * recordWidth_), SEEK_SET) != 0 ||
        std::fwrite(record.data(), 1, record.size(), store_.get()) != record.size())
    {
        // The read-back check will count this page as corrupt.
        std::clearerr(store_.get());
    }
    std::fflush(store_.get());
}

bool MemoryManager::readSlot(std::size_t slot, std::size_t frame, std::uint64_t processId, std::size_t page)
{
    std::string record(recordWidth_, '\0');
    if (std::fseek(store_.get(), static_cast<long>(slot * recordWidth_), SEEK_SET) != 0 ||
        std::fread(&record[0], 1, recordWidth_, store_.get()) != recordWidth_)
    {
        std::clearerr(store_.get());
        return false;
    }
    if (record.compare(0, kHeaderWidth, recordHeader(processId, page)) != 0 ||
        record.compare(kHeaderWidth, 2, "| ") != 0 || record.back() != '\n')
    {
        return false;
    }

    std::uint8_t *bytes = frameBytes(frame);
    std::size_t position = kHeaderWidth + 2;
    for (std::uint64_t offset = 0; offset < frameSize_; offset += 2, position += 5)
    {
        unsigned value = 0;
        for (std::size_t digit = 0; digit < 4; ++digit)
        {
            const char c = record[position + digit];
            const int nibble = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (nibble < 0)
            {
                return false;
            }
            value = value * 16 + static_cast<unsigned>(nibble);
        }
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    }
    return true;
}
